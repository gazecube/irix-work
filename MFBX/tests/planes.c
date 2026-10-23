#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <unistd.h>

static Window mk8(Display *d, VisualID vid, int x, int y, int w, int h, unsigned long bgpix_rgb, int transparent_bg, Colormap *cmo)
{
    XVisualInfo tmpl, *vi; int n;
    XSetWindowAttributes a; XColor c; Window win;
    tmpl.visualid = vid;
    vi = XGetVisualInfo(d, VisualIDMask, &tmpl, &n);
    if (!vi) { fprintf(stderr, "no visual %lx\n", vid); return 0; }
    a.colormap = XCreateColormap(d, DefaultRootWindow(d), vi->visual, AllocNone);
    if (transparent_bg) a.background_pixel = 0;
    else { c.red = (bgpix_rgb >> 16 & 0xff) * 257; c.green = (bgpix_rgb >> 8 & 0xff) * 257; c.blue = (bgpix_rgb & 0xff) * 257; XAllocColor(d, a.colormap, &c); a.background_pixel = c.pixel; }
    a.border_pixel = 0; a.override_redirect = True;
    win = XCreateWindow(d, DefaultRootWindow(d), x, y, w, h, 0, 8, InputOutput, vi->visual, CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect, &a);
    XMapWindow(d, win);
    XInstallColormap(d, a.colormap);
    *cmo = a.colormap;
    XFree(vi);
    return win;
}

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    Atom prop = XInternAtom(d, "SERVER_OVERLAY_VISUALS", False), type; int fmt; unsigned long n, after; unsigned char *data = NULL;
    long *v; VisualID ovl = 0, popup = 0, under = 0; unsigned long i;
    Window img, w; Colormap cm; GC g; XColor c;
    XSetWindowAttributes a;
    if (!d) return 1;
    XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 64, False, prop, &type, &fmt, &n, &after, &data);
    v = (long *) data;
    for (i = 0; i + 3 < n; i += 4) {
        printf("plane visual %lx type %ld value %ld layer %ld\n", v[i], v[i+1], v[i+2], v[i+3]);
        if (v[i+3] == 1) ovl = v[i]; else if (v[i+3] == 2) popup = v[i]; else if (v[i+3] < 0) under = v[i];
    }
    fflush(stdout);
    /* image plane: green window with a magenta key patch */
    a.background_pixel = 0x20a040; a.override_redirect = True; a.border_pixel = 0;
    img = XCreateWindow(d, DefaultRootWindow(d), 50, 50, 500, 350, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWBorderPixel | CWOverrideRedirect, &a);
    XMapWindow(d, img);
    g = XCreateGC(d, img, 0, NULL);
    XSetForeground(d, g, 0xff00ff); XFillRectangle(d, img, g, 350, 50, 100, 100);   /* abs 400..500 x 100..200 */
    /* underlay: orange, partly behind the key patch */
    w = mk8(d, under, 450, 120, 150, 150, 0xff8000, 0, &cm);
    /* overlay: TRANSPARENT background with a red and a blue rectangle */
    w = mk8(d, ovl, 300, 200, 400, 300, 0, 1, &cm);
    c.red = 0xffff; c.green = 0; c.blue = 0; XAllocColor(d, cm, &c);
    g = XCreateGC(d, w, 0, NULL); XSetForeground(d, g, c.pixel); XFillRectangle(d, w, g, 20, 20, 150, 100);
    c.red = 0; c.green = 0; c.blue = 0xffff; XAllocColor(d, cm, &c); XSetForeground(d, g, c.pixel); XFillRectangle(d, w, g, 250, 150, 100, 100);
    /* popup: cyan, above the overlay */
    w = mk8(d, popup, 350, 250, 160, 100, 0x00ffff, 0, &cm);
    XFlush(d);
    for (;;) { XEvent e; while (XPending(d)) XNextEvent(d, &e); usleep(100000); }
}
