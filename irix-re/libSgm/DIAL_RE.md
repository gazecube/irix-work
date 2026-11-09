# SgDial: recovery from the original libSgm.so.1

Dial.c reproduces the original widget's X requests, one for one. For the dial sizes 64 to 136 and a volume-knob
configuration (start 225, range 270, 0..125) its output is pixel-identical to the original running in the IRIX runtime,
and the request streams of an interaction (enter, press, drag, release, leave) are identical too.  The only thing not
covered is Motif's own focus and highlight ring, whose colour comes from the environment.

## How it was done

1. The original libSgm.so.1 (N32) runs in the IRIX runtime, so the real widget can be driven.  tools/dialtest3.c is an
   N32 program that creates one SgDial (SIZE VALUE [START RANGE MARKERS MAX]).  Build it with
   desktop-integration/n32-toolchain/n32cc.sh (add -I native-libs/build/xm-sgi-inc) and link with a copy of n32link.sh
   that adds --export-dynamic (it defines __rld_obj_head, otherwise rld stops on libC.so.2).  Copy the binary to
   IRIX-TREE/tmp and run it with desktop-integration/irix-run /tmp/dialtest3 under IRIX_EXEC_BACKEND=interp.
2. The guest talks X over its own socket (IRIX_GUEST_DISPLAY=unix/:98), so tools/xproxy.py sits on that socket,
   forwards to the real display and decodes the drawing requests (the guest is big-endian) into a log with timestamps.
   Clip regions arrive as exact rectangle lists.
3. Dial.c is built for 32-bit against the SGI Motif headers and run the same way; both request streams are normalised
   (GC ids replaced by the colour and line width they carry) and diffed.
4. The formulas were read from the MIPS code (llvm-objdump --triple=mips) and checked against the traces.

## Class record SgdialClassRec @ 0x0b251508, class name "StdDial"
superclass xmPrimitive, widget size 0x708, 12 resources, 4 actions (select, release, enter, leave).
Translations: <BtnMotion>: select()  <Btn1Down>: select()  <Btn1Up>: release()  <Enter>: enter()  <Leave>: leave()
Methods: initialize 0x0b1f28ec, expose 0x0b1f2ce4, set_values 0x0b1f2dd4, actions 0x0b1f3358.. (select, release, enter, leave).

Resources (offset, default): dialMarkers (0xd0, 16), minimum (0xc0, 0), maximum (0xc4, 360), startAngle (0xc8, 0),
angleRange (0xcc, 360), indicatorColor (0xb8), value short (0xd6, 0), markerLength short (0xd4, 8),
dialForeground (0xbc, "red"), dragCallback (0x690), valueChangedCallback (0x694), dialVisual (0x6a8).
Default size 100x100.  Initialize: min < max else min = max - 1; start < range else start = range - 1; value clamped.

## Geometry (width/height w,h; s = min(w,h); cx = w/2, cy = h/2; shadow = shadowThickness; hl = highlightThickness)
- e2 = s/2 - markerLength/2 - shadow   (outer marker radius)      e0 = e2 - markerLength   (inner)
- m = (short)(e0 * 1.5)                  knob outline size, also the indicator offset
- rings n = (m/10 < 5) ? m/10 + 1 : 5
- knob box x0 = cx - m/2, y0 = cy - m/2

## Drawing order and requests (a full redraw; the window is not cleared)
1. Knob: black XDrawArc (x0,y0,m,m) a1=14720 a2=10880, then a1=2560 a2=12160.  For ring k = 1..n the box is
   (x0+k, y0+k, m-2k) and four arcs are drawn, each angle in 1/64 degrees:
     light  a1 = 3200 + 64(k-1)   a2 = 10880 - 128(k-1)
     trans1 a1 = 2560 - 64(k-1)   a2 =  640 + 128(k-1)
     trans2 a1 = 14080 - 64(k-1)  a2 =  640 + 128(k-1)
     dark   a1 = 14720 + 64(k-1)  a2 = 10880 - 128(k-1)
   Normal colours: light = lightest (k=1), mid-light (1<k<n), background (k=n); trans = background; dark = dark.
   Armed colours (pointer inside or dragging): light = lightest for k<n and mid-light for k=n; trans = mid-light;
   dark = background.  Then XFillArc of the innermost box (x0+n,y0+n,m-2n) in the interior colour (background, armed:
   mid-light).
   Palette, derived from the background B, top shadow T, bottom shadow D (8 bit, truncated):
   mid-light = B + 0.87 (T-B), lightest = B + 0.705 (white-B), dark = B + 0.545 (D-B).
2. Bar: base points (-1-k,-1) (-1-k,1) (-1,1) (-1,-1) (-1-k,-1) with k = min(7, m/2), tip points (-11,0) (-10,-1)
   (-9,-1) (-9,1) (-10,1) (-10,0).  Each is rotated about (cx,cy): xx = (float)(x + m/2.0), s = (float)sin(theta),
   c = (float)cos(theta), X = rint(s*xx - c*y) + cx, Y = rint(-(s*y + c*xx)) + cy, all single precision.
   theta = (start + range * ((float)(value-min)/(float)(max-min))) * 2pi/360.  Bar: XFillPolygon + XDrawLines in black;
   tip: XDrawLines then XFillPolygon in the lightest colour.
3. Markers: angle_i = start_rad + i * step, step = range_rad/(markers-1) accumulated in double (so a full circle puts
   the first and last marker on top of each other).  Outer (Cx,Cy) = trunc(cx + e2 sin, cy - e2 cos), inner (Dx,Dy)
   likewise with e0.  With w = markerLength/6 and (s1,c1) = w*(sin,cos)(angle + 90.0), (s2,c2) = w*(sin,cos)(angle - 90.0)
   (the 90 is in radians): P0 = (Dx-s2, Dy+c2) P1 = (Cx+s1, Cy-c1) P2 = (Cx+s2, Cy-c2) P3 = (Dx-s1, Dy+c1), truncated.
   Red XFillPolygon of P0 P1 P2 P3 P0, then four 1 px edges (line width 1: these GCs use the wide line algorithm) whose
   light/dark assignment depends on the raw angle in degrees (not wrapped):
     <= 44 or >= 316: P1P2 L, P0P1 D, P3P0 D, P2P3 L     45..134: P0P1 D, P1P2 D, P2P3 L, P3P0 L
     247..315:        P0P1 L, P1P2 L, P2P3 D, P3P0 D     otherwise: P0P1 L, P1P2 D, P2P3 D, P3P0 L
4. Bevel: XFillRectangles in top and bottom shadow, rows (0,i,w-i,1), columns (i,0,1,h-i) light and (1+i,h-1-i,w-1-i,1),
   (w-1-i,1+i,1,h-1-i) dark, for i < highlight+shadow thickness.

## Interaction
- enter: armed; if not dragging a full redraw, then the highlight ring.  leave: not armed; full redraw, then the ring in
  the background colour.
- select (press or motion): value = trunc(min + (max-min) * rel/range) with rel = atan2(y-cy, x-cx) in degrees + 90 - start,
  wrapped once; outside the travel it goes to the end nearer to the current value.  Press remembers the old value and starts
  the drag.  A changed value goes through XtVaSetValues and the drag callback (XmCR_DRAG).
- set_values with only the value changed redraws just the knob and the bar: outline arcs, the old bar wiped in the interior
  colour, rings, interior, new bar, tip.  Anything else is a full redraw.
- release: the value under the pointer, value-changed callback (XmCR_VALUE_CHANGED) if it differs from the value at press.
