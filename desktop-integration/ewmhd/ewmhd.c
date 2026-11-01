/*
 * ewmhd: an EWMH companion for 4Dwm.
 *
 * 4Dwm (the IRIX window manager, a Motif mwm descendant) knows nothing of EWMH,
 * so modern clients that ask for it get no answer: a game that wants fullscreen
 * stays windowed and keeps its frame, and frameless apps (Steam, Discord, other
 * Chromium/Electron/GTK windows that draw their own title bar) cannot be dragged,
 * because they ask the window manager to move them with _NET_WM_MOVERESIZE.
 *
 * This process runs beside 4Dwm and answers those requests using only things 4Dwm
 * already understands: _MOTIF_WM_HINTS (to drop the decorations), ordinary
 * configure requests (to size, move and raise), and its own pointer grab (to run
 * a drag).  It changes nothing in 4Dwm.
 *
 * Implemented:
 *   _NET_WM_STATE_FULLSCREEN   add / remove / toggle by client message, and the
 *                              initial state a client sets before mapping
 *   _NET_WM_MOVERESIZE         move and the eight resize directions, keyboard
 *                              and cancel; the drag ends on button release
 *   _NET_SUPPORTED, _NET_SUPPORTING_WM_CHECK so clients know to ask
 *
 * One instance per X screen (like 4Dwm without -multiscreen).  usage: ewmhd
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#define MWM_HINTS_DECORATIONS (1L << 1)

static Display *dpy;
static Window root, check;
static int scr, sw, sh;

static Atom A_SUPPORTED, A_CHECK, A_WMNAME, A_UTF8, A_STATE, A_FULLSCREEN, A_MOVERESIZE,
            A_MOTIF, A_WMSTATE_LEGACY;

/* ---- per-window state we need to undo ---- */
typedef struct Saved {
    Window win;
    int x, y, w, h;               /* client position in root coordinates, size */
    unsigned long hints[5];       /* _MOTIF_WM_HINTS before we changed it */
    int had_hints;
    int fullscreen;
    struct Saved *next;
} Saved;
static Saved *saved;

static Saved *
find(Window w, int create)
{
    Saved *s;

    for (s = saved; s; s = s->next)
        if (s->win == w)
            return s;
    if (!create)
        return NULL;
    s = calloc(1, sizeof(*s));
    s->win = w;
    s->next = saved;
    saved = s;
    return s;
}

static void
forget(Window w)
{
    Saved **p, *s;

    for (p = &saved; (s = *p); p = &s->next)
        if (s->win == w) {
            *p = s->next;
            free(s);
            return;
        }
}

static int
xerr(Display *d, XErrorEvent *e)
{
    (void)d; (void)e;           /* windows come and go under us; ignore */
    return 0;
}

static void
root_pos(Window w, int *x, int *y)
{
    Window c;

    *x = *y = 0;
    XTranslateCoordinates(dpy, w, root, 0, 0, x, y, &c);
}

/* ---- _NET_WM_STATE property bookkeeping (4Dwm will not do it) ---- */
static void
set_state_atom(Window w, int on)
{
    Atom *cur = NULL, *out;
    Atom type;
    int fmt;
    unsigned long n = 0, after, i, m = 0;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, w, A_STATE, 0, 64, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data)
        cur = (Atom *)data;
    out = malloc((n + 1) * sizeof(Atom));
    for (i = 0; i < n; i++)
        if (cur[i] != A_FULLSCREEN)
            out[m++] = cur[i];
    if (on)
        out[m++] = A_FULLSCREEN;
    if (m)
        XChangeProperty(dpy, w, A_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)out, (int)m);
    else
        XDeleteProperty(dpy, w, A_STATE);
    free(out);
    if (data)
        XFree(data);
}

static int
has_fullscreen(Window w)
{
    Atom type, *cur;
    int fmt;
    unsigned long n, after, i;
    unsigned char *data = NULL;
    int r = 0;

    if (XGetWindowProperty(dpy, w, A_STATE, 0, 64, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
        cur = (Atom *)data;
        for (i = 0; i < n; i++)
            if (cur[i] == A_FULLSCREEN)
                r = 1;
        XFree(data);
    }
    return r;
}

/* ---- fullscreen ---- */
static void
settle(void)
{
    XSync(dpy, False);
    usleep(60 * 1000);          /* let 4Dwm re-frame before we size the window */
}

static void
fullscreen_on(Window w)
{
    Saved *s = find(w, 1);
    XWindowAttributes wa;
    unsigned long hints[5] = { MWM_HINTS_DECORATIONS, 0, 0, 0, 0 };
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;

    if (s->fullscreen)
        return;
    if (!XGetWindowAttributes(dpy, w, &wa))
        return;
    root_pos(w, &s->x, &s->y);
    s->w = wa.width;
    s->h = wa.height;
    if (XGetWindowProperty(dpy, w, A_MOTIF, 0, 5, False, A_MOTIF, &type, &fmt, &n, &after, &data) == Success && data && n >= 5) {
        memcpy(s->hints, data, sizeof(s->hints));
        s->had_hints = 1;
    } else {
        s->had_hints = 0;
    }
    if (data)
        XFree(data);

    /* no decorations: 4Dwm re-frames the window without them */
    XChangeProperty(dpy, w, A_MOTIF, A_MOTIF, 32, PropModeReplace, (unsigned char *)hints, 5);
    settle();
    XMoveResizeWindow(dpy, w, 0, 0, (unsigned)sw, (unsigned)sh);
    XRaiseWindow(dpy, w);
    set_state_atom(w, 1);
    s->fullscreen = 1;
    XSelectInput(dpy, w, StructureNotifyMask | PropertyChangeMask);
    XSync(dpy, False);
}

static void
fullscreen_off(Window w)
{
    Saved *s = find(w, 0);

    if (!s || !s->fullscreen)
        return;
    if (s->had_hints)
        XChangeProperty(dpy, w, A_MOTIF, A_MOTIF, 32, PropModeReplace, (unsigned char *)s->hints, 5);
    else
        XDeleteProperty(dpy, w, A_MOTIF);
    settle();
    XMoveResizeWindow(dpy, w, s->x, s->y, (unsigned)s->w, (unsigned)s->h);
    set_state_atom(w, 0);
    s->fullscreen = 0;
    XSync(dpy, False);
}

/* ---- _NET_WM_MOVERESIZE ---- */
enum { SZ_TL, SZ_T, SZ_TR, SZ_R, SZ_BR, SZ_B, SZ_BL, SZ_L, MOVE, SZ_KB, MOVE_KB, CANCEL };

static void
drag(Window w, int px, int py, int dir)
{
    XWindowAttributes wa;
    int ox, oy, ow, oh;
    Cursor cur;
    XEvent ev;
    int cursor_shape = XC_fleur;

    if (!XGetWindowAttributes(dpy, w, &wa))
        return;
    if (dir == CANCEL || dir == SZ_KB || dir == MOVE_KB)
        return;                                     /* keyboard moves not offered */
    root_pos(w, &ox, &oy);
    ow = wa.width;
    oh = wa.height;
    if (dir != MOVE) {
        static const int shapes[] = { XC_top_left_corner, XC_top_side, XC_top_right_corner, XC_right_side,
                                      XC_bottom_right_corner, XC_bottom_side, XC_bottom_left_corner, XC_left_side };
        cursor_shape = shapes[dir];
    }
    cur = XCreateFontCursor(dpy, (unsigned)cursor_shape);
    /* the client has already let go of its own grab, as the spec requires */
    if (XGrabPointer(dpy, root, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
                     None, cur, CurrentTime) != GrabSuccess) {
        XFreeCursor(dpy, cur);
        return;
    }
    XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    for (;;) {
        int dx, dy, nx = ox, ny = oy, nw = ow, nh = oh;

        XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | KeyPressMask, &ev);
        if (ev.type == KeyPress) {
            if (XLookupKeysym(&ev.xkey, 0) == XK_Escape) {
                XMoveResizeWindow(dpy, w, ox, oy, (unsigned)ow, (unsigned)oh);
                break;
            }
            continue;
        }
        if (ev.type == ButtonRelease)
            break;
        while (XCheckMaskEvent(dpy, PointerMotionMask, &ev))
            ;                                       /* compress motion */
        dx = ev.xmotion.x_root - px;
        dy = ev.xmotion.y_root - py;
        if (dir == MOVE) {
            nx = ox + dx;
            ny = oy + dy;
            XMoveWindow(dpy, w, nx, ny);
        } else {
            if (dir == SZ_TL || dir == SZ_L || dir == SZ_BL) { nx = ox + dx; nw = ow - dx; }
            if (dir == SZ_TR || dir == SZ_R || dir == SZ_BR) { nw = ow + dx; }
            if (dir == SZ_TL || dir == SZ_T || dir == SZ_TR) { ny = oy + dy; nh = oh - dy; }
            if (dir == SZ_BL || dir == SZ_B || dir == SZ_BR) { nh = oh + dy; }
            if (nw < 80) { if (nx != ox) nx = ox + ow - 80; nw = 80; }
            if (nh < 40) { if (ny != oy) ny = oy + oh - 40; nh = 40; }
            XMoveResizeWindow(dpy, w, nx, ny, (unsigned)nw, (unsigned)nh);
        }
        XFlush(dpy);
    }
    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cur);
    XSync(dpy, False);
}

/* ---- advertising ---- */
static void
advertise(void)
{
    Atom supported[] = { A_SUPPORTED, A_CHECK, A_STATE, A_FULLSCREEN, A_MOVERESIZE };
    char *name = "4Dwm";

    check = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, check, A_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(dpy, check, A_WMNAME, A_UTF8, 8, PropModeReplace, (unsigned char *)name, (int)strlen(name));
    XChangeProperty(dpy, root, A_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(dpy, root, A_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported,
                    (int)(sizeof(supported) / sizeof(supported[0])));
}

static void
unadvertise(void)
{
    XDeleteProperty(dpy, root, A_SUPPORTED);
    XDeleteProperty(dpy, root, A_CHECK);
    XDestroyWindow(dpy, check);
    XSync(dpy, False);
}

static volatile int quit;
static void onsig(int s) { (void)s; quit = 1; }

/* A window that reparented away from root has been managed: apply the state it
 * asked for before mapping, once 4Dwm has finished framing it. */
#define MAXPEND 32
static Window pend[MAXPEND];
static struct timeval pend_t[MAXPEND];
static int npend;

static void
queue_check(Window w)
{
    int i;

    for (i = 0; i < npend; i++)
        if (pend[i] == w)
            return;
    if (npend < MAXPEND) {
        pend[npend] = w;
        gettimeofday(&pend_t[npend], NULL);
        npend++;
    }
}

static void
run_pending(void)
{
    struct timeval now;
    int i;

    gettimeofday(&now, NULL);
    for (i = 0; i < npend; ) {
        long ms = (now.tv_sec - pend_t[i].tv_sec) * 1000 + (now.tv_usec - pend_t[i].tv_usec) / 1000;

        if (ms < 250) {
            i++;
            continue;
        }
        if (has_fullscreen(pend[i]))
            fullscreen_on(pend[i]);
        pend[i] = pend[--npend];
        pend_t[i] = pend_t[npend];
    }
}

int
main(void)
{
    int fd;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "ewmhd: cannot open display\n");
        return 1;
    }
    XSetErrorHandler(xerr);
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    sw = DisplayWidth(dpy, scr);
    sh = DisplayHeight(dpy, scr);
    A_SUPPORTED = XInternAtom(dpy, "_NET_SUPPORTED", False);
    A_CHECK = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    A_WMNAME = XInternAtom(dpy, "_NET_WM_NAME", False);
    A_UTF8 = XInternAtom(dpy, "UTF8_STRING", False);
    A_STATE = XInternAtom(dpy, "_NET_WM_STATE", False);
    A_FULLSCREEN = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    A_MOVERESIZE = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
    A_MOTIF = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    A_WMSTATE_LEGACY = XInternAtom(dpy, "WM_STATE", False);
    (void)A_WMSTATE_LEGACY;

    advertise();
    /* ClientMessages that clients send to the root reach everyone who selected
     * SubstructureNotify; 4Dwm owns SubstructureRedirect and we do not need it. */
    XSelectInput(dpy, root, SubstructureNotifyMask);
    fd = ConnectionNumber(dpy);
    signal(SIGINT, onsig);
    signal(SIGTERM, onsig);
    fprintf(stderr, "ewmhd: running on screen %d (%dx%d)\n", scr, sw, sh);

    while (!quit) {
        XEvent ev;
        fd_set rd;
        struct timeval tv = { 0, 100000 };

        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case ClientMessage:
                if (ev.xclient.message_type == A_STATE) {
                    Window w = ev.xclient.window;
                    long action = ev.xclient.data.l[0];
                    int i, isfs = 0;

                    for (i = 1; i <= 2; i++)
                        if ((Atom)ev.xclient.data.l[i] == A_FULLSCREEN)
                            isfs = 1;
                    if (isfs) {
                        int cur = find(w, 0) && find(w, 0)->fullscreen;

                        if (action == 1 || (action == 2 && !cur))
                            fullscreen_on(w);
                        else if (action == 0 || (action == 2 && cur))
                            fullscreen_off(w);
                    }
                } else if (ev.xclient.message_type == A_MOVERESIZE) {
                    drag(ev.xclient.window, (int)ev.xclient.data.l[0], (int)ev.xclient.data.l[1],
                         (int)ev.xclient.data.l[2]);
                }
                break;
            case ReparentNotify:
                if (ev.xreparent.parent != root)
                    queue_check(ev.xreparent.window);
                break;
            case DestroyNotify:
                forget(ev.xdestroywindow.window);
                break;
            default:
                break;
            }
        }
        run_pending();
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        select(fd + 1, &rd, NULL, NULL, &tv);
    }
    /* put everything back the way the apps had it */
    {
        Saved *s;

        for (s = saved; s; s = s->next)
            if (s->fullscreen)
                fullscreen_off(s->win);
    }
    unadvertise();
    XCloseDisplay(dpy);
    return 0;
}
