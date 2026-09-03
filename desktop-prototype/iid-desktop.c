#include <X11/Xlib.h>
#include <X11/Xatom.h>
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
    Atom wm_state = intern_atom(dpy, "_NET_WM_STATE");
    Atom states[3];
    Atom utf8 = intern_atom(dpy, "UTF8_STRING");
    Atom net_name = intern_atom(dpy, "_NET_WM_NAME");
    const char *name = "Indigo Magic Desktop";

    states[0] = intern_atom(dpy, "_NET_WM_STATE_BELOW");
    states[1] = intern_atom(dpy, "_NET_WM_STATE_STICKY");
    states[2] = intern_atom(dpy, "_NET_WM_STATE_SKIP_TASKBAR");

    XChangeProperty(dpy, win, wm_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&desktop_type, 1);
    XChangeProperty(dpy, win, wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 3);

    XStoreName(dpy, win, name);
    XChangeProperty(dpy, win, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char *)name, (int)strlen(name));
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

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "iid-desktop: cannot open display\n");
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

    attrs.background_pixel = bg;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;
    attrs.override_redirect = False;

    win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                        CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWEventMask | CWOverrideRedirect,
                        &attrs);

    set_desktop_properties(dpy, win);
    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    XFlush(dpy);

    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.count == 0)
            redraw(dpy, win, gc);
    }

    return 0;
}
