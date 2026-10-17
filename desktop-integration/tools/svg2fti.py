#!/usr/bin/env python3
import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET

NAMED = {
    "iconcolor": "#c8c8c8",
    "shadowcolor": "#555555",
    "outlinecolor": "#000000",
}
CSS = {
    "black": "#000000", "white": "#ffffff", "red": "#ff0000",
    "green": "#008000", "lime": "#00ff00", "blue": "#0000ff",
    "yellow": "#ffff00", "gray": "#808080", "grey": "#808080",
    "silver": "#c0c0c0", "none": None,
}
NUM = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def ident():
    return [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]


def mul(m, n):
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return [a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D,
            a * E + c * F + e, b * E + d * F + f]


def apply(m, x, y):
    return (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])


def parse_transform(text):
    m = ident()
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text or ""):
        v = [float(x) for x in NUM.findall(args)]
        if name == "matrix" and len(v) == 6:
            t = v
        elif name == "translate":
            t = [1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0]
        elif name == "scale":
            t = [v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0]
        elif name == "rotate":
            r = math.radians(v[0])
            t = [math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0]
            if len(v) == 3:
                t = mul(mul([1, 0, 0, 1, v[1], v[2]], t),
                        [1, 0, 0, 1, -v[1], -v[2]])
        elif name == "skewX":
            t = [1, 0, math.tan(math.radians(v[0])), 1, 0, 0]
        elif name == "skewY":
            t = [1, math.tan(math.radians(v[0])), 0, 1, 0, 0]
        else:
            continue
        m = mul(m, t)
    return m


def bez(p0, p1, p2, p3, steps):
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0]
                    + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1]
                    + t**3 * p3[1]))
    return out


def arc_points(p0, rx, ry, phi, large, sweep, p1, steps):
    if rx == 0 or ry == 0:
        return [p1]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1 = cp * dx + sp * dy
    y1 = -sp * dx + cp * dy
    lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp = co * rx * y1 / ry
    cyp = -co * ry * x1 / rx
    cx = cp * cxp - sp * cyp + (p0[0] + p1[0]) / 2
    cy = sp * cxp + cp * cyp + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    th1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry)
    dth = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx,
              (-y1 - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi
    out = []
    for i in range(1, steps + 1):
        t = th1 + dth * i / steps
        x = rx * math.cos(t)
        y = ry * math.sin(t)
        out.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return out


def path_subpaths(d, steps):
    toks = re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|" + NUM.pattern, d)
    i = 0
    subs = []
    cur = None
    pos = (0.0, 0.0)
    start = pos
    cmd = None
    last_ctrl = None
    last_cmd = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in "Zz":
                if cur:
                    subs.append((cur, True))
                    cur = None
                pos = start
                last_cmd = cmd
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            x, y = num(), num()
            if rel:
                x, y = pos[0] + x, pos[1] + y
            if cur:
                subs.append((cur, False))
            pos = start = (x, y)
            cur = [pos]
            cmd = "l" if rel else "L"
        elif c == "L":
            x, y = num(), num()
            if rel:
                x, y = pos[0] + x, pos[1] + y
            pos = (x, y)
            cur = cur or [start]
            cur.append(pos)
        elif c == "H":
            x = num()
            pos = (pos[0] + x if rel else x, pos[1])
            cur = cur or [start]
            cur.append(pos)
        elif c == "V":
            y = num()
            pos = (pos[0], pos[1] + y if rel else y)
            cur = cur or [start]
            cur.append(pos)
        elif c in "CS":
            if c == "C":
                x1, y1 = num(), num()
                if rel:
                    x1, y1 = pos[0] + x1, pos[1] + y1
            else:
                if last_cmd and last_cmd.upper() in "CS" and last_ctrl:
                    x1, y1 = 2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]
                else:
                    x1, y1 = pos
            x2, y2, x, y = num(), num(), num(), num()
            if rel:
                x2, y2 = pos[0] + x2, pos[1] + y2
                x, y = pos[0] + x, pos[1] + y
            cur = cur or [start]
            cur.extend(bez(pos, (x1, y1), (x2, y2), (x, y), steps))
            last_ctrl = (x2, y2)
            pos = (x, y)
        elif c in "QT":
            if c == "Q":
                qx, qy = num(), num()
                if rel:
                    qx, qy = pos[0] + qx, pos[1] + qy
            else:
                if last_cmd and last_cmd.upper() in "QT" and last_ctrl:
                    qx, qy = 2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]
                else:
                    qx, qy = pos
            x, y = num(), num()
            if rel:
                x, y = pos[0] + x, pos[1] + y
            c1 = (pos[0] + 2 / 3 * (qx - pos[0]), pos[1] + 2 / 3 * (qy - pos[1]))
            c2 = (x + 2 / 3 * (qx - x), y + 2 / 3 * (qy - y))
            cur = cur or [start]
            cur.extend(bez(pos, c1, c2, (x, y), steps))
            last_ctrl = (qx, qy)
            pos = (x, y)
        elif c == "A":
            rx, ry, rot, lg, sw, x, y = (num(), num(), num(), num(), num(),
                                         num(), num())
            if rel:
                x, y = pos[0] + x, pos[1] + y
            cur = cur or [start]
            cur.extend(arc_points(pos, rx, ry, rot, int(lg), int(sw), (x, y),
                                  steps))
            pos = (x, y)
        last_cmd = cmd
    if cur:
        subs.append((cur, False))
    return subs


def ellipse_pts(cx, cy, rx, ry, steps):
    return [(cx + rx * math.cos(2 * math.pi * k / steps),
             cy + ry * math.sin(2 * math.pi * k / steps)) for k in range(steps)]


def parse_style(el):
    st = {}
    for k in ("fill", "stroke", "opacity", "fill-opacity", "display"):
        if el.get(k) is not None:
            st[k] = el.get(k)
    for part in (el.get("style") or "").split(";"):
        if ":" in part:
            k, v = part.split(":", 1)
            st[k.strip()] = v.strip()
    return st


def norm_color(v):
    if v is None:
        return None
    v = v.strip().lower()
    if v in CSS:
        return CSS[v]
    if v.startswith("url(") or v == "currentcolor":
        return None
    if re.match(r"#[0-9a-f]{3}$", v):
        return "#" + "".join(ch * 2 for ch in v[1:])
    if re.match(r"#[0-9a-f]{6}$", v):
        return v
    m = re.match(r"rgb\(([^)]*)\)", v)
    if m:
        p = [x.strip() for x in m.group(1).split(",")]
        vals = [round(float(x[:-1]) * 2.55) if x.endswith("%") else int(float(x))
                for x in p[:3]]
        return "#%02x%02x%02x" % tuple(vals)
    return None


def stop_props(ch):
    props = {}
    for part in (ch.get("style") or "").split(";"):
        if ":" in part:
            k, v = part.split(":", 1)
            props[k.strip()] = v.strip()
    for k in ("stop-color", "stop-opacity"):
        if ch.get(k) is not None:
            props[k] = ch.get(k)
    return props


def gradient_colors(root):
    grads = {}
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        if tag in ("linearGradient", "radialGradient"):
            stops = []
            for ch in el:
                if ch.tag.split("}")[-1] == "stop":
                    p = stop_props(ch)
                    c = norm_color(p.get("stop-color", "#000000"))
                    try:
                        a = float(p.get("stop-opacity", "1"))
                    except ValueError:
                        a = 1.0
                    if c:
                        stops.append((c, a))
            href = el.get("{http://www.w3.org/1999/xlink}href") or el.get("href")
            grads[el.get("id")] = (stops, href[1:] if href else None)
    out = {}
    for gid in grads:
        stops, href = grads[gid]
        seen = set()
        while not stops and href and href not in seen and href in grads:
            seen.add(href)
            stops, href = grads[href]
        if stops:
            n = len(stops)
            alpha = sum(a for _, a in stops) / n
            tot = sum(a for _, a in stops) or 1.0
            r = int(sum(int(c[1:3], 16) * a for c, a in stops) / tot)
            g = int(sum(int(c[3:5], 16) * a for c, a in stops) / tot)
            b = int(sum(int(c[5:7], 16) * a for c, a in stops) / tot)
            if min(a for _, a in stops) != max(a for _, a in stops):
                alpha = 0.0
            out[gid] = ("#%02x%02x%02x" % (r, g, b), alpha)
    return out


class Conv:
    def __init__(self, colors, steps, warn):
        self.grad = {}
        self.colors = colors
        self.names = {v: k for k, v in colors.items()}
        self.steps = steps
        self.shapes = []
        self.warn = warn

    def resolve(self, v):
        c = norm_color(v)
        if c is None and v and v.strip().startswith("url("):
            gid = re.match(r"url\(\s*#([^)\s]+)", v.strip())
            gid = gid.group(1) if gid else None
            c = None
            if gid in self.grad:
                c, alpha = self.grad[gid]
                if alpha < 0.5:
                    self.warn("gradient #%s fades or is transparent (a shading "
                              "overlay); shape skipped" % gid)
                    return None
                self.warn("gradient #%s flattened to its average colour %s"
                          % (gid, c))
            else:
                self.warn("fill %s not supported; shape skipped" % v)
        return c

    def color_ref(self, hexv):
        if hexv in self.names:
            return self.names[hexv]
        r, g, b = (int(hexv[i:i + 2], 16) for i in (1, 3, 5))
        luma = int(round(0.299 * r + 0.587 * g + 0.114 * b))
        self.warn("colour %s has no FTI name; using color(-%d) (grey-ramp "
                  "guess, unverified)" % (hexv, luma))
        return str(-luma)

    def walk(self, el, m, inherited):
        tag = el.tag.split("}")[-1]
        st = dict(inherited)
        st.update(parse_style(el))
        if st.get("display") == "none":
            return
        m = mul(m, parse_transform(el.get("transform")))
        if tag in ("g", "svg", "a", "switch"):
            for ch in el:
                self.walk(ch, m, st)
            return
        subs = []
        f = lambda k, d=0.0: float(NUM.findall(el.get(k, str(d)))[0])
        if tag == "rect":
            x, y, w, h = f("x"), f("y"), f("width"), f("height")
            subs = [([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)]
        elif tag == "circle":
            subs = [(ellipse_pts(f("cx"), f("cy"), f("r"), f("r"), self.steps * 4),
                     True)]
        elif tag == "ellipse":
            subs = [(ellipse_pts(f("cx"), f("cy"), f("rx"), f("ry"), self.steps * 4),
                     True)]
        elif tag in ("polygon", "polyline"):
            v = [float(x) for x in NUM.findall(el.get("points", ""))]
            pts = list(zip(v[0::2], v[1::2]))
            subs = [(pts, tag == "polygon")]
        elif tag == "line":
            subs = [([(f("x1"), f("y1")), (f("x2"), f("y2"))], False)]
        elif tag == "path":
            subs = path_subpaths(el.get("d", ""), self.steps)
        else:
            return
        try:
            op = float(st.get("opacity", "1")) * float(st.get("fill-opacity", "1"))
        except ValueError:
            op = 1.0
        if op < 0.5:
            if op > 0.0:
                self.warn("%s has opacity %.2f; shape skipped (the format has "
                          "no transparency)" % (el.get("id") or tag, op))
            return
        fill = self.resolve(st.get("fill", "#000000"))
        stroke = self.resolve(st.get("stroke"))
        for pts, closed in subs:
            pts = [apply(m, x, y) for x, y in pts]
            if len(pts) >= 2 and pts[0] == pts[-1]:
                pts = pts[:-1]
            if len(pts) < 2:
                continue
            self.shapes.append((el.get("id") or tag, pts, closed, fill, stroke))

    def emit(self, root):
        vb = [float(x) for x in NUM.findall(root.get("viewBox", ""))]
        if len(vb) != 4:
            w = float(NUM.findall(root.get("width", "100"))[0])
            h = float(NUM.findall(root.get("height", "100"))[0])
            vb = [0, 0, w, h]
        self.grad = gradient_colors(root)
        if any(e.tag.split("}")[-1] in ("mask", "clipPath", "filter")
               for e in root.iter()):
            self.warn("mask/clipPath/filter are ignored; flatten them to "
                      "plain paths in Inkscape (Path > Intersection/Difference)")
        self.walk(root, ident(), {})
        s = 100.0 / max(vb[2], vb[3])
        ox = (100.0 - vb[2] * s) / 2
        oy = (100.0 - vb[3] * s) / 2
        out = []
        for name, pts, closed, fill, stroke in self.shapes:
            q = [((x - vb[0]) * s + ox, 100.0 - ((y - vb[1]) * s + oy))
                 for x, y in pts]
            out.append("#\t%s" % name)
            if closed and len(q) >= 3 and fill:
                col = self.color_ref(fill)
                out.append("\t\tcolor(%s);" % col)
                if stroke:
                    out.append("\t\tbgnoutlinepolygon();")
                else:
                    out.append("\t\tbgnpolygon();")
                out.extend("\t\t\tvertex(%.2f, %.2f);" % p for p in q)
                if stroke:
                    out.append("\t\tendoutlinepolygon(%s);"
                               % self.color_ref(stroke))
                else:
                    out.append("\t\tendpolygon();")
            elif stroke:
                out.append("\t\tcolor(%s);" % self.color_ref(stroke))
                out.append("\t\tbgn%sline();" % ("closed" if closed else ""))
                out.extend("\t\t\tvertex(%.2f, %.2f);" % p for p in q)
                out.append("\t\tend%sline();" % ("closed" if closed else ""))
            out.append("")
        return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(
        description="Convert a flat SVG to an IRIX .fti icon. Circles, "
                    "ellipses, rectangles, curves and arcs are flattened to "
                    "polygons; transforms are applied; the viewBox is fitted "
                    "to the 0..100 icon grid (y up).")
    ap.add_argument("svg")
    ap.add_argument("fti", nargs="?")
    ap.add_argument("--steps", type=int, default=12,
                    help="segments per curve or per circle quadrant (default 12)")
    ap.add_argument("--color", action="append", default=[],
                    help="name=#rrggbb: fills of this colour become color(name)")
    a = ap.parse_args()
    colors = dict(NAMED)
    for c in a.color:
        k, v = c.split("=", 1)
        colors[k] = v.lower()
    seen = set()

    def warn(msg):
        if msg not in seen:
            seen.add(msg)
            sys.stderr.write("svg2fti: %s\n" % msg)

    root = ET.parse(a.svg).getroot()
    text = Conv(colors, a.steps, warn).emit(root)
    if a.fti:
        open(a.fti, "w").write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
