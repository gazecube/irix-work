#!/usr/bin/env python3
"""Write an Xcursor theme straight from a cursor-font PCF file (no X server needed).

Used for IRIX's own cursor font (Xsgi's cursor.pcf), whose glyphs differ from X.org's.
Shapes are drawn like 4Dwm draws its pointer (*pointerColor: red): a red shape with a white outline.
Freedesktop/CSS cursor names are symlinks to the nearest X shape (same table as mkxcursors.c).

usage: mkxcursors-pcf.py CURSOR.PCF OUTDIR [fg-hex] [bg-hex]
"""
import os, struct, sys

NAMES = ["X_cursor","arrow","based_arrow_down","based_arrow_up","boat","bogosity","bottom_left_corner",
 "bottom_right_corner","bottom_side","bottom_tee","box_spiral","center_ptr","circle","clock","coffee_mug",
 "cross","cross_reverse","crosshair","diamond_cross","dot","dotbox","double_arrow","draft_large","draft_small",
 "draped_box","exchange","fleur","gobbler","gumby","hand1","hand2","heart","icon","iron_cross","left_ptr",
 "left_side","left_tee","leftbutton","ll_angle","lr_angle","man","middlebutton","mouse","pencil","pirate","plus",
 "question_arrow","right_ptr","right_side","right_tee","rightbutton","rtl_logo","sailboat","sb_down_arrow",
 "sb_h_double_arrow","sb_left_arrow","sb_right_arrow","sb_up_arrow","sb_v_double_arrow","shuttle","sizing",
 "spider","spraycan","star","target","tcross","top_left_arrow","top_left_corner","top_right_corner","top_side",
 "top_tee","trek","ul_angle","umbrella","ur_angle","watch","xterm"]          # shape number = 2 * index

ALIASES = {"default":"left_ptr","pointer":"hand2",
 "pointing_hand":"hand2","hand":"hand2","text":"xterm","ibeam":"xterm","vertical-text":"xterm","wait":"watch",
 "progress":"watch","left_ptr_watch":"watch","half-busy":"watch",
 "move":"fleur","all-scroll":"fleur","size_all":"fleur","grab":"hand1","grabbing":"hand1","openhand":"hand1",
 "closedhand":"hand1","help":"question_arrow","whats_this":"question_arrow","not-allowed":"pirate",
 "no-drop":"pirate","forbidden":"pirate","crossed_circle":"pirate","context-menu":"left_ptr","alias":"left_ptr",
 "copy":"left_ptr","dnd-copy":"left_ptr","cell":"plus","zoom-in":"left_ptr","zoom-out":"left_ptr",
 "col-resize":"sb_h_double_arrow","ew-resize":"sb_h_double_arrow","h_double_arrow":"sb_h_double_arrow",
 "split_h":"sb_h_double_arrow","size_hor":"sb_h_double_arrow","row-resize":"sb_v_double_arrow",
 "ns-resize":"sb_v_double_arrow","v_double_arrow":"sb_v_double_arrow","split_v":"sb_v_double_arrow",
 "size_ver":"sb_v_double_arrow","nesw-resize":"sizing","nwse-resize":"sizing","size_bdiag":"sizing",
 "size_fdiag":"sizing","e-resize":"right_side","w-resize":"left_side","n-resize":"top_side",
 "s-resize":"bottom_side","ne-resize":"top_right_corner","nw-resize":"top_left_corner",
 "se-resize":"bottom_right_corner","sw-resize":"bottom_left_corner","up_arrow":"sb_up_arrow",
 "down_arrow":"sb_down_arrow","left_arrow":"sb_left_arrow","right_arrow":"sb_right_arrow"}


def read_pcf(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x01fcp":
        raise SystemExit("not a PCF file: %s" % path)
    tables = {}
    for i in range(struct.unpack("<I", d[4:8])[0]):
        t, f, sz, off = struct.unpack("<IIII", d[8 + 16 * i:24 + 16 * i])
        tables[t] = (f, sz, off)

    def fmt_end(f):
        return ">" if f & 4 else "<"

    f, sz, off = tables[8]                      # BITMAPS
    e = fmt_end(f)
    n = struct.unpack(e + "I", d[off + 4:off + 8])[0]
    offs = struct.unpack(e + "%dI" % n, d[off + 8:off + 8 + 4 * n])
    base = off + 8 + 4 * n + 16
    pad = [1, 2, 4, 8][f & 3]
    msb = bool(f & 8)
    f, sz, off = tables[4]                      # METRICS
    e = fmt_end(f)
    metrics = []
    if f & 0x100:                               # compressed: 5 unsigned bytes, biased by 0x80
        n = struct.unpack(e + "H", d[off + 4:off + 6])[0]
        for i in range(n):
            lb, rb, w, a, de = [b - 0x80 for b in d[off + 6 + 5 * i:off + 11 + 5 * i]]
            metrics.append((lb, rb, w, a, de))
    else:
        n = struct.unpack(e + "I", d[off + 4:off + 8])[0]
        for i in range(n):
            lb, rb, w, a, de, _ = struct.unpack(e + "hhhhhH", d[off + 8 + 12 * i:off + 20 + 12 * i])
            metrics.append((lb, rb, w, a, de))
    f, sz, off = tables[0x20]                   # BDF_ENCODINGS
    e = fmt_end(f)
    minb2, maxb2, minb1, maxb1, dflt = struct.unpack(e + "5H", d[off + 4:off + 14])
    cnt = (maxb2 - minb2 + 1) * (maxb1 - minb1 + 1)
    idx = struct.unpack(e + "%dH" % cnt, d[off + 14:off + 14 + 2 * cnt])
    enc = {}
    for i, g in enumerate(idx):
        if g != 0xFFFF:
            enc[minb2 + i] = g                  # single-byte fonts: byte1 range is 0

    def glyph(code):
        g = enc.get(code)
        if g is None:
            return None
        lb, rb, w, a, de = metrics[g]
        gw, gh = rb - lb, a + de
        stride = (((gw + 8 * pad - 1) // (8 * pad)) * pad) if gw > 0 else 0
        o = base + offs[g]
        pix = set()
        for y in range(gh):
            row = d[o + y * stride:o + (y + 1) * stride]
            for x in range(gw):
                byte = row[x >> 3]
                bit = (byte >> (7 - (x & 7))) & 1 if msb else (byte >> (x & 7)) & 1
                if bit:
                    pix.add((lb + x, -a + y))       # relative to the glyph origin
        return pix
    return glyph


def le32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    glyph = read_pcf(sys.argv[1])
    out = sys.argv[2]
    fg = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xFF0000
    bg = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xFFFFFF
    cdir = os.path.join(out, "cursors")
    os.makedirs(cdir, exist_ok=True)
    made = 0
    for i, name in enumerate(NAMES):
        shape = 2 * i
        src, msk = glyph(shape), glyph(shape + 1)
        if not src and not msk:
            continue
        src, msk = src or set(), msk or set()
        allp = src | msk
        minx = min(x for x, _ in allp); maxx = max(x for x, _ in allp)
        miny = min(y for _, y in allp); maxy = max(y for _, y in allp)
        w, h = maxx - minx + 1, maxy - miny + 1
        hx = min(max(-minx, 0), w - 1); hy = min(max(-miny, 0), h - 1)
        px = []
        for y in range(miny, maxy + 1):
            for x in range(minx, maxx + 1):
                if (x, y) in src:
                    px.append(0xFF000000 | fg)
                elif (x, y) in msk:
                    px.append(0xFF000000 | bg)
                else:
                    px.append(0)
        body = b"".join(le32(v) for v in px)
        chunk = le32(36) + le32(0xFFFD0002) + le32(h) + le32(1) + le32(w) + le32(h) + le32(hx) + le32(hy) + le32(0) + body
        with open(os.path.join(cdir, name), "wb") as f:
            f.write(b"Xcur" + le32(16) + le32(0x10000) + le32(1) + le32(0xFFFD0002) + le32(h) + le32(28) + chunk)
        made += 1
    for alias, target in ALIASES.items():
        p = os.path.join(cdir, alias)
        if alias in NAMES:
            continue                            # a real X shape, never an alias
        if os.path.exists(os.path.join(cdir, target)):
            if os.path.lexists(p):
                os.unlink(p)
            os.symlink(target, p)
    with open(os.path.join(out, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=x-red\nComment=The Xsgi cursor font in 4Dwm's pointer red (generated by mkxcursors-pcf.py)\n")
    print("wrote %d shapes + %d aliases to %s" % (made, len(ALIASES), out))


main()
