#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom intern_atom(Display *dpy, const char *name)
{
    return XInternAtom(dpy, name, False);
}

static void set_desktop_properties(Display *dpy, Window win)
{
    Atom wm_type = intern_atom(dpy, "_NET_WM_WINDOW_TYPE");
    Atom desktop_type = intern_atom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP");
    Atom utf8 = intern_atom(dpy, "UTF8_STRING");
    Atom net_name = intern_atom(dpy, "_NET_WM_NAME");
    const char *name = "Indigo Magic Desktop";

    XChangeProperty(dpy, win, wm_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&desktop_type, 1);

    XStoreName(dpy, win, name);
    XChangeProperty(dpy, win, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char *)name, (int)strlen(name));
}

static int desktop_object_rects(XRectangle *out, int cap)
{
    static const XRectangle objects[] = {
        { 18, 20, 96, 66 },
        { 18, 96, 96, 66 }
    };
    int n = (int)(sizeof(objects) / sizeof(objects[0]));

    if (n > cap)
        n = cap;
    memcpy(out, objects, (size_t)n * sizeof(objects[0]));
    return n;
}

static void rebuild_desktop_shape(Display *dpy, Window win)
{
    XRectangle objects[16];
    int n = desktop_object_rects(objects, 16);

    /*
     * IRIX fm BackLayout::shapeAllViews() makes the background ViewPanel's
     * ShapeBounding region empty and unions each desktop view back into it.
     * For these static prototype objects, setting the final rectangle set in
     * one operation is equivalent: empty workspace is no longer part of the
     * desktop child window at all.
     */
    XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0,
                            objects, n, ShapeSet, YXBanded);

    /* Keep input constrained to the same temporary object rectangles. */
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0,
                            objects, n, ShapeSet, YXBanded);
}

static void draw_icon(Display *dpy, Window win, GC gc, int x, int y,
                      const char *label)
{
    int screen = DefaultScreen(dpy);

    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XFillRectangle(dpy, win, gc, x, y, 42, 34);

    XSetForeground(dpy, gc, BlackPixel(dpy, screen));
    XDrawRectangle(dpy, win, gc, x, y, 42, 34);
    XDrawLine(dpy, win, gc, x + 8, y + 9, x + 34, y + 9);
    XDrawLine(dpy, win, gc, x + 8, y + 15, x + 34, y + 15);
    XDrawLine(dpy, win, gc, x + 8, y + 21, x + 28, y + 21);
    XDrawString(dpy, win, gc, x, y + 50, label, (int)strlen(label));
}

static void redraw(Display *dpy, Window win, GC gc)
{
    draw_icon(dpy, win, gc, 24, 28, "Home");
    draw_icon(dpy, win, gc, 24, 104, "Dumpster");
}

int main(void)
{
    Display *dpy;
    int screen;
    Window root;
    Window win;
    XSetWindowAttributes attrs;
    XEvent ev;
    GC gc;
    Colormap cmap;
    XColor exact, color;
    unsigned long bg;
    unsigned int width, height;
    int shape_event_base, shape_error_base;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "iid-desktop: cannot open display\n");
        return 1;
    }

    if (!XShapeQueryExtension(dpy, &shape_event_base, &shape_error_base)) {
        fprintf(stderr, "iid-desktop: X Shape extension is required\n");
        XCloseDisplay(dpy);
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    width = (unsigned int)DisplayWidth(dpy, screen);
    height = (unsigned int)DisplayHeight(dpy, screen);

    cmap = DefaultColormap(dpy, screen);
    bg = BlackPixel(dpy, screen);
    if (XAllocNamedColor(dpy, cmap, "#4f718a", &color, &exact))
        bg = color.pixel;

    /*
     * The visible workspace background belongs to the root window. The fm
     * desktop child is shape-bounded around desktop objects instead of being
     * an opaque full-screen window. This matches the BackLayout behavior now
     * recovered from the IRIX binary and naturally preserves 5Dwm root-menu
     * handling in empty workspace.
     */
    XSetWindowBackground(dpy, root, bg);
    XClearWindow(dpy, root);

    attrs.background_pixel = bg;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;

    /* 4Dwm/5Dwm must never reparent or decorate the fm desktop surface. */
    attrs.override_redirect = True;

    win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWEventMask | CWOverrideRedirect,
                        &attrs);

    set_desktop_properties(dpy, win);
    rebuild_desktop_shape(dpy, win);
    XMapWindow(dpy, win);

    /* Keep desktop objects underneath normal application windows. */
    XLowerWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    XFlush(dpy);

    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.count == 0)
            redraw(dpy, win, gc);
    }

    return 0;
}
