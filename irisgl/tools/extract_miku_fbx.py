#!/usr/bin/env python3
"""Extract static triangle geometry from the supplied Miku FBX.

This intentionally reads only the FBX Geometry/Mesh control points and
PolygonVertexIndex arrays. Rigging, animation, normals, UVs and materials are
not consumed by the first IRIS GL viewport bring-up.

The parser supports binary FBX 7500, which is the format of the supplied
Miku.fbx. Output is a self-contained C header containing indexed float3
geometry and preserved submesh ranges.
"""

import argparse
import hashlib
import struct
import zlib

NULL_RECORD_7500 = 25
EXPECTED_FBX_SHA256 = "451f0fc482afa804b48667ad23c0743e3dc628e2e9f8f5acea432c753d1b7541"


class Node:
    def __init__(self, name, props, children):
        self.name = name
        self.props = props
        self.children = children


def read_prop(data, off):
    kind = chr(data[off])
    off += 1

    scalar = {
        "Y": ("<h", 2),
        "C": ("<?", 1),
        "I": ("<i", 4),
        "F": ("<f", 4),
        "D": ("<d", 8),
        "L": ("<q", 8),
    }
    if kind in scalar:
        fmt, size = scalar[kind]
        return struct.unpack_from(fmt, data, off)[0], off + size

    if kind in "fdlibc":
        count, encoding, encoded_len = struct.unpack_from("<III", data, off)
        off += 12
        raw = data[off:off + encoded_len]
        off += encoded_len
        if encoding == 1:
            raw = zlib.decompress(raw)
        elif encoding != 0:
            raise ValueError("unsupported FBX array encoding %d" % encoding)

        fmt = {
            "f": "f",
            "d": "d",
            "l": "q",
            "i": "i",
            "b": "?",
            "c": "b",
        }[kind]
        elem_size = struct.calcsize("<" + fmt)
        if len(raw) != count * elem_size:
            raise ValueError("bad FBX array byte count")
        return list(struct.unpack("<%d%s" % (count, fmt), raw)), off

    if kind in "SR":
        length = struct.unpack_from("<I", data, off)[0]
        off += 4
        raw = data[off:off + length]
        off += length
        if kind == "S":
            return raw.decode("utf-8", "replace"), off
        return raw, off

    raise ValueError("unsupported FBX property type %r" % kind)


def read_node(data, off):
    start = off
    end_off, prop_count, prop_bytes = struct.unpack_from("<QQQ", data, off)
    off += 24
    name_len = data[off]
    off += 1

    if end_off == 0:
        return None, start + NULL_RECORD_7500

    name = data[off:off + name_len].decode("utf-8", "replace")
    off += name_len

    props = []
    for _ in range(prop_count):
        value, off = read_prop(data, off)
        props.append(value)

    children = []
    while off < end_off - NULL_RECORD_7500:
        child, next_off = read_node(data, off)
        if child is None:
            off = next_off
            break
        children.append(child)
        off = next_off

    return Node(name, props, children), end_off


def parse_fbx(data):
    if not data.startswith(b"Kaydara FBX Binary  \x00\x1a\x00"):
        raise ValueError("not a binary FBX file")
    version = struct.unpack_from("<I", data, 23)[0]
    if version != 7500:
        raise ValueError("expected FBX 7500, got %d" % version)

    roots = []
    off = 27
    while off < len(data) - NULL_RECORD_7500:
        node, next_off = read_node(data, off)
        if node is None:
            break
        roots.append(node)
        off = next_off
    return roots


def decode_polygons(encoded):
    polygons = []
    current = []
    for value in encoded:
        if value < 0:
            current.append(-value - 1)
            polygons.append(current)
            current = []
        else:
            current.append(value)
    if current:
        raise ValueError("unterminated PolygonVertexIndex polygon")
    return polygons


def clean_name(value):
    return value.split("\x00", 1)[0]


def extract_meshes(roots):
    objects = next((node for node in roots if node.name == "Objects"), None)
    if objects is None:
        raise ValueError("FBX has no Objects node")

    meshes = []
    for geom in objects.children:
        if geom.name != "Geometry" or len(geom.props) < 3 or geom.props[2] != "Mesh":
            continue

        fields = {child.name: child for child in geom.children}
        if "Vertices" not in fields or "PolygonVertexIndex" not in fields:
            continue

        flat_vertices = fields["Vertices"].props[0]
        if len(flat_vertices) % 3:
            raise ValueError("vertex array is not float3")
        vertices = [tuple(flat_vertices[i:i + 3]) for i in range(0, len(flat_vertices), 3)]

        polygons = decode_polygons(fields["PolygonVertexIndex"].props[0])
        if not all(len(poly) == 3 for poly in polygons):
            raise ValueError("first Miku bring-up expects already-triangulated geometry")

        indices = [index for poly in polygons for index in poly]
        if any(index < 0 or index >= len(vertices) for index in indices):
            raise ValueError("out-of-range mesh index")

        meshes.append((clean_name(geom.props[1]), vertices, indices))

    if not meshes:
        raise ValueError("FBX contains no mesh geometry")
    return meshes


def emit_header(meshes, source_sha):
    all_vertices = []
    all_indices = []
    ranges = []

    for name, vertices, indices in meshes:
        first_vertex = len(all_vertices)
        first_index = len(all_indices)
        all_vertices.extend(vertices)
        all_indices.extend(first_vertex + index for index in indices)
        ranges.append((name, first_vertex, len(vertices), first_index, len(indices)))

    if len(all_vertices) >= 65536:
        raise ValueError("mesh no longer fits uint16_t indices")

    xs = [v[0] for v in all_vertices]
    ys = [v[1] for v in all_vertices]
    zs = [v[2] for v in all_vertices]

    out = []
    out.append("/* Generated from Miku.fbx sha256 %s. */" % source_sha)
    out.append("/* Static geometry only: rig, animation, normals, UVs and materials omitted. */")
    out.append("#ifndef IRISGL_MIKU_MESH_H")
    out.append("#define IRISGL_MIKU_MESH_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("struct igl_miku_vertex { float x, y, z; };")
    out.append("struct igl_miku_submesh {")
    out.append("    const char *name;")
    out.append("    uint32_t first_vertex, vertex_count;")
    out.append("    uint32_t first_index, index_count;")
    out.append("};")
    out.append("")
    out.append("#define IGL_MIKU_VERTEX_COUNT %du" % len(all_vertices))
    out.append("#define IGL_MIKU_INDEX_COUNT %du" % len(all_indices))
    out.append("#define IGL_MIKU_TRIANGLE_COUNT %du" % (len(all_indices) // 3))
    out.append("#define IGL_MIKU_SUBMESH_COUNT %du" % len(ranges))
    out.append("")
    out.append("static const struct igl_miku_vertex igl_miku_vertices[IGL_MIKU_VERTEX_COUNT] = {")
    for x, y, z in all_vertices:
        out.append("    { %.9gf, %.9gf, %.9gf }," % (x, y, z))
    out.append("};")
    out.append("")
    out.append("static const uint16_t igl_miku_indices[IGL_MIKU_INDEX_COUNT] = {")
    for base in range(0, len(all_indices), 12):
        out.append("    " + ", ".join(str(i) for i in all_indices[base:base + 12]) + ",")
    out.append("};")
    out.append("")
    out.append("static const struct igl_miku_submesh igl_miku_submeshes[IGL_MIKU_SUBMESH_COUNT] = {")
    for name, first_vertex, vertex_count, first_index, index_count in ranges:
        out.append('    { "%s", %du, %du, %du, %du },' %
                   (name, first_vertex, vertex_count, first_index, index_count))
    out.append("};")
    out.append("")
    out.append("#define IGL_MIKU_MIN_X (%.9gf)" % min(xs))
    out.append("#define IGL_MIKU_MAX_X (%.9gf)" % max(xs))
    out.append("#define IGL_MIKU_MIN_Y (%.9gf)" % min(ys))
    out.append("#define IGL_MIKU_MAX_Y (%.9gf)" % max(ys))
    out.append("#define IGL_MIKU_MIN_Z (%.9gf)" % min(zs))
    out.append("#define IGL_MIKU_MAX_Z (%.9gf)" % max(zs))
    out.append("")
    out.append("#endif /* IRISGL_MIKU_MESH_H */")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fbx", help="path to Miku.fbx")
    parser.add_argument("output", nargs="?", default="tools/miku_mesh.h")
    parser.add_argument("--allow-different-source", action="store_true",
                        help="allow an FBX whose sha256 differs from the supplied model")
    args = parser.parse_args()

    with open(args.fbx, "rb") as fp:
        data = fp.read()
    source_sha = hashlib.sha256(data).hexdigest()
    if source_sha != EXPECTED_FBX_SHA256 and not args.allow_different_source:
        raise SystemExit("unexpected Miku.fbx sha256: %s" % source_sha)

    roots = parse_fbx(data)
    meshes = extract_meshes(roots)
    header = emit_header(meshes, source_sha)
    with open(args.output, "w", encoding="ascii") as fp:
        fp.write(header)

    vertex_count = sum(len(vertices) for _, vertices, _ in meshes)
    index_count = sum(len(indices) for _, _, indices in meshes)
    print("wrote %s: %d meshes, %d vertices, %d triangles" %
          (args.output, len(meshes), vertex_count, index_count // 3))


if __name__ == "__main__":
    main()
