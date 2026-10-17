#!/usr/bin/env python3
import argparse
import re
import sys

NAMED = {
    "iconcolor": "#c8c8c8",
    "shadowcolor": "#555555",
    "outlinecolor": "#000000",
    "black": "#000000",
    "white": "#ffffff",
}

TOKEN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*;")


def parse(text):
    text = re.sub(r"#[^\n]*", "", text)
    shapes = []
    color = "iconcolor"
    cur = None
    for name, args in TOKEN.findall(text):
        args = [a.strip() for a in args.split(",") if a.strip()]
        if name == "color":
            color = args[0] if args else color
        elif name in ("bgnpolygon", "bgnoutlinepolygon", "bgnline",
                      "bgnclosedline"):
            cur = {"kind": name[3:], "color": color, "pts": [], "outline": None}
        elif name == "vertex" and cur is not None:
            cur["pts"].append((float(args[0]), float(args[1])))
        elif name.startswith("end") and cur is not None:
            if args:
                cur["outline"] = args[0]
            shapes.append(cur)
            cur = None
        else:
            sys.stderr.write("fti2svg: ignored %s(%s)\n" % (name, ",".join(args)))
    return shapes


IRIS = ["#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff",
        "#00ffff", "#ffffff"]


def numeric_color(color):
    try:
        n = int(color)
    except ValueError:
        return None
    if n < 0:
        g = min(-n, 255)
        return "#%02x%02x%02x" % (g, g, g)
    if n < len(IRIS):
        return IRIS[n]
    return None


def fill_of(color, colors):
    if color in colors:
        return colors[color]
    c = numeric_color(color)
    if c:
        return c
    sys.stderr.write("fti2svg: unknown colour %r, using grey\n" % color)
    return "#808080"


def to_svg(shapes, colors):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" '
           'viewBox="0 0 100 100">']
    for n, s in enumerate(shapes):
        pts = " ".join("%.2f,%.2f" % (x, 100.0 - y) for x, y in s["pts"])
        fill = fill_of(s["color"], colors)
        label = s["color"]
        if s["kind"] == "polygon":
            out.append('  <polygon id="s%d" data-fti-color="%s" fill="%s" '
                       'points="%s"/>' % (n, label, fill, pts))
        elif s["kind"] == "outlinepolygon":
            stroke = fill_of(s["outline"] or "outlinecolor", colors)
            out.append('  <polygon id="s%d" data-fti-color="%s" '
                       'data-fti-outline="%s" fill="%s" stroke="%s" '
                       'stroke-width="1" stroke-linejoin="round" '
                       'points="%s"/>' % (n, label, s["outline"] or "outlinecolor",
                                          fill, stroke, pts))
        else:
            tag = "polygon" if s["kind"] == "closedline" else "polyline"
            out.append('  <%s id="s%d" data-fti-color="%s" fill="none" '
                       'stroke="%s" stroke-width="1" points="%s"/>'
                       % (tag, n, label, fill, pts))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(
        description="Convert an IRIX .fti icon (flat polygons/lines) to SVG. "
                    "y is flipped; the icon grid 0..100 becomes a 100x100 viewBox.")
    ap.add_argument("fti")
    ap.add_argument("svg", nargs="?")
    ap.add_argument("--color", action="append", default=[],
                    help="name=#rrggbb for an FTI colour name")
    a = ap.parse_args()
    colors = dict(NAMED)
    for c in a.color:
        k, v = c.split("=", 1)
        colors[k] = v
    svg = to_svg(parse(open(a.fti, encoding="latin-1").read()), colors)
    if a.svg:
        open(a.svg, "w").write(svg)
    else:
        sys.stdout.write(svg)


if __name__ == "__main__":
    main()
