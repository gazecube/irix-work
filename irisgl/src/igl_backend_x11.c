#include "igl_backend.h"

#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

#include <gl/device.h>

struct igl_backend_window {
    Window xwindow;
    GC gc;
    unsigned long pixel;
};

struct igl_backend {
    Display *display;
    int screen;
};

static struct igl_backend_window *lookup_window(struct igl_backend *backend,
                                                Window xwindow)
{
    XPointer data = NULL;
    XContext context = XUniqueContext();
    (void)backend;
    (void)xwindow;
    (void)data;
    (void)context;
    return NULL;
}

struct igl_backend *igl_backend_create(void)
{
    struct igl_backend *backend;

    backend = calloc(1, sizeof(*backend));
    if (!backend)
        return NULL;

    backend->display = XOpenDisplay(NULL);
    if (!backend->display) {
        free(backend);
        return NULL;
    }

    backend->screen = DefaultScreen(backend->display);
    return backend;
}

void igl_backend_destroy(struct igl_backend *backend)
{
    if (!backend)
        return;
    if (backend->display)
        XCloseDisplay(backend->display);
    free(backend);
}

struct igl_backend_window *igl_backend_open_window(
    struct igl_backend *backend,
    const char *title,
    int x,
    int y,
    int width,
    int height,
    int have_position)
{
    struct igl_backend_window *window;
    unsigned long black;
    unsigned long white;
    XSizeHints hints;

    if (!backend || !backend->display)
        return NULL;

    window = calloc(1, sizeof(*window));
    if (!window)
        return NULL;

    black = BlackPixel(backend->display, backend->screen);
    white = WhitePixel(backend->display, backend->screen);

    window->xwindow = XCreateSimpleWindow(
        backend->display,
        RootWindow(backend->display, backend->screen),
        x,
        y,
        (unsigned int)width,
        (unsigned int)height,
        0,
        black,
        black);

    if (!window->xwindow) {
        free(window);
        return NULL;
    }

    XSelectInput(backend->display, window->xwindow,
                 ExposureMask | StructureNotifyMask);

    memset(&hints, 0, sizeof(hints));
    if (have_position) {
        hints.flags |= USPosition;
        hints.x = x;
        hints.y = y;
    }
    hints.flags |= USSize;
    hints.width = width;
    hints.height = height;
    XSetWMNormalHints(backend->display, window->xwindow, &hints);

    if (title)
        XStoreName(backend->display, window->xwindow, title);

    window->gc = XCreateGC(backend->display, window->xwindow, 0, NULL);
    if (!window->gc) {
        XDestroyWindow(backend->display, window->xwindow);
        free(window);
        return NULL;
    }

    window->pixel = white;
    XSetForeground(backend->display, window->gc, window->pixel);
    XMapWindow(backend->display, window->xwindow);
    XFlush(backend->display);

    return window;
}

void igl_backend_destroy_window(struct igl_backend *backend,
                                struct igl_backend_window *window)
{
    if (!backend || !window)
        return;
    if (window->gc)
        XFreeGC(backend->display, window->gc);
    if (window->xwindow)
        XDestroyWindow(backend->display, window->xwindow);
    free(window);
}

int igl_backend_get_geometry(struct igl_backend *backend,
                             struct igl_backend_window *window,
                             int *x,
                             int *y,
                             int *width,
                             int *height)
{
    Window root;
    int gx, gy;
    unsigned int gw, gh, border, depth;

    if (!backend || !window)
        return 0;

    if (!XGetGeometry(backend->display, window->xwindow, &root,
                      &gx, &gy, &gw, &gh, &border, &depth))
        return 0;

    if (x) *x = gx;
    if (y) *y = gy;
    if (width) *width = (int)gw;
    if (height) *height = (int)gh;
    return 1;
}

void igl_backend_set_rgb(struct igl_backend *backend,
                         struct igl_backend_window *window,
                         int16_t red,
                         int16_t green,
                         int16_t blue)
{
    XColor color;
    Colormap cmap;

    if (!backend || !window)
        return;

    color.red = (unsigned short)((unsigned short)red * 257u);
    color.green = (unsigned short)((unsigned short)green * 257u);
    color.blue = (unsigned short)((unsigned short)blue * 257u);
    color.flags = DoRed | DoGreen | DoBlue;

    cmap = DefaultColormap(backend->display, backend->screen);
    if (XAllocColor(backend->display, cmap, &color)) {
        window->pixel = color.pixel;
        XSetForeground(backend->display, window->gc, window->pixel);
    }
}

void igl_backend_clear(struct igl_backend *backend,
                       struct igl_backend_window *window)
{
    XWindowAttributes attrs;

    if (!backend || !window)
        return;

    if (!XGetWindowAttributes(backend->display, window->xwindow, &attrs))
        return;

    XFillRectangle(backend->display, window->xwindow, window->gc,
                   0, 0,
                   (unsigned int)attrs.width,
                   (unsigned int)attrs.height);
    XFlush(backend->display);
}

int igl_backend_poll_event(struct igl_backend *backend,
                           struct igl_backend_window **window,
                           long *device,
                           short *value,
                           int block)
{
    XEvent event;
    static struct igl_backend_window event_window;

    if (!backend || !backend->display)
        return 0;

    if (!block && XPending(backend->display) == 0)
        return 0;

    if (block)
        XNextEvent(backend->display, &event);
    else
        XNextEvent(backend->display, &event);

    switch (event.type) {
    case Expose:
    case ConfigureNotify:
        memset(&event_window, 0, sizeof(event_window));
        event_window.xwindow = event.xany.window;
        if (window)
            *window = &event_window;
        if (device)
            *device = REDRAW;
        if (value)
            *value = 0;
        return 1;
    default:
        return 0;
    }
}
