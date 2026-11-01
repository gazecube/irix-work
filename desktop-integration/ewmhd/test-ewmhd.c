/* Drives ewmhd the way real apps do: fullscreen add/remove, then a _NET_WM_MOVERESIZE drag
 * (button down, ungrab, ask, motion, release) with XTest.  Prints PASS/FAIL per check. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static Display *d; static Window root, w; static int fails;
static void ok(int c, const char *m) { printf("%s  %s\n", c ? "PASS" : "FAIL", m); if (!c) fails++; }
static void pos(int *x, int *y, int *wd, int *ht) {
    Window c; XWindowAttributes a; *x = *y = 0;
    XTranslateCoordinates(d, w, root, 0, 0, x, y, &c);
    XGetWindowAttributes(d, w, &a); *wd = a.width; *ht = a.height;
}
static void pause_ms(int ms) { XSync(d, False); usleep(ms * 1000); }
static void state_msg(long action, const char *atom) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = w;
    e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False); e.xclient.format = 32;
    e.xclient.data.l[0] = action; e.xclient.data.l[1] = (long)XInternAtom(d, atom, False); e.xclient.data.l[3] = 1;
    XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e);
}

/* press at (px,py), ask for a drag in direction dir, move the pointer by (dx,dy), release */
static void run_drag(int dir, int px, int py, int dx, int dy) {
    int i;
    XTestFakeMotionEvent(d, 0, px, py, 0); pause_ms(150);
    XTestFakeButtonEvent(d, 1, True, 0); pause_ms(150);
    XUngrabPointer(d, CurrentTime);
    { XEvent e; memset(&e, 0, sizeof e);
      e.xclient.type = ClientMessage; e.xclient.window = w;
      e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False); e.xclient.format = 32;
      e.xclient.data.l[0] = px; e.xclient.data.l[1] = py; e.xclient.data.l[2] = dir; e.xclient.data.l[3] = 1; e.xclient.data.l[4] = 1;
      XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e); }
    pause_ms(200);
    for (i = 1; i <= 10; i++) { XTestFakeMotionEvent(d, 0, px + dx * i / 10, py + dy * i / 10, 0); pause_ms(40); }
    pause_ms(300);
    XTestFakeButtonEvent(d, 1, False, 0); pause_ms(400);
}
int main(void) {
    int x, y, wd, ht, sw, sh, i, found = 0;
    Atom type, *sup; int fmt; unsigned long n, after; unsigned char *data = NULL;
    unsigned long hints[5] = { 2, 0, 0, 0, 0 };            /* frameless: no decorations */
    d = XOpenDisplay(NULL); if (!d) { puts("no display"); return 2; }
    root = DefaultRootWindow(d); sw = DisplayWidth(d, 0); sh = DisplayHeight(d, 0);
    if (XGetWindowProperty(d, root, XInternAtom(d, "_NET_SUPPORTED", False), 0, 64, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
        sup = (Atom *)data;
        for (i = 0; i < (int)n; i++) if (sup[i] == XInternAtom(d, "_NET_WM_MOVERESIZE", False)) found++;
        for (i = 0; i < (int)n; i++) if (sup[i] == XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False)) found++;
    }
    ok(found == 2, "_NET_SUPPORTED lists fullscreen and moveresize");
    w = XCreateSimpleWindow(d, root, 100, 100, 300, 200, 0, 0, 0xcc5500);
    XSelectInput(d, w, StructureNotifyMask | ButtonPressMask);
    XChangeProperty(d, w, XInternAtom(d, "_MOTIF_WM_HINTS", False), XInternAtom(d, "_MOTIF_WM_HINTS", False), 32, PropModeReplace, (unsigned char *)hints, 5);
    XMapWindow(d, w); pause_ms(1200);
    pos(&x, &y, &wd, &ht); printf("start: %d,%d %dx%d\n", x, y, wd, ht);
    state_msg(1, "_NET_WM_STATE_FULLSCREEN"); pause_ms(900);
    pos(&x, &y, &wd, &ht); printf("fullscreen: %d,%d %dx%d (screen %dx%d)\n", x, y, wd, ht, sw, sh);
    ok(x == 0 && y == 0 && wd == sw && ht == sh, "fullscreen: window covers the screen");
    state_msg(0, "_NET_WM_STATE_FULLSCREEN"); pause_ms(900);
    pos(&x, &y, &wd, &ht); printf("restored: %d,%d %dx%d\n", x, y, wd, ht);
    ok(wd == 300 && ht == 200 && abs(x - 100) <= 4 && abs(y - 100) <= 4, "leaving fullscreen restores size and place");
    /* a drag, the way Chromium/GTK do it */
    {
        int px = x + 50, py = y + 20, bx, by, bw, bh;
        XTestFakeMotionEvent(d, 0, px, py, 0); pause_ms(200);
        XTestFakeButtonEvent(d, 1, True, 0); pause_ms(200);
        XUngrabPointer(d, CurrentTime);
        { XEvent e; memset(&e, 0, sizeof e);
          e.xclient.type = ClientMessage; e.xclient.window = w;
          e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False); e.xclient.format = 32;
          e.xclient.data.l[0] = px; e.xclient.data.l[1] = py; e.xclient.data.l[2] = 8; e.xclient.data.l[3] = 1; e.xclient.data.l[4] = 1;
          XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e); }
        pause_ms(300);
        for (i = 1; i <= 10; i++) { XTestFakeMotionEvent(d, 0, px + 12 * i, py + 8 * i, 0); pause_ms(30); }
        XTestFakeButtonEvent(d, 1, False, 0); pause_ms(500);
        pos(&bx, &by, &bw, &bh); printf("after drag: %d,%d (expected %d,%d)\n", bx, by, x + 120, y + 80);
        ok(abs(bx - (x + 120)) <= 4 && abs(by - (y + 80)) <= 4, "_NET_WM_MOVERESIZE move follows the pointer");
    }
    /* a fast, long drag (a mouse moving at high rate): the window must end exactly under the pointer */
    pos(&x, &y, &wd, &ht);
    {
        int px = x + 50, py = y + 20, bx, by, bw, bh;
        XTestFakeMotionEvent(d, 0, px, py, 0); pause_ms(200);
        XTestFakeButtonEvent(d, 1, True, 0); pause_ms(200);
        XUngrabPointer(d, CurrentTime);
        { XEvent e; memset(&e, 0, sizeof e);
          e.xclient.type = ClientMessage; e.xclient.window = w;
          e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False); e.xclient.format = 32;
          e.xclient.data.l[0] = px; e.xclient.data.l[1] = py; e.xclient.data.l[2] = 8; e.xclient.data.l[3] = 1; e.xclient.data.l[4] = 1;
          XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e); }
        pause_ms(200);
        for (i = 1; i <= 240; i++) { XTestFakeMotionEvent(d, 0, px + 3 * i, py + 2 * i, 0); XFlush(d); usleep(1500); }
        pause_ms(250);                                              /* settle with the button still down */
        pos(&bx, &by, &bw, &bh); printf("fast drag, button held: %d,%d (pointer-anchored target %d,%d)\n", bx, by, x + 720, y + 480);
        ok(abs(bx - (x + 720)) <= 2 && abs(by - (y + 480)) <= 2, "fast drag: window ends exactly under the pointer");
        XTestFakeButtonEvent(d, 1, False, 0); pause_ms(400);
        x = bx; y = by;
        XTestFakeMotionEvent(d, 0, 640, 500, 0); pause_ms(150);
        { int ax, ay, aw, ah; pos(&ax, &ay, &aw, &ah); ok(ax == bx && ay == by, "after release the window stays put"); }
    }
    /* the button is already up when the request arrives (fast clicks, Electron): the
     * daemon must not start a drag that waits for a release that never comes */
    {
        int px = x + 50, py = y + 20, bx, by, bw, bh, ax, ay, aw, ah;
        pos(&ax, &ay, &aw, &ah);
        XTestFakeMotionEvent(d, 0, px, py, 0); pause_ms(150);
        XTestFakeButtonEvent(d, 1, True, 0); pause_ms(100);
        XTestFakeButtonEvent(d, 1, False, 0); pause_ms(150);        /* released before the request */
        { XEvent e; memset(&e, 0, sizeof e);
          e.xclient.type = ClientMessage; e.xclient.window = w;
          e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False); e.xclient.format = 32;
          e.xclient.data.l[0] = px; e.xclient.data.l[1] = py; e.xclient.data.l[2] = 8; e.xclient.data.l[3] = 1; e.xclient.data.l[4] = 1;
          XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e); }
        pause_ms(300);
        for (i = 1; i <= 6; i++) { XTestFakeMotionEvent(d, 0, px + 15 * i, py + 10 * i, 0); pause_ms(30); }
        pause_ms(300);
        pos(&bx, &by, &bw, &bh); printf("after stale request: %d,%d (should still be %d,%d)\n", bx, by, ax, ay);
        ok(bx == ax && by == ay, "a request after the button is already up does not start a drag");
        { Window gr = 0; (void)gr; }
        XTestFakeButtonEvent(d, 1, True, 0); XTestFakeButtonEvent(d, 1, False, 0); pause_ms(200);   /* clear any stuck grab */
    }
    /* resize from each edge/corner, the way a frameless app asks for it */
    {
        static const char *nm[] = { "top-left", "top", "top-right", "right", "bottom-right", "bottom", "bottom-left", "left" };
        int dir;
        for (dir = 0; dir < 8; dir++) {
            int ox, oy, ow, oh, nx, ny, nw, nh, px, py, ex = 0, ey = 0, ew = 0, eh = 0;
            Window gr = 0; (void)gr;
            XMoveResizeWindow(d, w, 300, 250, 300, 200); pause_ms(600);
            pos(&ox, &oy, &ow, &oh);
            /* the pointer on the edge being dragged */
            px = ox + ((dir == 0 || dir == 6 || dir == 7) ? 3 : (dir == 2 || dir == 3 || dir == 4) ? ow - 3 : ow / 2);
            py = oy + ((dir == 0 || dir == 1 || dir == 2) ? 3 : (dir == 4 || dir == 5 || dir == 6) ? oh - 3 : oh / 2);
            XTestFakeMotionEvent(d, 0, px, py, 0); pause_ms(150);
            XTestFakeButtonEvent(d, 1, True, 0); pause_ms(150);
            XUngrabPointer(d, CurrentTime);
            { XEvent e; memset(&e, 0, sizeof e);
              e.xclient.type = ClientMessage; e.xclient.window = w;
              e.xclient.message_type = XInternAtom(d, "_NET_WM_MOVERESIZE", False); e.xclient.format = 32;
              e.xclient.data.l[0] = px; e.xclient.data.l[1] = py; e.xclient.data.l[2] = dir; e.xclient.data.l[3] = 1; e.xclient.data.l[4] = 1;
              XSendEvent(d, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e); }
            pause_ms(200);
            for (i = 1; i <= 8; i++) { XTestFakeMotionEvent(d, 0, px + 10 * i, py + 6 * i, 0); pause_ms(40); }
            pause_ms(300);
            XTestFakeButtonEvent(d, 1, False, 0); pause_ms(400);
            pos(&nx, &ny, &nw, &nh);
            ex = ox; ey = oy; ew = ow; eh = oh;
            if (dir == 0 || dir == 6 || dir == 7) { ex = ox + 80; ew = ow - 80; }
            if (dir == 2 || dir == 3 || dir == 4) { ew = ow + 80; }
            if (dir == 0 || dir == 1 || dir == 2) { ey = oy + 48; eh = oh - 48; }
            if (dir == 4 || dir == 5 || dir == 6) { eh = oh + 48; }
            printf("resize %-12s: %d,%d %dx%d -> %d,%d %dx%d (expected %d,%d %dx%d)\n", nm[dir], ox, oy, ow, oh, nx, ny, nw, nh, ex, ey, ew, eh);
            ok(abs(nx - ex) <= 3 && abs(ny - ey) <= 3 && abs(nw - ew) <= 3 && abs(nh - eh) <= 3, nm[dir]);
        }
    }
    /* the laws of resize: at its minimum size a window must not slide when you push on its edge */
    {
        XSizeHints hs; int a, b, c2, d2;
        memset(&hs, 0, sizeof hs); hs.flags = PMinSize; hs.min_width = 150; hs.min_height = 100;
        XSetWMNormalHints(d, w, &hs);
        XMoveResizeWindow(d, w, 500, 300, 150, 100); pause_ms(700);
        pos(&a, &b, &c2, &d2); printf("at minimum: %d,%d %dx%d\n", a, b, c2, d2);
        run_drag(7, a + 3, b + 50, 70, 0);                               /* left edge pushed inward */
        pos(&a, &b, &c2, &d2); printf("left edge pushed in at minimum: %d,%d %dx%d\n", a, b, c2, d2);
        ok(a == 500 && c2 == 150, "left edge at minimum: window stays put");
        run_drag(1, a + 75, b + 3, 0, 50);                               /* top edge pushed inward */
        pos(&a, &b, &c2, &d2); printf("top edge pushed in at minimum: %d,%d %dx%d\n", a, b, c2, d2);
        ok(b == 300 && d2 == 100, "top edge at minimum: window stays put");
        run_drag(7, a + 3, b + 50, -40, 0);                              /* left edge pulled outward: grows */
        pos(&a, &b, &c2, &d2); printf("left edge pulled out: %d,%d %dx%d\n", a, b, c2, d2);
        ok(a == 460 && c2 == 190, "left edge pulled out: grows leftwards, right edge fixed");
        run_drag(7, a + 3, b + 50, 200, 0);                              /* pushed far in: clamps to min, right edge fixed */
        pos(&a, &b, &c2, &d2); printf("left edge pushed far in: %d,%d %dx%d\n", a, b, c2, d2);
        ok(a + c2 == 650 && c2 == 150, "left edge pushed past the minimum: right edge stays at 650");
        memset(&hs, 0, sizeof hs); hs.flags = PMinSize | PResizeInc | PBaseSize; hs.min_width = 150; hs.min_height = 100;
        hs.width_inc = 20; hs.height_inc = 20; hs.base_width = 10; hs.base_height = 0;
        XSetWMNormalHints(d, w, &hs); pause_ms(300);
        run_drag(3, a + c2 - 3, b + 50, 75, 0);                          /* right edge, increments of 20 from base 10 */
        pos(&a, &b, &c2, &d2); printf("increments: width %d\n", c2);
        ok((c2 - 10) % 20 == 0, "resize honours the app's size increments");
    }
    /* a normal, decorated window (a game that did not ask for a frame to go away) */
    XDestroyWindow(d, w); pause_ms(300);
    w = XCreateSimpleWindow(d, root, 200, 150, 320, 220, 0, 0, 0x3366cc);
    XMapWindow(d, w); pause_ms(1200);
    pos(&x, &y, &wd, &ht); printf("decorated start: %d,%d %dx%d\n", x, y, wd, ht);
    state_msg(2, "_NET_WM_STATE_FULLSCREEN"); pause_ms(1000);            /* toggle on */
    pos(&x, &y, &wd, &ht); printf("decorated fullscreen: %d,%d %dx%d\n", x, y, wd, ht);
    ok(x == 0 && y == 0 && wd == sw && ht == sh, "decorated window: frame dropped, covers the screen");
    state_msg(2, "_NET_WM_STATE_FULLSCREEN"); pause_ms(1000);            /* toggle off */
    pos(&x, &y, &wd, &ht); printf("decorated restored: %d,%d %dx%d\n", x, y, wd, ht);
    ok(wd == 320 && ht == 220, "decorated window: frame and size come back");
    XCloseDisplay(d);
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
