/*
 * SgDial reconstruction.
 *
 * Class layout and resources are taken from the original SGI libSgm.so.1
 * (see DIAL_RE.md): superclass xmPrimitive, class name "StdDial",
 * resources minimum/maximum/value/startAngle/angleRange/dialMarkers/
 * markerLength/indicatorColor/dialForeground plus two callbacks, and the
 * actions select/release/enter/leave.
 *
 * The drawing is reconstructed from pixel measurements of the original
 * widget running in the IRIX runtime:
 *   bevel           highlight + shadow thickness wide, flush with the window
 *   knob diameter   0.75 * size - 20, centred
 *   markers         dialMarkers red bars, radial, on a ring around the
 *                   middle; each a 2x6 body with a 1 px light copy up and
 *                   left and a 1 px dark copy down and right
 *   knob            1 px black outline, dithered light crescent upper left,
 *                   dithered dark crescent lower right
 *   indicator       3 px black bar from the rim inward, T-shaped light tip
 *   value 0 is at 12 o'clock and the value runs clockwise.
 */
#include <Sgm/Dial.h>

#include <Xm/PrimitiveP.h>
#include <Xm/Xm.h>
#include <Xm/Scale.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef XmInheritBorderHighlight
#define XmInheritBorderHighlight ((XtWidgetProc)_XtInherit)
#endif
#ifndef XmInheritBorderUnhighlight
#define XmInheritBorderUnhighlight ((XtWidgetProc)_XtInherit)
#endif

#define DIAL_PI 3.14159265358979323846

typedef struct {
	Pixel indicator_color; /* +0xb8 */
	Pixel dial_foreground; /* +0xbc */
	int minimum;	       /* +0xc0 */
	int maximum;	       /* +0xc4 */
	int start_angle;       /* +0xc8 */
	int angle_range;       /* +0xcc */
	int markers;	       /* +0xd0 */
	short marker_length;   /* +0xd4 */
	short value;	       /* +0xd6 */
	XtCallbackList drag_callback;
	XtCallbackList value_changed_callback;
	Boolean dragging;
	Boolean hover; /* the pointer is in the widget: "armed" colours */
	double theta; /* the angle the bar was last drawn at, in radians (+0x6a0) */
	short press_value; /* the value when the button went down (+0x698) */
	GC gc;
	Pixel light_pixel, dark_pixel,
		tip_pixel; /* shaded knob colours, allocated from the palette */
	Boolean colors_valid;
} SgDialPartCompat;

typedef struct _SgDialRec {
	CorePart core;
	XmPrimitivePart primitive;
	SgDialPartCompat dial;
} SgDialRecCompat;

typedef struct {
	XtPointer extension;
} SgDialClassPartCompat;

typedef struct _SgDialClassRec {
	CoreClassPart core_class;
	XmPrimitiveClassPart primitive_class;
	SgDialClassPartCompat dial_class;
} SgDialClassRecCompat;

static void dialInitialize(Widget, Widget, ArgList, Cardinal *);
static void dialDestroy(Widget);
static void dialResize(Widget);
static void dialExpose(Widget, XEvent *, Region);
static Boolean dialSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void actionSelect(Widget, XEvent *, String *, Cardinal *);
static void actionRelease(Widget, XEvent *, String *, Cardinal *);
static void actionEnter(Widget, XEvent *, String *, Cardinal *);
static void actionLeave(Widget, XEvent *, String *, Cardinal *);

#define NAME_MARKERS "dialMarkers"
#define NAME_START "startAngle"
#define NAME_RANGE "angleRange"
#define NAME_INDICATOR "indicatorColor"
#define NAME_MARKLEN "markerLength"
#define NAME_DIALFG "dialForeground"

static XtResource dialResources[] = {
	{ NAME_MARKERS, "Markers", XmRInt, sizeof(int),
	  XtOffsetOf(SgDialRecCompat, dial.markers), XmRImmediate,
	  (XtPointer)16 },
	{ XmNminimum, XmCMinimum, XmRInt, sizeof(int),
	  XtOffsetOf(SgDialRecCompat, dial.minimum), XmRImmediate,
	  (XtPointer)0 },
	{ XmNmaximum, XmCMaximum, XmRInt, sizeof(int),
	  XtOffsetOf(SgDialRecCompat, dial.maximum), XmRImmediate,
	  (XtPointer)360 },
	{ NAME_START, "StartAngle", XmRInt, sizeof(int),
	  XtOffsetOf(SgDialRecCompat, dial.start_angle), XmRImmediate,
	  (XtPointer)0 },
	{ NAME_RANGE, "AngleRange", XmRInt, sizeof(int),
	  XtOffsetOf(SgDialRecCompat, dial.angle_range), XmRImmediate,
	  (XtPointer)360 },
	{ NAME_INDICATOR, XmCForeground, XmRPixel, sizeof(Pixel),
	  XtOffsetOf(SgDialRecCompat, dial.indicator_color), XmRString,
	  (XtPointer) "Red" },
	{ XmNvalue, XmCValue, XmRShort, sizeof(short),
	  XtOffsetOf(SgDialRecCompat, dial.value), XmRImmediate, (XtPointer)0 },
	{ NAME_MARKLEN, "MarkerLength", XmRShort, sizeof(short),
	  XtOffsetOf(SgDialRecCompat, dial.marker_length), XmRImmediate,
	  (XtPointer)8 },
	{ NAME_DIALFG, XmCForeground, XmRPixel, sizeof(Pixel),
	  XtOffsetOf(SgDialRecCompat, dial.dial_foreground), XmRString,
	  (XtPointer) "red" },
	{ XmNdragCallback, XmCCallback, XmRCallback, sizeof(XtCallbackList),
	  XtOffsetOf(SgDialRecCompat, dial.drag_callback), XmRCallback,
	  (XtPointer)NULL },
	{ XmNvalueChangedCallback, XmCCallback, XmRCallback,
	  sizeof(XtCallbackList),
	  XtOffsetOf(SgDialRecCompat, dial.value_changed_callback), XmRCallback,
	  (XtPointer)NULL }
};

static XtActionsRec dialActions[] = { { "select", actionSelect },
				      { "release", actionRelease },
				      { "enter", actionEnter },
				      { "leave", actionLeave } };

static char dialTranslations[] = "<BtnMotion>:\tselect()\n"
				 " <Btn1Down>:\tselect()\n"
				 " <Btn1Up>:\trelease()\n"
				 " <Enter>:\tenter()\n"
				 " <Leave>:\tleave()";

SgDialClassRecCompat SgdialClassRec = {
	{ (WidgetClass)&xmPrimitiveClassRec,
	  "StdDial",
	  sizeof(SgDialRecCompat),
	  NULL,
	  NULL,
	  False,
	  dialInitialize,
	  NULL,
	  XtInheritRealize,
	  dialActions,
	  XtNumber(dialActions),
	  dialResources,
	  XtNumber(dialResources),
	  NULLQUARK,
	  True,
	  XtExposeCompressMultiple,
	  True,
	  False,
	  dialDestroy,
	  dialResize,
	  dialExpose,
	  dialSetValues,
	  NULL,
	  XtInheritSetValuesAlmost,
	  NULL,
	  NULL,
	  XtVersion,
	  NULL,
	  dialTranslations,
	  NULL,
	  NULL,
	  NULL },
	{ XmInheritBorderHighlight, XmInheritBorderUnhighlight,
	  XtInheritTranslations, NULL, NULL, 0, NULL },
	{ NULL }
};

WidgetClass sgDialWidgetClass = (WidgetClass)&SgdialClassRec;

/* ---- helpers -------------------------------------------------------------------------------------------------- */
static void
validate(SgDialRecCompat *d)
{
	/* the checks and fixes of the original's initialize */
	if (!(d->dial.minimum < d->dial.maximum))
		d->dial.minimum = d->dial.maximum - 1;
	if (!(d->dial.start_angle < d->dial.angle_range))
		d->dial.start_angle = d->dial.angle_range - 1;
	if (d->dial.maximum < d->dial.value)
		d->dial.value = (short)d->dial.maximum;
	if (d->dial.value < d->dial.minimum)
		d->dial.value = (short)d->dial.minimum;
	if (d->dial.markers < 2)
		d->dial.markers = 4;
}

/* blend in 8 bits and truncate: a + (b - a) * t, which is how the original's shades come out (see DIAL_RE.md) */
static Pixel
blend_pixel(Widget w, Pixel from, Pixel to, double t)
{
	XColor a, b, c;
	Display *dpy = XtDisplay(w);
	Colormap cmap = w->core.colormap;
	a.pixel = from;
	b.pixel = to;
	XQueryColor(dpy, cmap, &a);
	XQueryColor(dpy, cmap, &b);
	c.red = (unsigned short)(((int)floor((a.red >> 8) +
					     ((int)(b.red >> 8) -
					      (int)(a.red >> 8)) *
						     t)) *
				 257);
	c.green = (unsigned short)(((int)floor((a.green >> 8) +
					       ((int)(b.green >> 8) -
						(int)(a.green >> 8)) *
						       t)) *
				   257);
	c.blue = (unsigned short)(((int)floor((a.blue >> 8) +
					      ((int)(b.blue >> 8) -
					       (int)(a.blue >> 8)) *
						      t)) *
				  257);
	c.flags = DoRed | DoGreen | DoBlue;
	if (!XAllocColor(dpy, cmap, &c))
		return from;
	return c.pixel;
}

static void
ensure_colors(SgDialRecCompat *d)
{
	Widget w = (Widget)d;
	XColor white;
	if (d->dial.colors_valid)
		return;
	d->dial.light_pixel = blend_pixel(w, d->core.background_pixel,
					  d->primitive.top_shadow_color, 0.87);
	d->dial.dark_pixel =
		blend_pixel(w, d->core.background_pixel,
			    d->primitive.bottom_shadow_color, 0.545);
	white.red = white.green = white.blue = 65535;
	white.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(XtDisplay(w), w->core.colormap, &white))
		d->dial.tip_pixel = blend_pixel(w, d->core.background_pixel,
						white.pixel, 0.705);
	else
		d->dial.tip_pixel = d->primitive.top_shadow_color;
	d->dial.colors_valid = True;
}

static void
callout(SgDialRecCompat *d, XtCallbackList list, int reason, XEvent *ev)
{
	XmScaleCallbackStruct cb;
	if (!list)
		return;
	cb.reason = reason;
	cb.event = ev;
	cb.value = d->dial.value;
	XtCallCallbackList((Widget)d, list, &cb);
}

/* ---- class methods ---------------------------------------------------------------------------------------------- */
static void
dialInitialize(Widget request, Widget newWidget, ArgList args, Cardinal *count)
{
	SgDialRecCompat *d = (SgDialRecCompat *)newWidget;
	XGCValues v;
	(void)request;
	(void)args;
	(void)count;
	validate(d);
	if (d->core.width == 0)
		d->core.width = 100;
	if (d->core.height == 0)
		d->core.height = 100;
	v.foreground = d->primitive.foreground;
	v.background = d->core.background_pixel;
	d->dial.gc = XtGetGC(newWidget, GCForeground | GCBackground, &v);
	d->dial.dragging = False;
	d->dial.hover = False;
	d->dial.press_value = d->dial.value;
	d->dial.theta = 0.0;
	d->dial.colors_valid = False;
	d->primitive.highlight_on_enter =
		False; /* the enter and leave actions draw their own ring */
}

static void
dialDestroy(Widget w)
{
	SgDialRecCompat *d = (SgDialRecCompat *)w;
	if (d->dial.gc)
		XtReleaseGC(w, d->dial.gc);
	d->dial.gc = 0;
}

static void
setfg(SgDialRecCompat *d, Pixel p)
{
	XSetForeground(XtDisplay((Widget)d), d->dial.gc, p);
}

/*
 * Exact drawing.  The sequence of X requests, their coordinates and the rounding follow the original widget, as
 * recorded with an X protocol trace (see DIAL_RE.md): knob arcs, indicator bar and tip, the markers, then the bevel.
 * Single precision products and rint() are used where the original uses them, so build with SSE math.
 */
#define TWO_PI 6.283185307179586

typedef struct {
	int width, height, cx, cy, s, e0, e2, m, n, x0, y0, mlen;
} DialGeo;

static void
geometry(SgDialRecCompat *d, DialGeo *g)
{
	int shadow = d->primitive.shadow_thickness;
	g->width = d->core.width;
	g->height = d->core.height;
	g->cx = g->width / 2;
	g->cy = g->height / 2;
	g->s = g->width < g->height ? g->width : g->height;
	g->mlen = (unsigned short)d->dial.marker_length;
	g->e2 = g->s / 2 - g->mlen / 2 - shadow;
	g->e0 = g->e2 - g->mlen;
	g->m = (short)(int)((double)(short)g->e0 *
			    1.5); /* the knob outline size, also the indicator offset */
	g->n = (g->m / 10 < 5) ? g->m / 10 + 1 : 5; /* shading rings */
	g->x0 = g->cx - g->m / 2;
	g->y0 = g->cy - g->m / 2;
}

static double
value_theta(const SgDialRecCompat *d)
{
	float frac = (float)(d->dial.value - d->dial.minimum) /
		     (float)(d->dial.maximum - d->dial.minimum);
	return ((double)d->dial.start_angle +
		(double)d->dial.angle_range * (double)frac) *
	       TWO_PI / 360.0;
}

static void
arc(SgDialRecCompat *d, Pixel color, int x, int y, int w, int h, int a1, int a2)
{
	setfg(d, color);
	XDrawArc(XtDisplay((Widget)d), XtWindow((Widget)d), d->dial.gc, x, y,
		 (unsigned)w, (unsigned)h, a1, a2);
}

/* rotate base points about the dial centre as the original's helper does: sin/cos in single precision, then rint */
static void
rotate_points(const short *in, XPoint *out, int n, int cx, int cy, int m,
	      double theta)
{
	float s = (float)sin(theta), c = (float)cos(theta);
	double half = 0.5 * (double)m;
	int i;
	for (i = 0; i < n; i++) {
		float xx = (float)((double)in[2 * i] + half);
		float yy = (float)in[2 * i + 1];
		float px = s * xx;
		float py = c * yy;
		float qx = s * yy;
		float qy = c * xx;
		float X = px - py;
		float Y = qx + qy;
		out[i].x = (short)((int)rint((double)X) + cx);
		out[i].y = (short)((int)rint((double)(-Y)) + cy);
	}
}

/* the bar polygon at an angle (5 points), erased or drawn */
static void
bar_poly(const DialGeo *g, double theta, XPoint *poly)
{
	int kk = (g->m / 2 < 7) ? g->m / 2 : 7;
	short bar[10] = {
		(short)(-1 - kk), -1, (short)(-1 - kk), 1, -1, 1, -1, -1,
		(short)(-1 - kk), -1
	};
	rotate_points(bar, poly, 5, g->cx, g->cy, g->m, theta);
}

static void
draw_bar(SgDialRecCompat *d, const DialGeo *g, double theta, Pixel black)
{
	Display *dpy = XtDisplay((Widget)d);
	Window win = XtWindow((Widget)d);
	XPoint poly[6];
	static const short tip[12] = { -11, 0, -10, -1, -9,  -1,
				       -9,  1, -10, 1,	-10, 0 };
	bar_poly(g, theta, poly);
	setfg(d, black);
	XFillPolygon(dpy, win, d->dial.gc, poly, 5, Convex, CoordModeOrigin);
	XDrawLines(dpy, win, d->dial.gc, poly, 5, CoordModeOrigin);
	rotate_points(tip, poly, 6, g->cx, g->cy, g->m, theta);
	setfg(d, d->dial.tip_pixel);
	XDrawLines(dpy, win, d->dial.gc, poly, 6, CoordModeOrigin);
	XFillPolygon(dpy, win, d->dial.gc, poly, 6, Convex, CoordModeOrigin);
}

/*
 * The knob: outline, shading rings, interior.  Armed (pointer inside) uses lighter rings and no dark side.  With
 * erase set the old bar is wiped with the interior colour between the outline and the rings, which is how a drag
 * redraws only the knob.
 */
static void
draw_knob(SgDialRecCompat *d, const DialGeo *g, Pixel black, Pixel bg,
	  int erase, double old_theta)
{
	Display *dpy = XtDisplay((Widget)d);
	Window win = XtWindow((Widget)d);
	int armed = d->dial.hover || d->dial.dragging, k;
	Pixel interior = armed ? d->dial.light_pixel : bg;
	arc(d, black, g->x0, g->y0, g->m, g->m, 14720, 10880);
	arc(d, black, g->x0, g->y0, g->m, g->m, 2560, 12160);
	if (erase) {
		XPoint poly[5];
		bar_poly(g, old_theta, poly);
		setfg(d, interior);
		XFillPolygon(dpy, win, d->dial.gc, poly, 5, Convex,
			     CoordModeOrigin);
		XDrawLines(dpy, win, d->dial.gc, poly, 5, CoordModeOrigin);
	}
	for (k = 1; k <= g->n; k++) {
		int bx = g->x0 + k, by = g->y0 + k, sz = g->m - 2 * k;
		Pixel lit, trans, dark;
		if (armed) {
			lit = (k < g->n) ? d->dial.tip_pixel
					 : d->dial.light_pixel;
			trans = d->dial.light_pixel;
			dark = bg;
		} else {
			lit = (k == 1)	    ? d->dial.tip_pixel
			      : (k == g->n) ? bg
					    : d->dial.light_pixel;
			trans = bg;
			dark = d->dial.dark_pixel;
		}
		arc(d, lit, bx, by, sz, sz, 3200 + 64 * (k - 1),
		    10880 - 128 * (k - 1));
		arc(d, trans, bx, by, sz, sz, 2560 - 64 * (k - 1),
		    640 + 128 * (k - 1));
		arc(d, trans, bx, by, sz, sz, 14080 - 64 * (k - 1),
		    640 + 128 * (k - 1));
		arc(d, dark, bx, by, sz, sz, 14720 + 64 * (k - 1),
		    10880 - 128 * (k - 1));
	}
	setfg(d, interior);
	XFillArc(dpy, win, d->dial.gc, g->x0 + g->n, g->y0 + g->n,
		 (unsigned)(g->m - 2 * g->n), (unsigned)(g->m - 2 * g->n), 0,
		 360 * 64);
}

typedef struct {
	int edge;
	int lit;
} EdgeStep;

static void
draw_marker_edges(SgDialRecCompat *d, Pixel light, Pixel dark, const XPoint *p,
		  double deg)
{
	Display *dpy = XtDisplay((Widget)d);
	Window win = XtWindow((Widget)d);
	XPoint seg[2];
	/* which of the four edges are lit depends on the sector the marker is in; (edge, colour) in drawing order */
	static const EdgeStep top[4] = {
		{ 1, 1 }, { 0, 0 }, { 3, 0 }, { 2, 1 }
	};
	static const EdgeStep right[4] = {
		{ 0, 0 }, { 1, 0 }, { 2, 1 }, { 3, 1 }
	};
	static const EdgeStep bottom[4] = {
		{ 0, 1 }, { 1, 0 }, { 2, 0 }, { 3, 1 }
	};
	static const EdgeStep left[4] = {
		{ 0, 1 }, { 1, 1 }, { 2, 0 }, { 3, 0 }
	};
	const EdgeStep *order;
	int i;
	if (deg <= 44.0 || deg >= 316.0)
		order = top;
	else if (deg >= 45.0 && deg <= 134.0)
		order = right;
	else if (deg >= 247.0 && deg < 316.0)
		order = left;
	else
		order = bottom;
	for (i = 0; i < 4; i++) {
		seg[0] = p[order[i].edge];
		seg[1] = p[(order[i].edge + 1) & 3];
		setfg(d, order[i].lit ? light : dark);
		XDrawLines(dpy, win, d->dial.gc, seg, 2, CoordModeOrigin);
	}
}

static void
draw_markers(SgDialRecCompat *d, const DialGeo *g)
{
	Display *dpy = XtDisplay((Widget)d);
	Window win = XtWindow((Widget)d);
	double start_rad = (double)(float)d->dial.start_angle * TWO_PI / 360.0;
	double step = (double)(float)d->dial.angle_range * TWO_PI / 360.0 /
		      (double)(float)(d->dial.markers - 1);
	double ang = start_rad;
	int i;
	/* the marker GCs carry line_width 1 in the original (the wide line algorithm), the others are thin lines */
	XSetLineAttributes(dpy, d->dial.gc, 1, LineSolid, CapButt, JoinMiter);
	for (i = 0; i < d->dial.markers; i++) {
		double sn = sin(ang), cs = cos(ang),
		       wsc = (double)g->mlen / 6.0;
		int Cx = (int)(g->cx + g->e2 * sn),
		    Cy = (int)(g->cy - g->e2 * cs);
		int Dx = (int)(g->cx + g->e0 * sn),
		    Dy = (int)(g->cy - g->e0 * cs);
		double s1 = sin(ang + 90.0) * wsc, c1 = cos(ang + 90.0) * wsc;
		double s2 = sin(ang - 90.0) * wsc, c2 = cos(ang - 90.0) * wsc;
		XPoint p[5];
		p[0].x = (short)(int)(Dx - s2);
		p[0].y = (short)(int)(Dy + c2);
		p[1].x = (short)(int)(Cx + s1);
		p[1].y = (short)(int)(Cy - c1);
		p[2].x = (short)(int)(Cx + s2);
		p[2].y = (short)(int)(Cy - c2);
		p[3].x = (short)(int)(Dx - s1);
		p[3].y = (short)(int)(Dy + c1);
		p[4] = p[0];
		setfg(d, d->dial.dial_foreground);
		XFillPolygon(dpy, win, d->dial.gc, p, 5, Convex,
			     CoordModeOrigin);
		draw_marker_edges(d, d->primitive.top_shadow_color,
				  d->primitive.bottom_shadow_color, p,
				  ang / TWO_PI * 360.0);
		ang = step + ang;
	}
	XSetLineAttributes(dpy, d->dial.gc, 0, LineSolid, CapButt, JoinMiter);
}

static void
draw_bevel(SgDialRecCompat *d, const DialGeo *g)
{
	Display *dpy = XtDisplay((Widget)d);
	Window win = XtWindow((Widget)d);
	int bt = d->primitive.highlight_thickness +
		 d->primitive.shadow_thickness,
	    i;
	int width = g->width, height = g->height;
	XRectangle top[8], bot[8];
	if (bt > 4)
		bt = 4;
	/* row by row over the whole highlight plus shadow width */
	for (i = 0; i < bt; i++) {
		top[i].x = 0;
		top[i].y = (short)i;
		top[i].width = (unsigned short)(width - i);
		top[i].height = 1;
		top[bt + i].x = (short)i;
		top[bt + i].y = 0;
		top[bt + i].width = 1;
		top[bt + i].height = (unsigned short)(height - i);
		bot[i].x = (short)(1 + i);
		bot[i].y = (short)(height - 1 - i);
		bot[i].width = (unsigned short)(width - 1 - i);
		bot[i].height = 1;
		bot[bt + i].x = (short)(width - 1 - i);
		bot[bt + i].y = (short)(1 + i);
		bot[bt + i].width = 1;
		bot[bt + i].height = (unsigned short)(height - 1 - i);
	}
	XFillRectangles(dpy, win, d->primitive.top_shadow_GC, top, 2 * bt);
	XFillRectangles(dpy, win, d->primitive.bottom_shadow_GC, bot, 2 * bt);
}

/* a full redraw: knob, bar, markers, bevel.  The window is not cleared: expose and SetValues already did that. */
static void
drawDial(SgDialRecCompat *d)
{
	Widget w = (Widget)d;
	Display *dpy;
	DialGeo g;
	Pixel black, bg = d->core.background_pixel;
	if (!XtIsRealized(w) || !d->dial.gc)
		return;
	dpy = XtDisplay(w);
	ensure_colors(d);
	black = BlackPixelOfScreen(XtScreen(w));
	XSetLineAttributes(dpy, d->dial.gc, 0, LineSolid, CapButt, JoinMiter);
	geometry(d, &g);
	draw_knob(d, &g, black, bg, 0, 0.0);
	d->dial.theta = value_theta(d);
	draw_bar(d, &g, d->dial.theta, black);
	draw_markers(d, &g);
	draw_bevel(d, &g);
}

/* the partial redraw of a drag: the knob and the bar only, the old bar wiped out first */
static void
redrawKnob(SgDialRecCompat *d)
{
	Widget w = (Widget)d;
	DialGeo g;
	Pixel black, bg = d->core.background_pixel;
	if (!XtIsRealized(w) || !d->dial.gc)
		return;
	ensure_colors(d);
	black = BlackPixelOfScreen(XtScreen(w));
	XSetLineAttributes(XtDisplay(w), d->dial.gc, 0, LineSolid, CapButt,
			   JoinMiter);
	geometry(d, &g);
	draw_knob(d, &g, black, bg, 1, d->dial.theta);
	d->dial.theta = value_theta(d);
	draw_bar(d, &g, d->dial.theta, black);
}

/* the geometry is recomputed on the fly, so a resize only waits for the expose that follows */
static void
dialResize(Widget w)
{
	(void)w;
}

static void
dialExpose(Widget w, XEvent *ev, Region region)
{
	(void)ev;
	(void)region;
	drawDial((SgDialRecCompat *)w);
}

static Boolean
dialSetValues(Widget oldW, Widget reqW, Widget newW, ArgList args,
	      Cardinal *count)
{
	SgDialRecCompat *o = (SgDialRecCompat *)oldW,
			*n = (SgDialRecCompat *)newW;
	Boolean full;
	(void)reqW;
	(void)args;
	(void)count;
	if (!(n->dial.minimum < n->dial.maximum))
		n->dial.minimum = n->dial.maximum - 1;
	if (!(n->dial.start_angle < n->dial.angle_range))
		n->dial.start_angle = n->dial.angle_range - 1;
	if (n->dial.maximum < n->dial.value)
		n->dial.value = (short)n->dial.maximum;
	if (n->dial.value < n->dial.minimum)
		n->dial.value = (short)n->dial.minimum;
	if (n->dial.markers != o->dial.markers) {
		int lim = (n->dial.angle_range - n->dial.start_angle) / 2;
		if (n->dial.markers >= lim)
			n->dial.markers = lim;
		if (n->dial.markers < 2)
			n->dial.markers = 4;
		if (n->dial.markers >= 180)
			n->dial.markers = 180;
		if (n->dial.markers < 2)
			n->dial.markers = 4;
	}
	if (n->dial.marker_length != o->dial.marker_length) {
		DialGeo g;
		int limit;
		geometry(o, &g);
		limit = g.e2 / 2;
		if ((unsigned short)n->dial.marker_length > limit)
			n->dial.marker_length = (short)limit;
	}
	full = o->dial.minimum != n->dial.minimum ||
	       o->dial.maximum != n->dial.maximum ||
	       o->dial.start_angle != n->dial.start_angle ||
	       o->dial.angle_range != n->dial.angle_range ||
	       o->dial.markers != n->dial.markers ||
	       o->dial.marker_length != n->dial.marker_length ||
	       o->dial.indicator_color != n->dial.indicator_color ||
	       o->dial.dial_foreground != n->dial.dial_foreground;
	if (!full && o->dial.value != n->dial.value) {
		/* only the value moved: the knob and the bar are redrawn in place, nothing else */
		n->dial.theta = o->dial.theta;
		redrawKnob(n);
		return False;
	}
	return full;
}

/* ---- pointer ---------------------------------------------------------------------------------------------------- */
/* the value under a pointer position, exactly as the original maps it */
static short
pointer_value(SgDialRecCompat *d, int x, int y)
{
	int cx = d->core.width / 2, cy = d->core.height / 2;
	double deg = atan2((double)(y - cy), (double)(x - cx)) / TWO_PI * 360.0;
	int old = d->dial.value;
	deg -= (double)(d->dial.start_angle - 90);
	if (deg < 0.0)
		deg += 360.0;
	if ((double)d->dial.angle_range <
	    deg) /* outside the travel: the end nearer to where the knob is */
		return (short)(((d->dial.maximum - old) <
				(old - d->dial.minimum))
				       ? d->dial.maximum
				       : d->dial.minimum);
	return (short)(int)((double)d->dial.minimum +
			    (double)(d->dial.maximum - d->dial.minimum) *
				    (deg / (double)d->dial.angle_range));
}

static void
actionSelect(Widget w, XEvent *ev, String *params, Cardinal *np)
{
	SgDialRecCompat *d = (SgDialRecCompat *)w;
	short old = d->dial.value, v = old;
	(void)params;
	(void)np;
	if (ev->type == ButtonPress)
		v = pointer_value(d, ev->xbutton.x, ev->xbutton.y);
	else if (ev->type == MotionNotify)
		v = pointer_value(d, ev->xmotion.x, ev->xmotion.y);
	if (ev->type == ButtonPress) {
		d->dial.press_value = old;
		d->dial.dragging = True;
	}
	if (v == old)
		return;
	XtVaSetValues(w, XmNvalue, v, NULL);
	callout(d, d->dial.drag_callback, XmCR_DRAG, ev);
}

static void
actionRelease(Widget w, XEvent *ev, String *params, Cardinal *np)
{
	SgDialRecCompat *d = (SgDialRecCompat *)w;
	short v = d->dial.value;
	(void)params;
	(void)np;
	if (ev->type == ButtonRelease) {
		d->dial.dragging = False;
		v = pointer_value(d, ev->xbutton.x, ev->xbutton.y);
		if (v != d->dial.value)
			XtVaSetValues(w, XmNvalue, v, NULL);
	}
	if (d->dial.press_value != v)
		callout(d, d->dial.value_changed_callback, XmCR_VALUE_CHANGED,
			ev);
}

/* the highlight ring over the outer highlight-thickness pixels, drawn after a full redraw on enter and leave */
static void
highlight_ring(SgDialRecCompat *d, Pixel color)
{
	int hl = d->primitive.highlight_thickness, w = d->core.width,
	    h = d->core.height;
	XRectangle r[4];
	if (hl <= 0 || !XtIsRealized((Widget)d))
		return;
	r[0].x = 0;
	r[0].y = 0;
	r[0].width = (unsigned short)w;
	r[0].height = (unsigned short)hl;
	r[1].x = 0;
	r[1].y = (short)(h - hl);
	r[1].width = (unsigned short)w;
	r[1].height = (unsigned short)hl;
	r[2].x = 0;
	r[2].y = 0;
	r[2].width = (unsigned short)hl;
	r[2].height = (unsigned short)h;
	r[3].x = (short)(w - hl);
	r[3].y = 0;
	r[3].width = (unsigned short)hl;
	r[3].height = (unsigned short)h;
	setfg(d, color);
	XFillRectangles(XtDisplay((Widget)d), XtWindow((Widget)d), d->dial.gc,
			r, 4);
}

static void
actionEnter(Widget w, XEvent *ev, String *params, Cardinal *np)
{
	SgDialRecCompat *d = (SgDialRecCompat *)w;
	(void)ev;
	(void)params;
	(void)np;
	d->dial.hover = True;
	if (!d->dial.dragging) {
		drawDial(d);
		highlight_ring(d, d->primitive.highlight_color);
	}
}

static void
actionLeave(Widget w, XEvent *ev, String *params, Cardinal *np)
{
	SgDialRecCompat *d = (SgDialRecCompat *)w;
	(void)ev;
	(void)params;
	(void)np;
	d->dial.hover = False;
	if (!d->dial.dragging) {
		drawDial(d);
		highlight_ring(d, d->core.background_pixel);
	}
}
