/*
 * IRIS GL (libgl.so) subset implemented over Xlib, for N32 guest programs.
 *
 * The original libgl needs SGI graphics hardware (it maps the GL pipe at
 * load).  This replacement draws 2D with core X requests into a window (or a
 * back-buffer pixmap for doublebuffer) and implements the pieces simple
 * IRIS GL programs such as /usr/sbin/clock use: windows, the event queue,
 * single-matrix transforms, polygons/meshes/lines/rects, bitmap text and pup
 * menus.  It is a software stand-in, not a hardware 3D pipeline.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <gl/gl.h>
#include <gl/device.h>

#include <math.h>
/* IRIX's stdarg.h expects MIPSpro builtins; use clang's directly. */
typedef __builtin_va_list iva_list;
#define iva_start(ap, last) __builtin_va_start(ap, last)
#define iva_end(ap) __builtin_va_end(ap)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static int igl_trace = -1;
#define TR(...) do { if (igl_trace < 0) igl_trace = getenv("IGL_TRACE") != NULL; if (igl_trace) { fprintf(stderr, "igl: " __VA_ARGS__); fputc('\n', stderr); } } while (0)
#define MAXV 512
#define QMAX 256

typedef struct { float m[4][4]; } Mat;

static Display *dpy;
static int scr;
static Window win;
static long gid;
static GC gc;
static Pixmap back;
static int dbuf, rgbmode;
static int winw = 256, winh = 256, orgx, orgy;
static int prefw, prefh, aspx, aspy, icw, ich;
static Visual *vis;
static int depth;
static XFontStruct *xfont;
static Atom wm_delete;

static Mat cur, stack[32];
static int sp;
static int vpl, vpb, vpw, vph;       /* viewport in window pixels (origin bottom) */

static unsigned long cur_pixel;
static unsigned long cmap[256];
static long cur_index;
static int charx, chary;

/* polygon / mesh accumulation */
static enum { M_NONE, M_POLY, M_LINE, M_TMESH, M_POINT } mode;
static float vx[MAXV], vy[MAXV];
static int nv;
static float tp0x, tp0y, tp1x, tp1y;
static int tcount, tswap;

/* event queue */
typedef struct { short dev, val; } Ev;
static Ev q[QMAX];
static int qh, qt;
static unsigned char qenabled[1024];
static int timer_ms, timer_en;
static struct timeval timer_next;

static void mat_ident(Mat *a)
{
    int i, j;
    for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) a->m[i][j] = (i == j);
}

/* column-vector convention: cur = cur * t  (t applies to vertices first) */
static void mat_mul_right(Mat *a, const Mat *t)
{
    Mat r;
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j) {
            float s = 0;
            for (k = 0; k < 4; ++k) s += a->m[i][k] * t->m[k][j];
            r.m[i][j] = s;
        }
    *a = r;
}

static void ensure_dpy(void)
{
    if (!dpy) {
        dpy = XOpenDisplay(NULL);
        if (!dpy) { fprintf(stderr, "libgl: cannot open display\n"); exit(1); }
        scr = DefaultScreen(dpy);
        vis = DefaultVisual(dpy, scr);
        depth = DefaultDepth(dpy, scr);
        mat_ident(&cur);
    }
}

static void default_projection(void)
{
    /* IRIS GL default: screen coordinates, origin bottom-left */
    float w = (float)winw, h = (float)winh;
    mat_ident(&cur);
    cur.m[0][0] = 2.0f / w;  cur.m[0][3] = -1.0f;
    cur.m[1][1] = 2.0f / h;  cur.m[1][3] = -1.0f;
    vpl = vpb = 0; vpw = winw; vph = winh;
}

static Drawable target(void)
{
    if (dbuf) {
        if (!back) back = XCreatePixmap(dpy, win, (unsigned)winw, (unsigned)winh, (unsigned)depth);
        return back;
    }
    return win;
}

static unsigned long rgb_pixel(int r, int g, int b)
{
    XColor c;
    c.red = (unsigned short)(r * 257); c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257); c.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, DefaultColormap(dpy, scr), &c)) return c.pixel;
    return BlackPixel(dpy, scr);
}

static void set_fg(unsigned long p)
{
    cur_pixel = p;
    if (gc) XSetForeground(dpy, gc, p);
}

static void project(float x, float y, float z, int *ox, int *oy)
{
    float cx = cur.m[0][0]*x + cur.m[0][1]*y + cur.m[0][2]*z + cur.m[0][3];
    float cy = cur.m[1][0]*x + cur.m[1][1]*y + cur.m[1][2]*z + cur.m[1][3];
    float cw = cur.m[3][0]*x + cur.m[3][1]*y + cur.m[3][2]*z + cur.m[3][3];
    float px, py;
    if (cw != 0 && cw != 1) { cx /= cw; cy /= cw; }
    px = vpl + (cx + 1.0f) * 0.5f * (float)vpw;
    py = vpb + (cy + 1.0f) * 0.5f * (float)vph;
    *ox = (int)floorf(px + 0.5f);
    *oy = winh - 1 - (int)floorf(py + 0.5f);   /* flip: X origin is top-left */
}

/* ---- window ------------------------------------------------------------ */
long winopen(String title)
{
    TR("winopen(%s)", title ? title : "(null)");
    XSetWindowAttributes a;
    XSizeHints h;
    unsigned long black;
    ensure_dpy();
    if (prefw > 0) { winw = prefw; winh = prefh; }
    black = BlackPixel(dpy, scr);
    a.background_pixel = black;
    a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
                   ButtonReleaseMask | KeyPressMask | PointerMotionMask;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), orgx, orgy, (unsigned)winw,
                        (unsigned)winh, 0, depth, InputOutput, vis,
                        CWBackPixel | CWEventMask, &a);
    memset(&h, 0, sizeof(h));
    h.flags = PSize;
    h.width = winw; h.height = winh;
    if (aspx > 0 && aspy > 0) {
        h.flags |= PAspect;
        h.min_aspect.x = h.max_aspect.x = aspx;
        h.min_aspect.y = h.max_aspect.y = aspy;
    }
    XSetWMNormalHints(dpy, win, &h);
    XStoreName(dpy, win, title ? title : "IRIS GL");
    XSetIconName(dpy, win, title ? title : "IRIS GL");
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    gc = XCreateGC(dpy, win, 0, NULL);
    xfont = XLoadQueryFont(dpy, "-*-helvetica-medium-r-normal--12-*");
    if (!xfont) xfont = XLoadQueryFont(dpy, "fixed");
    if (xfont) XSetFont(dpy, gc, xfont->fid);
    set_fg(WhitePixel(dpy, scr));
    default_projection();
    XMapWindow(dpy, win);
    XFlush(dpy);
    gid = (long)(win & 0x7fffffff);
    return gid;
}

void winset(long id) { (void)id; }
long winget(void) { return gid; }
void winclose(long id) { (void)id; if (dpy && win) XDestroyWindow(dpy, win); }
void prefsize(long x, long y) { prefw = (int)x; prefh = (int)y; }
void prefposition(long x1, long x2, long y1, long y2)
{ orgx = (int)x1; prefw = (int)(x2 - x1 + 1); prefh = (int)(y2 - y1 + 1); }
void iconsize(long x, long y) { icw = (int)x; ich = (int)y; }
void keepaspect(long x, long y) { aspx = (int)x; aspy = (int)y; }
void getsize(long *x, long *y) { *x = winw; *y = winh; }
void getorigin(long *x, long *y)
{
    int rx = 0, ry = 0;
    Window c;
    if (dpy && win) XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &rx, &ry, &c);
    *x = rx; *y = DisplayHeight(dpy, scr) - ry - winh;
}
long getheight(void)
{
    if (!xfont) return 12;
    return xfont->ascent + xfont->descent;
}
void doublebuffer(void) {
TR("doublebuffer");  dbuf = 1; }
void singlebuffer(void) { dbuf = 0; }
void RGBmode(void) {
TR("RGBmode");  rgbmode = 1; }
void cmode(void) { rgbmode = 0; }
void gconfig(void) {
TR("gconfig");  }
void glcompat(long mode, long value) { (void)mode; (void)value; }
void polysmooth(long m) { (void)m; }
void subpixel(Boolean b) { (void)b; }
void blendfunction(long s, long d) { (void)s; (void)d; }
void zbuffer(Boolean b) { (void)b; }
void ringbell(void) { if (dpy) XBell(dpy, 0); }
void gflush(void) { if (dpy) XFlush(dpy); }
void finish(void) { if (dpy) XSync(dpy, False); }

long getgdesc(long inq)
{
    TR("getgdesc(%ld)", inq);
    ensure_dpy();
    switch (inq) {
    case GD_XPMAX: return DisplayWidth(dpy, scr);
    case GD_YPMAX: return DisplayHeight(dpy, scr);
    case GD_BITS_NORM_SNG_CMODE: return 8;
    case GD_BITS_NORM_DBL_CMODE: return 8;
    case GD_BITS_NORM_SNG_RED: case GD_BITS_NORM_SNG_GREEN:
    case GD_BITS_NORM_SNG_BLUE: return 8;
    case GD_BITS_NORM_DBL_RED: case GD_BITS_NORM_DBL_GREEN:
    case GD_BITS_NORM_DBL_BLUE: return 8;
    case GD_BITS_NORM_ZBUFFER: return 0;
    case GD_BLEND: return 0;
    case GD_TEXTPORT: return 0;
    default: return 0;
    }
}

/* ---- colour ------------------------------------------------------------ */
static void init_cmap(void)
{
    static int done;
    static const int base[8][3] = {
        {0,0,0},{255,0,0},{0,255,0},{255,255,0},
        {0,0,255},{255,0,255},{0,255,255},{255,255,255}
    };
    int i;
    if (done) return;
    done = 1;
    for (i = 0; i < 8; ++i)
        cmap[i] = rgb_pixel(base[i][0], base[i][1], base[i][2]);
    /* 8..255: the desktop's default colormap is not available; use a gray
     * ramp, with the entries the SGI clock draws with set explicitly. */
    for (i = 8; i < 256; ++i) {
        int g = (i * 255) / 255;
        cmap[i] = rgb_pixel(g, g, g);
    }
    cmap[34] = rgb_pixel(0x1c, 0x1c, 0x1c);   /* hand shadow (drawn first, offset) */
    cmap[37] = rgb_pixel(0x3a, 0x3a, 0x3a);   /* face */
    cmap[42] = rgb_pixel(0xd0, 0xd0, 0xd0);   /* hand fill */
    cmap[52] = rgb_pixel(0xe8, 0xe8, 0xe8);   /* tick marks */
    cmap[54] = rgb_pixel(0xf8, 0xf8, 0xf8);   /* hand outline */
}

void color(Colorindex c)
{
    TR("color(%d)", (int)c);
    ensure_dpy();
    init_cmap();
    cur_index = c;
    set_fg(cmap[c & 255]);
}

void mapcolor(Colorindex i, short r, short g, short b)
{
    ensure_dpy();
    init_cmap();
    cmap[i & 255] = rgb_pixel(r, g, b);
}

void cpack(unsigned long c)       /* 0xAABBGGRR */
{
    TR("cpack(%08lx)", c);
    ensure_dpy();
    set_fg(rgb_pixel((int)(c & 255), (int)((c >> 8) & 255), (int)((c >> 16) & 255)));
}

void RGBcolor(short r, short g, short b)
{
    ensure_dpy();
    set_fg(rgb_pixel(r, g, b));
}

void c3i(const long c[3]) { RGBcolor((short)c[0], (short)c[1], (short)c[2]); }
void c3s(const short c[3]) { RGBcolor(c[0], c[1], c[2]); }

void clear(void)
{
    TR("clear()");
    if (!dpy || !win) return;
    XFillRectangle(dpy, target(), gc, 0, 0, (unsigned)winw, (unsigned)winh);
}

/* ---- matrices ---------------------------------------------------------- */
void mmode(short m) { (void)m; }
void ortho2(Coord l, Coord r, Coord b, Coord t)
{
    TR("ortho2(%g,%g,%g,%g)", l, r, b, t);
    Mat p;
    mat_ident(&p);
    p.m[0][0] = 2.0f / (r - l);  p.m[0][3] = -(r + l) / (r - l);
    p.m[1][1] = 2.0f / (t - b);  p.m[1][3] = -(t + b) / (t - b);
    mat_ident(&cur);              /* ortho2 defines the projection */
    mat_mul_right(&cur, &p);
}
void loadmatrix(const Matrix m)
{
    TR("loadmatrix");
    int i, j;
    for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) cur.m[j][i] = m[i][j]; /* row->col */
}
void pushmatrix(void) { TR("pushmatrix"); if (sp < 31) stack[sp++] = cur; }
void popmatrix(void) { TR("popmatrix"); if (sp > 0) cur = stack[--sp]; }
void translate(Coord x, Coord y, Coord z)
{
    TR("translate(%g,%g,%g)", x, y, z);
    Mat t; mat_ident(&t);
    t.m[0][3] = x; t.m[1][3] = y; t.m[2][3] = z;
    mat_mul_right(&cur, &t);
}
void scale(float x, float y, float z)
{
    TR("scale(%g,%g,%g)", x, y, z);
    Mat t; mat_ident(&t);
    t.m[0][0] = x; t.m[1][1] = y; t.m[2][2] = z;
    mat_mul_right(&cur, &t);
}
void rotate(Angle a, char axis)
{
    TR("rotate(%d,%c)", (int)a, axis);
    Mat t; float r = (float)a * (float)M_PI / 1800.0f, c = cosf(r), s = sinf(r);
    mat_ident(&t);
    if (axis == 'z' || axis == 'Z') {
        t.m[0][0] = c; t.m[0][1] = -s; t.m[1][0] = s; t.m[1][1] = c;
    } else if (axis == 'x' || axis == 'X') {
        t.m[1][1] = c; t.m[1][2] = -s; t.m[2][1] = s; t.m[2][2] = c;
    } else {
        t.m[0][0] = c; t.m[0][2] = s; t.m[2][0] = -s; t.m[2][2] = c;
    }
    mat_mul_right(&cur, &t);
}
void viewport(Screencoord l, Screencoord r, Screencoord b, Screencoord t)
{
    TR("viewport(%d,%d,%d,%d)", l, r, b, t);
    vpl = l; vpb = b; vpw = r - l + 1; vph = t - b + 1;
}

/* ---- primitives -------------------------------------------------------- */
static void fill_poly(const float *xs, const float *ys, int n)
{
    XPoint p[MAXV];
    int i;
    if (n < 3 || !dpy || !win) return;
    for (i = 0; i < n && i < MAXV; ++i) {
        int ix, iy;
        project(xs[i], ys[i], 0, &ix, &iy);
        p[i].x = (short)ix; p[i].y = (short)iy;
    }
    XFillPolygon(dpy, target(), gc, p, n, Complex, CoordModeOrigin);
}

void rectf(Coord x1, Coord y1, Coord x2, Coord y2)
{
    TR("rectf(%g,%g,%g,%g)", x1, y1, x2, y2);
    float xs[4], ys[4];
    xs[0] = x1; ys[0] = y1; xs[1] = x2; ys[1] = y1;
    xs[2] = x2; ys[2] = y2; xs[3] = x1; ys[3] = y2;
    fill_poly(xs, ys, 4);
}
void recti(Icoord a, Icoord b, Icoord c, Icoord d) { rectf((Coord)a, (Coord)b, (Coord)c, (Coord)d); }

void bgnpolygon(void) {
TR("bgnpolygon");  mode = M_POLY; nv = 0; }
void endpolygon(void) { TR("endpolygon"); if (mode == M_POLY) fill_poly(vx, vy, nv); mode = M_NONE; nv = 0; }
void bgnclosedline(void) { TR("bgnclosedline"); mode = M_LINE; nv = 0; }
void bgnline(void) { TR("bgnline"); mode = M_LINE; nv = 0; }
static void draw_line(int closed)
{
    XPoint p[MAXV + 1];
    int i;
    if (nv < 2 || !dpy || !win) return;
    for (i = 0; i < nv && i < MAXV; ++i) {
        int ix, iy;
        project(vx[i], vy[i], 0, &ix, &iy);
        p[i].x = (short)ix; p[i].y = (short)iy;
    }
    if (closed) { p[nv] = p[0]; XDrawLines(dpy, target(), gc, p, nv + 1, CoordModeOrigin); }
    else XDrawLines(dpy, target(), gc, p, nv, CoordModeOrigin);
}
void endclosedline(void) { TR("endclosedline"); if (mode == M_LINE) draw_line(1); mode = M_NONE; nv = 0; }
void endline(void) { TR("endline"); if (mode == M_LINE) draw_line(0); mode = M_NONE; nv = 0; }
void bgntmesh(void) { TR("bgntmesh"); mode = M_TMESH; tcount = 0; tswap = 0; }
void endtmesh(void) { TR("endtmesh"); mode = M_NONE; tcount = 0; }
void swaptmesh(void) { TR("swaptmesh"); tswap = 1; }

static void add_vertex(float x, float y)
{
    if (mode == M_POLY || mode == M_LINE) {
        if (nv < MAXV) { vx[nv] = x; vy[nv] = y; ++nv; }
    } else if (mode == M_TMESH) {
        if (tcount == 0) { tp0x = x; tp0y = y; tcount = 1; }
        else if (tcount == 1) { tp1x = x; tp1y = y; tcount = 2; }
        else {
            float xs[3], ys[3];
            xs[0] = tp0x; ys[0] = tp0y; xs[1] = tp1x; ys[1] = tp1y;
            xs[2] = x; ys[2] = y;
            fill_poly(xs, ys, 3);
            if (tswap) { tp1x = x; tp1y = y; tswap = 0; }   /* replace newest */
            else { tp0x = tp1x; tp0y = tp1y; tp1x = x; tp1y = y; }
        }
    } else {
        /* a bare vertex outside begin/end draws a point */
    }
}
void v2f(const float v[2]) { TR("v2f(%g,%g)", v[0], v[1]); add_vertex(v[0], v[1]); }
void v2i(const long v[2]) { add_vertex((float)v[0], (float)v[1]); }
void v2s(const short v[2]) { add_vertex((float)v[0], (float)v[1]); }
void v3f(const float v[3]) { add_vertex(v[0], v[1]); }

void pnt(Coord x, Coord y, Coord z)
{
    int ix, iy;
    (void)z;
    if (!dpy || !win) return;
    project(x, y, 0, &ix, &iy);
    XDrawPoint(dpy, target(), gc, ix, iy);
}

void swapbuffers(void)
{
    TR("swapbuffers()");
    if (!dpy || !win) return;
    if (dbuf && back)
        XCopyArea(dpy, back, win, gc, 0, 0, (unsigned)winw, (unsigned)winh, 0, 0);
    XFlush(dpy);
}

/* ---- text -------------------------------------------------------------- */
void cmov2i(Icoord x, Icoord y)
{
    int ix, iy;
    project((float)x, (float)y, 0, &ix, &iy);
    charx = ix; chary = iy;
}
void cmov2(Coord x, Coord y) { cmov2i((Icoord)x, (Icoord)y); }
void charstr(String s)
{
    if (!dpy || !win || !s) return;
    XDrawString(dpy, target(), gc, charx, chary, s, (int)strlen(s));
}
long strwidth(String s)
{
    if (!xfont || !s) return 0;
    return XTextWidth(xfont, s, (int)strlen(s));
}

/* ---- events ------------------------------------------------------------ */
static void qput(short dev, short val)
{
    int n = (qt + 1) % QMAX;
    if (n == qh) return;
    q[qt].dev = dev; q[qt].val = val; qt = n;
}

static int wanted(short dev)
{
    switch (dev) {
    case REDRAW: case INPUTCHANGE: case WINQUIT: case WINSHUT: case WINFREEZE:
    case WINTHAW: case DEPTHCHANGE: return 1;
    default: return dev >= 0 && dev < 1024 && qenabled[dev];
    }
}

static void pump(void)
{
    XEvent e;
    while (XPending(dpy)) {
        XNextEvent(dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) qput(REDRAW, (short)gid);
            break;
        case ConfigureNotify:
            if (e.xconfigure.width != winw || e.xconfigure.height != winh) {
                winw = e.xconfigure.width; winh = e.xconfigure.height;
                if (back) { XFreePixmap(dpy, back); back = 0; }
                vpl = vpb = 0; vpw = winw; vph = winh;
            }
            qput(REDRAW, (short)gid);
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == wm_delete) qput(WINQUIT, 0);
            break;
        case DestroyNotify: qput(WINSHUT, 0); break;
        case ButtonPress: case ButtonRelease: {
            short d = e.xbutton.button == 1 ? LEFTMOUSE
                    : e.xbutton.button == 2 ? MIDDLEMOUSE : RIGHTMOUSE;
            if (wanted(d)) qput(d, e.type == ButtonPress ? 1 : 0);
            break;
        }
        case KeyPress: {
            char buf[8];
            KeySym ks;
            int n = XLookupString(&e.xkey, buf, sizeof(buf), &ks, NULL);
            if (n > 0 && wanted(KEYBD)) qput(KEYBD, (short)buf[0]);
            break;
        }
        default: break;
        }
    }
}

static int timer_due(void)
{
    struct timeval now;
    if (!timer_en) return 0;
    gettimeofday(&now, NULL);
    return now.tv_sec > timer_next.tv_sec ||
           (now.tv_sec == timer_next.tv_sec && now.tv_usec >= timer_next.tv_usec);
}
static void timer_rearm(void)
{
    gettimeofday(&timer_next, NULL);
    timer_next.tv_usec += (long)timer_ms * 1000;
    timer_next.tv_sec += timer_next.tv_usec / 1000000;
    timer_next.tv_usec %= 1000000;
}

void qdevice(Device d)
{
    TR("qdevice(%d)", d);
    if (d >= 0 && d < 1024) qenabled[d] = 1;
    if (d == TIMER0 && timer_ms == 0) { timer_ms = 1000; }
}
void unqdevice(Device d) { if (d >= 0 && d < 1024) qenabled[d] = 0; }
void noise(Device d, short delta)
{
    TR("noise(%d,%d)", d, delta);
    /* TIMER0 ticks are 1/60 s; a zero delta means every tick */
    if (d == TIMER0) { int t = delta < 1 ? 1 : delta; timer_ms = t * 1000 / 60; if (timer_ms < 16) timer_ms = 16; }
}
void qreset(void) { qh = qt = 0; }

long qtest(void)
{
    ensure_dpy();
    pump();
    if (timer_due() && qenabled[TIMER0]) { qput(TIMER0, 0); timer_rearm(); }
    return qh == qt ? 0 : q[qh].dev;
}

long qread(short *val)
{
    ensure_dpy();
    for (;;) {
        if (qtest()) {
            short d = q[qh].dev;
            *val = q[qh].val;
            qh = (qh + 1) % QMAX;
            TR("qread -> dev %d val %d", d, *val);
            return d;
        }
        {
            fd_set fds;
            struct timeval tv, *tp = NULL;
            int fd = ConnectionNumber(dpy);
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            if (timer_en || qenabled[TIMER0]) {
                struct timeval now;
                if (!timer_en) { timer_en = 1; timer_rearm(); }
                gettimeofday(&now, NULL);
                tv.tv_sec = timer_next.tv_sec - now.tv_sec;
                tv.tv_usec = timer_next.tv_usec - now.tv_usec;
                if (tv.tv_usec < 0) { tv.tv_usec += 1000000; tv.tv_sec -= 1; }
                if (tv.tv_sec < 0) { tv.tv_sec = 0; tv.tv_usec = 0; }
                tp = &tv;
            }
            select(fd + 1, &fds, NULL, NULL, tp);
        }
    }
}

long getvaluator(Device d)
{
    Window rr, cc;
    int rx, ry, wx, wy;
    unsigned int m;
    ensure_dpy();
    if (!XQueryPointer(dpy, RootWindow(dpy, scr), &rr, &cc, &rx, &ry, &wx, &wy, &m))
        return 0;
    if (d == MOUSEX) return rx;
    if (d == MOUSEY) return DisplayHeight(dpy, scr) - 1 - ry;
    return 0;
}

/* ---- pup menus --------------------------------------------------------- */
#define MAXMENU 16
#define MAXITEM 32
typedef struct {
    int n;
    char title[64];
    char label[MAXITEM][64];
    long value[MAXITEM];
    int grey[MAXITEM];
} Menu;
static Menu menus[MAXMENU];
static int nmenus;

/* IRIX libc has no strsep */
static char *my_strsep(char **sp, const char *delim)
{
    char *s = *sp, *e;
    if (!s) return NULL;
    e = s + strcspn(s, delim);
    if (*e) { *e = 0; *sp = e + 1; } else *sp = NULL;
    return s;
}

long defpup(String fmt, ...)
{
    Menu *m;
    char buf[1024], *p, *tok;
    iva_list ap;
    int id;
    if (nmenus >= MAXMENU) return -1;
    iva_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    iva_end(ap);
    id = nmenus++;
    m = &menus[id];
    memset(m, 0, sizeof(*m));
    for (p = buf; (tok = my_strsep(&p, "|")) != NULL; ) {
        char *pc = strchr(tok, '%');
        long val = m->n + 1;
        int is_title = 0, is_line = 0;
        while (pc) {
            if (pc[1] == 't') { is_title = 1; }
            else if (pc[1] == 'l') { is_line = 1; }
            else if (pc[1] == 'x') val = strtol(pc + 2, NULL, 10);
            *pc = 0;
            pc = strchr(pc + 1, '%');
        }
        while (*tok == ' ') ++tok;
        if (is_title) { snprintf(m->title, sizeof(m->title), "%s", tok); continue; }
        if (m->n < MAXITEM && (*tok || is_line)) {
            snprintf(m->label[m->n], sizeof(m->label[m->n]), "%s", tok);
            m->value[m->n] = val;
            ++m->n;
        }
    }
    return id;
}

void setpup(long id, long item, unsigned long mode)
{
    if (id >= 0 && id < nmenus && item >= 1 && item <= menus[id].n)
        menus[id].grey[item - 1] = (mode & PUP_GREY) != 0;
}
void freepup(long id) { (void)id; }

long dopup(long id)
{
    Menu *m;
    Window w, rr, cc;
    int rx, ry, wx, wy, i, lh, ww, hh, sel = -1, top;
    unsigned int mask;
    XEvent e;
    GC mgc;
    ensure_dpy();
    if (id < 0 || id >= nmenus) return -1;
    m = &menus[id];
    XQueryPointer(dpy, RootWindow(dpy, scr), &rr, &cc, &rx, &ry, &wx, &wy, &mask);
    lh = (xfont ? xfont->ascent + xfont->descent : 12) + 6;
    ww = 120;
    for (i = 0; i < m->n; ++i) {
        int tw = xfont ? XTextWidth(xfont, m->label[i], (int)strlen(m->label[i])) : 60;
        if (tw + 16 > ww) ww = tw + 16;
    }
    top = m->title[0] ? lh : 0;
    hh = top + lh * m->n + 2;
    {
        XSetWindowAttributes a;
        a.override_redirect = True;
        a.background_pixel = WhitePixel(dpy, scr);
        a.border_pixel = BlackPixel(dpy, scr);
        a.event_mask = ExposureMask | ButtonReleaseMask | PointerMotionMask | ButtonPressMask;
        w = XCreateWindow(dpy, RootWindow(dpy, scr), rx, ry, (unsigned)ww, (unsigned)hh, 1,
                          CopyFromParent, InputOutput, CopyFromParent,
                          CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &a);
    }
    mgc = XCreateGC(dpy, w, 0, NULL);
    if (xfont) XSetFont(dpy, mgc, xfont->fid);
    XMapRaised(dpy, w);
    XGrabPointer(dpy, w, False, ButtonReleaseMask | PointerMotionMask | ButtonPressMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    for (;;) {
        XNextEvent(dpy, &e);
        if (e.type == Expose || e.type == MotionNotify) {
            int py = (e.type == MotionNotify ? e.xmotion.y : -1);
            if (e.type == MotionNotify) {
                int idx = (py - top) / lh;
                sel = (py >= top && idx >= 0 && idx < m->n && !m->grey[idx]) ? idx : -1;
            }
            XSetForeground(dpy, mgc, WhitePixel(dpy, scr));
            XFillRectangle(dpy, w, mgc, 0, 0, (unsigned)ww, (unsigned)hh);
            XSetForeground(dpy, mgc, BlackPixel(dpy, scr));
            if (top) XDrawString(dpy, w, mgc, 8, lh - 6, m->title, (int)strlen(m->title));
            for (i = 0; i < m->n; ++i) {
                int y = top + i * lh;
                if (i == sel) {
                    XFillRectangle(dpy, w, mgc, 1, y, (unsigned)ww - 2, (unsigned)lh);
                    XSetForeground(dpy, mgc, WhitePixel(dpy, scr));
                }
                XDrawString(dpy, w, mgc, 8, y + lh - 6, m->label[i], (int)strlen(m->label[i]));
                XSetForeground(dpy, mgc, BlackPixel(dpy, scr));
            }
        } else if (e.type == ButtonRelease) {
            break;
        }
    }
    XUngrabPointer(dpy, CurrentTime);
    XFreeGC(dpy, mgc);
    XDestroyWindow(dpy, w);
    XFlush(dpy);
    return sel >= 0 ? m->value[sel] : -1;
}

/* misc */
void sginap_stub(void) { }
