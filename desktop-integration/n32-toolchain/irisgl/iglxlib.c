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
    /* 8..255: the IRIX default colormap beyond the basic eight is a gray
     * ramp, gray(i) = 4*(i-8)+1.  Measured from real gr_osview (backcolor 46
     * -> #999999) and cal (#E1E1E1 -> 64) screenshots. */
    for (i = 8; i < 256; ++i) {
        int g = 4 * (i - 8) + 1;
        if (g > 255) g = 255;
        cmap[i] = rgb_pixel(g, g, g);
    }
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
static void cmov_f(float x, float y)
{
    int ix, iy;
    project(x, y, 0, &ix, &iy);
    charx = ix; chary = iy;
}
void cmov2i(Icoord x, Icoord y) { cmov_f((float)x, (float)y); }
void cmov2(Coord x, Coord y) { cmov_f(x, y); }
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

/* ---- additions for gr_osview / ical -------------------------------------- */
static float penx, peny;

void move(Coord x, Coord y, Coord z) { (void)z; penx = x; peny = y; }
void movei(Icoord x, Icoord y, Icoord z) { (void)z; penx = (float)x; peny = (float)y; }
void move2(Coord x, Coord y) { penx = x; peny = y; }
void move2i(Icoord x, Icoord y) { penx = (float)x; peny = (float)y; }
void draw(Coord x, Coord y, Coord z)
{
    int ax, ay, bx, by;
    (void)z;
    if (!dpy || !win) return;
    project(penx, peny, 0, &ax, &ay);
    project(x, y, 0, &bx, &by);
    XDrawLine(dpy, target(), gc, ax, ay, bx, by);
    penx = x; peny = y;
}
void drawi(Icoord x, Icoord y, Icoord z) { draw((Coord)x, (Coord)y, (Coord)z); }
void draw2(Coord x, Coord y) { draw(x, y, 0); }
void draw2i(Icoord x, Icoord y) { draw((Coord)x, (Coord)y, 0); }

/* outline rectangle */
void rect(Coord x1, Coord y1, Coord x2, Coord y2)
{
    float xs[4], ys[4];
    int i, nvsave = nv;
    TR("rect(%g,%g,%g,%g)", x1, y1, x2, y2);
    (void)nvsave;
    xs[0] = x1; ys[0] = y1; xs[1] = x2; ys[1] = y1;
    xs[2] = x2; ys[2] = y2; xs[3] = x1; ys[3] = y2;
    for (i = 0; i < 4; ++i) { vx[i] = xs[i]; vy[i] = ys[i]; }
    nv = 4;
    draw_line(1);
    nv = 0;
}
void reci(Icoord a, Icoord b, Icoord c, Icoord d) { rect((Coord)a, (Coord)b, (Coord)c, (Coord)d); }

void rectcopy(Screencoord x1, Screencoord y1, Screencoord x2, Screencoord y2,
              Screencoord nx, Screencoord ny)
{
    int w, h, sx, sy, dx, dy;
    if (!dpy || !win) return;
    if (x2 < x1) { Screencoord t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { Screencoord t = y1; y1 = y2; y2 = t; }
    w = x2 - x1 + 1; h = y2 - y1 + 1;
    sx = x1; sy = winh - 1 - y2;
    dx = nx; dy = winh - 1 - (ny + h - 1);
    XCopyArea(dpy, target(), target(), gc, sx, sy, (unsigned)w, (unsigned)h, dx, dy);
}

void linewidth(short w) { if (gc) XSetLineAttributes(dpy, gc, (unsigned)(w > 1 ? w : 0), LineSolid, CapButt, JoinMiter); }
void shademodel(long m) { (void)m; }
void getcpos(short *x, short *y) { *x = (short)charx; *y = (short)(winh - 1 - chary); }
void getmatrix(Matrix m)
{
    int i, j;
    for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) m[i][j] = cur.m[j][i];
}
void ortho(Coord l, Coord r, Coord b, Coord t, Coord n, Coord f)
{ (void)n; (void)f; ortho2(l, r, b, t); }
void reshapeviewport(void)
{
    XWindowAttributes a;
    if (!dpy || !win) return;
    if (XGetWindowAttributes(dpy, win, &a)) {
        if (a.width != winw || a.height != winh) {
            winw = a.width; winh = a.height;
            if (back) { XFreePixmap(dpy, back); back = 0; }
        }
    }
    vpl = vpb = 0; vpw = winw; vph = winh;
}
void frontbuffer(Boolean b) { (void)b; }
void backbuffer(Boolean b) { (void)b; }
void foreground(void) { }
void noborder(void) { }
void minsize(long x, long y) { (void)x; (void)y; }
void winpop(void) { if (dpy && win) XRaiseWindow(dpy, win); }
long qgetfd(void) { ensure_dpy(); return dpy ? ConnectionNumber(dpy) : -1; }

/* ---- cursor movement variants ------------------------------------------- */
void cmov(Coord x, Coord y, Coord z) { (void)z; cmov_f(x, y); }
void cmovi(Icoord x, Icoord y, Icoord z) { (void)z; cmov2i(x, y); }
void cmovs(Scoord x, Scoord y, Scoord z) { (void)z; cmov2i((Icoord)x, (Icoord)y); }
void cmov2s(Scoord x, Scoord y) { cmov2i((Icoord)x, (Icoord)y); }
void dither(long m) { (void)m; }
long getdisplaymode(void) { return 1; }          /* DMRGBDOUBLE-ish: unused */

/* ---- Font Manager over X core fonts -------------------------------------- */
/*
 * libfm.so is a thin wrapper: fmfindfont() etc. call gl_d_fmfindfont() when
 * gl_islocal() is true, with the signatures of <fmclient.h>.  Fonts are
 * mapped to X core fonts by family/weight/slant and point size (1 pt = 1 px).
 */
#include <fmclient.h>

typedef struct { char name[64]; double size; XFontStruct *xf; } FmFont;
static FmFont *fm_cur;

static XFontStruct *fm_load(const char *name, double size)
{
    char low[64], pat[160];
    const char *fam = "helvetica", *wt = "medium", *sl = "r";
    int px = size > 1.0 ? (int)(size + 0.5) : 12, i;
    XFontStruct *f;

    ensure_dpy();
    for (i = 0; name && name[i] && i < 63; ++i)
        low[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    low[i] = 0;
    if (strstr(low, "courier") || strstr(low, "mono")) fam = "courier";
    else if (strstr(low, "times") || strstr(low, "roman") || strstr(low, "serif")) fam = "times";
    else if (strstr(low, "symbol")) fam = "symbol";
    if (strstr(low, "bold")) wt = "bold";
    if (strstr(low, "oblique") || strstr(low, "italic")) sl = (strcmp(fam, "times") == 0) ? "i" : "o";
    snprintf(pat, sizeof(pat), "-*-%s-%s-%s-normal--%d-*-*-*-*-*-iso8859-1", fam, wt, sl, px);
    f = XLoadQueryFont(dpy, pat);
    if (!f) {
        snprintf(pat, sizeof(pat), "-*-%s-%s-%s-*-*-%d-*-*-*-*-*-*-*", fam, wt, sl, px);
        f = XLoadQueryFont(dpy, pat);
    }
    if (!f) f = XLoadQueryFont(dpy, "fixed");
    return f;
}

static FmFont *fm_make(const char *name, double size)
{
    FmFont *f = (FmFont *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    strncpy(f->name, name ? name : "Helvetica", sizeof(f->name) - 1);
    f->size = size;
    f->xf = fm_load(f->name, size);
    return f;
}

long gl_islocal(void) { TR("gl_islocal");  return 1; }
void gl_beginstring(void) { TR("gl_beginstring");  }
void gl_endstring(void) { TR("gl_endstring");  }
void gl_drawbitmap(long a, long b, long c, long d, long e, long f, long g, long h)
{ TR("gl_drawbitmap(%ld,%ld,%ld,%ld,%ld,%ld,%lx,%lx)", a, b, c, d, e, f, g, h); }

void gl_d_fminit(void) { TR("gl_d_fminit");  TR("fminit"); ensure_dpy(); }
fmfonthandle gl_d_fmfindfont(const char *name)
{ TR("gl_d_fmfindfont"); 
    TR("fmfindfont(%s)", name ? name : "(null)");
    return (fmfonthandle)fm_make(name, 12.0);
}
fmfonthandle gl_d_fmscalefont(fmfonthandle h, double size)
{ TR("gl_d_fmscalefont"); 
    FmFont *o = (FmFont *)h;
    TR("fmscalefont(%g)", size);
    return o ? (fmfonthandle)fm_make(o->name, size) : NULL;
}
fmfonthandle gl_d_fmmakefont(fmfonthandle h, double m[3][2])
{ TR("gl_d_fmmakefont"); 
    FmFont *o = (FmFont *)h;
    double s = m[1][1] != 0 ? (m[1][1] < 0 ? -m[1][1] : m[1][1]) : 1.0;
    return o ? (fmfonthandle)fm_make(o->name, o->size * s) : NULL;
}
void gl_d_fmfreefont(fmfonthandle h)
{ TR("gl_d_fmfreefont"); 
    FmFont *f = (FmFont *)h;
    if (!f) return;
    if (f->xf && dpy) XFreeFont(dpy, f->xf);
    if (f == fm_cur) fm_cur = NULL;
    free(f);
}
void gl_d_fmsetfont(fmfonthandle h)
{ TR("gl_d_fmsetfont"); 
    FmFont *f = (FmFont *)h;
    TR("fmsetfont");
    fm_cur = f;
    if (f && f->xf && gc) XSetFont(dpy, gc, f->xf->fid);
}
int gl_d_fmprstr(const char *s)
{ TR("gl_d_fmprstr"); 
    XFontStruct *xf = fm_cur ? fm_cur->xf : NULL;
    TR("fmprstr(%s)", s ? s : "(null)");
    if (!dpy || !win || !s) return 0;
    if (xf && gc) XSetFont(dpy, gc, xf->fid);
    XDrawString(dpy, target(), gc, charx, chary, s, (int)strlen(s));
    charx += xf ? XTextWidth(xf, s, (int)strlen(s)) : (int)strlen(s) * 6;
    return (int)strlen(s);
}
int gl_d_fmfprstr(fmfonthandle h, unsigned char *s, short len, int x, int y)
{ TR("gl_d_fmfprstr"); 
    FmFont *f = (FmFont *)h;
    if (!dpy || !win || !s) return 0;
    if (f && f->xf && gc) XSetFont(dpy, gc, f->xf->fid);
    XDrawString(dpy, target(), gc, x, winh - 1 - y, (char *)s, len);
    return len;
}
long gl_d_fmgetstrwidth(fmfonthandle h, const char *s)
{ TR("gl_d_fmgetstrwidth"); 
    FmFont *f = (FmFont *)h;
    if (!f || !f->xf || !s) return 0;
    return XTextWidth(f->xf, s, (int)strlen(s));
}
long gl_d_fmgetchrwidth(fmfonthandle h, const unsigned char c)
{ TR("gl_d_fmgetchrwidth"); 
    FmFont *f = (FmFont *)h;
    char ch = (char)c;
    if (!f || !f->xf) return 0;
    return XTextWidth(f->xf, &ch, 1);
}
long gl_d_fmoutchar(fmfonthandle h, const unsigned int c)
{ TR("gl_d_fmoutchar"); 
    FmFont *f = (FmFont *)h;
    char ch = (char)c;
    if (!dpy || !win || !f || !f->xf) return 0;
    XSetFont(dpy, gc, f->xf->fid);
    XDrawString(dpy, target(), gc, charx, chary, &ch, 1);
    charx += XTextWidth(f->xf, &ch, 1);
    return 0;
}
int gl_d_fmgetfontinfo(fmfonthandle h, fmfontinfo *i)
{ TR("gl_d_fmgetfontinfo"); 
    FmFont *f = (FmFont *)h;
    memset(i, 0, sizeof(*i));
    if (!f || !f->xf) return -1;
    i->matrix00 = f->size; i->matrix11 = f->size;
    i->type = FMBITMAPFONT;
    i->encoding = FMISO88591;
    i->xsize = f->xf->max_bounds.width;
    i->ysize = f->xf->ascent + f->xf->descent;
    i->height = i->ysize;
    i->width = f->xf->max_bounds.width;
    i->yorig = f->xf->descent;
    i->nglyphs = 224;
    i->firstcol = f->xf->min_char_or_byte2; i->lastcol = f->xf->max_char_or_byte2;
    i->firstrow = 0; i->lastrow = 0;
    i->resolution = 72;
    return 0;
}
int gl_d_fmgetfontname(fmfonthandle h, int len, char *buf)
{ TR("gl_d_fmgetfontname"); 
    FmFont *f = (FmFont *)h;
    if (!f || len <= 0) return -1;
    strncpy(buf, f->name, (size_t)len - 1); buf[len - 1] = 0;
    return (int)strlen(buf);
}
int gl_d_fmgetcomment(fmfonthandle h, int n, char *buf) { TR("gl_d_fmgetcomment");  (void)h; (void)n; if (buf) buf[0] = 0; return 0; }
void gl_d_fmenumerate(void (*cb)(char *))
{ TR("gl_d_fmenumerate"); 
    static char n1[] = "Helvetica", n2[] = "Helvetica-Bold", n3[] = "Times-Roman",
                n4[] = "Courier";
    if (cb) { cb(n1); cb(n2); cb(n3); cb(n4); }
}
void gl_d_fmcacheenable(void) { TR("gl_d_fmcacheenable");  }
void gl_d_fmcachedisable(void) { TR("gl_d_fmcachedisable");  }
int gl_d_fmgetcacheused(void) { TR("gl_d_fmgetcacheused");  return 0; }
void gl_d_fmsetcachelimit(int n) { TR("gl_d_fmsetcachelimit");  (void)n; }
int gl_d_fmcachelimit(void) { TR("gl_d_fmcachelimit");  return 1 << 20; }
void gl_d_fmsetpath(const char *p) { TR("gl_d_fmsetpath");  (void)p; }
void gl_d_fmprintermatch(int m) { TR("gl_d_fmprintermatch");  (void)m; }
void gl_d_fminitpagematrix(void) { TR("gl_d_fminitpagematrix");  }
void gl_d_fmconcatpagematrix(double m[3][2]) { TR("gl_d_fmconcatpagematrix");  (void)m; }
void gl_d_fmgetpagematrix(double m[3][2])
{ TR("gl_d_fmgetpagematrix");  m[0][0] = m[1][1] = 1; m[0][1] = m[1][0] = m[2][0] = m[2][1] = 0; }
void gl_d_fmsetpagematrix(double m[3][2]) { TR("gl_d_fmsetpagematrix");  (void)m; }
void gl_d_fmrotatepagematrix(double a) { TR("gl_d_fmrotatepagematrix");  (void)a; }
void gl_d_fmscalepagematrix(double s) { TR("gl_d_fmscalepagematrix");  (void)s; }
/* internals reached by libfm for point-size conversion and whole-font metrics */
long gl_d_fm_pointsize_to_pixels_int(long p) { TR("gl_d_fm_pointsize_to_pixels_int");  return p; }
long gl_d_fm_pixels_to_pointsize_int(long p) { TR("gl_d_fm_pixels_to_pointsize_int");  return p; }
char *gl_d_fm_fontpath_int(void) { TR("gl_d_fm_fontpath_int");  static char p[] = "/usr/lib/DPS/outline/base"; return p; }
long gl_d_fm_getwholemetrics_int(fmfonthandle h, fmglyphinfo *g)
{ TR("gl_d_fm_getwholemetrics_int");  (void)h; (void)g; return 0; }

/* window-font-manager (wfm) entry points: map onto the plain GL calls */
void gl_d_wfm_charstr(const char *s) { TR("gl_d_wfm_charstr");  charstr((String)s); }
void gl_d_wfm_charstrl(const char *s, long n)
{ TR("gl_d_wfm_charstrl");  if (dpy && win && s) { XDrawString(dpy, target(), gc, charx, chary, s, (int)n); charx += xfont ? XTextWidth(xfont, s, (int)n) : (int)n * 6; } }
void gl_d_wfm_charstrn(const char *s, long n) { TR("gl_d_wfm_charstrn");  gl_d_wfm_charstrl(s, n); }
void gl_d_wfm_cmov(Coord x, Coord y, Coord z) { TR("gl_d_wfm_cmov");  cmov(x, y, z); }
void gl_d_wfm_cmov2(Coord x, Coord y) { TR("gl_d_wfm_cmov2");  cmov2(x, y); }
void gl_d_wfm_cmov2i(Icoord x, Icoord y) { TR("gl_d_wfm_cmov2i");  cmov2i(x, y); }
void gl_d_wfm_cmov2s(Scoord x, Scoord y) { TR("gl_d_wfm_cmov2s");  cmov2s(x, y); }
void gl_d_wfm_cmovi(Icoord x, Icoord y, Icoord z) { TR("gl_d_wfm_cmovi");  cmovi(x, y, z); }
void gl_d_wfm_cmovs(Scoord x, Scoord y, Scoord z) { TR("gl_d_wfm_cmovs");  cmovs(x, y, z); }
void gl_d_wfm_color(Colorindex c) { TR("gl_d_wfm_color");  color(c); }
void gl_d_wfm_cpack(unsigned long c) { TR("gl_d_wfm_cpack");  cpack(c); }
void gl_d_wfm_aacolor(Colorindex c) { TR("gl_d_wfm_aacolor");  color(c); }
void gl_d_wfm_aacpack(unsigned long c) { TR("gl_d_wfm_aacpack");  cpack(c); }
long gl_d_wfm_strwidth(const char *s) { TR("gl_d_wfm_strwidth");  return strwidth((String)s); }
long gl_d_wfm_strnwidth(const char *s, long n)
{ TR("gl_d_wfm_strnwidth");  return (xfont && s) ? XTextWidth(xfont, s, (int)n) : 0; }
void gl_d_wfm_init(void) { TR("gl_d_wfm_init");  ensure_dpy(); }
void gl_d_wfm_enumerate(void (*cb)(char *)) { TR("gl_d_wfm_enumerate");  gl_d_fmenumerate(cb); }
void gl_d_wfm_font(long f) { TR("gl_d_wfm_font");  (void)f; }
void gl_d_wfm_fontpath(const char *p) { TR("gl_d_wfm_fontpath");  (void)p; }
void gl_d_wfm_setfontpath(const char *p) { TR("gl_d_wfm_setfontpath");  (void)p; }
void gl_d_wfm_fontunits(long u) { TR("gl_d_wfm_fontunits");  (void)u; }
void gl_d_wfm_freefont(long f) { TR("gl_d_wfm_freefont");  (void)f; }
long gl_d_wfm_getfont(void) { TR("gl_d_wfm_getfont");  return 0; }
long gl_d_wfm_getfontinfo(long f, void *i) { TR("gl_d_wfm_getfontinfo");  (void)f; (void)i; return 0; }
void gl_d_wfm_hint(long h) { TR("gl_d_wfm_hint");  (void)h; }
long gl_d_wfm_makefont_int(long f, void *m) { TR("gl_d_wfm_makefont_int");  (void)f; (void)m; return 0; }
void gl_d_wfm_makefontmat(void *m) { TR("gl_d_wfm_makefontmat");  (void)m; }
void gl_d_wfm_outchar(long c) { TR("gl_d_wfm_outchar");  (void)c; }
void gl_d_wfm_pos(Coord x, Coord y) { TR("gl_d_wfm_pos");  cmov2(x, y); }
void gl_d_wfm_rpos(Coord x, Coord y) { TR("gl_d_wfm_rpos");  charx += (int)x; chary -= (int)y; }
void gl_d_wfm_skipspace(void) { TR("gl_d_wfm_skipspace");  }
void gl_d_wfm_sizeenumerate(long f, void *cb) { TR("gl_d_wfm_sizeenumerate");  (void)f; (void)cb; }
void gl_d_wfm_strbounds(const char *s, void *b) { TR("gl_d_wfm_strbounds");  (void)s; (void)b; }
void gl_d_wfm_strnbounds(const char *s, long n, void *b) { TR("gl_d_wfm_strnbounds");  (void)s; (void)n; (void)b; }
void gl_d_wfm_getcbbox(long c, void *b) { TR("gl_d_wfm_getcbbox");  (void)c; (void)b; }
void gl_d_wfm_getcharmetrics_int(long c, void *m) { TR("gl_d_wfm_getcharmetrics_int");  (void)c; (void)m; }
void gl_d_wfm_getcoutline(long c, void *o) { TR("gl_d_wfm_getcoutline");  (void)c; (void)o; }

/*
 * Export the public Font Manager names from libgl as well.  gr_osview/ical
 * link libgl.so before libfm.so, so these win and the program draws text with
 * the X-font implementation above instead of libfm's own glyph renderer
 * (which needs the IRIS GL gl_drawbitmap hardware path).
 */
#define FMALIAS(ret, name, args) ret name args __attribute__((alias("gl_d_" #name)))
FMALIAS(void, fminit, (void));
FMALIAS(fmfonthandle, fmfindfont, (const char *));
FMALIAS(fmfonthandle, fmscalefont, (fmfonthandle, double));
FMALIAS(fmfonthandle, fmmakefont, (fmfonthandle, double[3][2]));
FMALIAS(void, fmfreefont, (fmfonthandle));
FMALIAS(void, fmsetfont, (fmfonthandle));
FMALIAS(int, fmprstr, (const char *));
FMALIAS(int, fmfprstr, (fmfonthandle, unsigned char *, short, int, int));
FMALIAS(long, fmgetstrwidth, (fmfonthandle, const char *));
FMALIAS(long, fmgetchrwidth, (fmfonthandle, const unsigned char));
FMALIAS(long, fmoutchar, (fmfonthandle, const unsigned int));
FMALIAS(int, fmgetfontinfo, (fmfonthandle, fmfontinfo *));
FMALIAS(int, fmgetfontname, (fmfonthandle, int, char *));
FMALIAS(int, fmgetcomment, (fmfonthandle, int, char *));
FMALIAS(void, fmenumerate, (void (*)(char *)));
FMALIAS(void, fmcacheenable, (void));
FMALIAS(void, fmcachedisable, (void));
FMALIAS(int, fmgetcacheused, (void));
FMALIAS(void, fmsetcachelimit, (int));
FMALIAS(int, fmcachelimit, (void));
FMALIAS(void, fmsetpath, (const char *));
FMALIAS(void, fmprintermatch, (int));
FMALIAS(void, fminitpagematrix, (void));
FMALIAS(void, fmconcatpagematrix, (double[3][2]));
FMALIAS(void, fmgetpagematrix, (double[3][2]));
FMALIAS(void, fmsetpagematrix, (double[3][2]));
FMALIAS(void, fmrotatepagematrix, (double));
FMALIAS(void, fmscalepagematrix, (double));

long fmgetwholemetrics(fmfonthandle h, fmglyphinfo *g)
{
    FmFont *f = (FmFont *)h;
    int c;
    TR("fmgetwholemetrics");
    if (!f || !f->xf || !g) return -1;
    for (c = 0; c < 256; ++c) {
        char ch = (char)c;
        memset(&g[c], 0, sizeof(g[c]));
        g[c].xsize = f->xf->max_bounds.width;
        g[c].ysize = f->xf->ascent + f->xf->descent;
        g[c].yorig = f->xf->descent;
        g[c].xmove = (float)XTextWidth(f->xf, &ch, 1);
        g[c].gtype = FMBITMAPGLYPH;
    }
    return 0;
}
