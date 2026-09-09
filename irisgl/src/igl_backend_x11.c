#include "igl_backend.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xpresent.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gl/device.h>

struct igl_backend_window {
    Window xwindow;
    GC gc;
    unsigned long pixel;
    Pixmap backbuffer;
    int back_width;
    int back_height;
    int doublebuffered;
    XID present_event_id;
    Pixmap present_pixmaps[2];
    int present_width;
    int present_height;
    int present_front;
    int present_pending;
    int gsync_present_mode;
};

struct igl_backend {
    Display *display;
    int screen;
    long debug_clear_delay_ms;
    int present_available;
    int present_opcode;
    uint32_t present_serial;
};

struct igl_present_wait {
    int opcode;
};

static Bool igl_backend_present_event(Display *display,
                                      XEvent *event,
                                      XPointer arg)
{
    struct igl_present_wait *wait = (struct igl_present_wait *)arg;

    (void)display;

    return event->type == GenericEvent &&
           event->xcookie.extension == wait->opcode &&
           (event->xcookie.evtype == PresentCompleteNotify ||
            event->xcookie.evtype == PresentIdleNotify);
}

static long igl_backend_debug_delay_ms(void)
{
    const char *value;
    char *end;
    long delay;

    value = getenv("IRISGL_DEBUG_CLEAR_DELAY");
    if (!value || !*value)
        return 0;

    delay = strtol(value, &end, 10);
    if (*end != '\0' || delay <= 0)
        return 0;

    return delay;
}

static int igl_backend_ensure_backbuffer(struct igl_backend *backend,
                                         struct igl_backend_window *window,
                                         int width,
                                         int height)
{
    Pixmap pixmap;

    if (!backend || !window || width <= 0 || height <= 0)
        return 0;

    if (window->backbuffer &&
        window->back_width == width &&
        window->back_height == height)
        return 1;

    pixmap = XCreatePixmap(backend->display, window->xwindow,
                           (unsigned int)width, (unsigned int)height,
                           (unsigned int)DefaultDepth(backend->display,
                                                      backend->screen));
    if (!pixmap)
        return 0;

    if (window->backbuffer)
        XFreePixmap(backend->display, window->backbuffer);

    window->backbuffer = pixmap;
    window->back_width = width;
    window->back_height = height;
    return 1;
}

static int igl_backend_ensure_present_pixmaps(struct igl_backend *backend,
                                              struct igl_backend_window *window,
                                              int width,
                                              int height)
{
    Pixmap first;
    Pixmap second;
    unsigned int depth;

    if (!backend || !window || width <= 0 || height <= 0)
        return 0;

    if (window->present_pixmaps[0] && window->present_pixmaps[1] &&
        window->present_width == width &&
        window->present_height == height)
        return 1;

    depth = (unsigned int)DefaultDepth(backend->display, backend->screen);
    first = XCreatePixmap(backend->display, window->xwindow,
                          (unsigned int)width, (unsigned int)height, depth);
    if (!first)
        return 0;

    second = XCreatePixmap(backend->display, window->xwindow,
                           (unsigned int)width, (unsigned int)height, depth);
    if (!second) {
        XFreePixmap(backend->display, first);
        return 0;
    }

    if (window->present_pixmaps[0])
        XFreePixmap(backend->display, window->present_pixmaps[0]);
    if (window->present_pixmaps[1])
        XFreePixmap(backend->display, window->present_pixmaps[1]);

    window->present_pixmaps[0] = first;
    window->present_pixmaps[1] = second;
    window->present_width = width;
    window->present_height = height;
    window->present_front = -1;
    window->present_pending = -1;
    return 1;
}

static uint32_t igl_backend_next_present_serial(struct igl_backend *backend)
{
    uint32_t serial;

    serial = ++backend->present_serial;
    if (serial == 0)
        serial = ++backend->present_serial;
    return serial;
}

struct igl_backend *igl_backend_create(void)
{
    struct igl_backend *backend;
    int event_base;
    int error_base;

    backend = calloc(1, sizeof(*backend));
    if (!backend)
        return NULL;

    backend->display = XOpenDisplay(NULL);
    if (!backend->display) {
        free(backend);
        return NULL;
    }

    backend->screen = DefaultScreen(backend->display);
    backend->debug_clear_delay_ms = igl_backend_debug_delay_ms();
    backend->present_available = XPresentQueryExtension(
        backend->display, &backend->present_opcode, &event_base, &error_base);
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

    window->present_front = -1;
    window->present_pending = -1;

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

    if (backend->present_available) {
        window->present_event_id = XPresentSelectInput(
            backend->display, window->xwindow,
            PresentCompleteNotifyMask | PresentIdleNotifyMask);
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
    if (window->present_event_id)
        XPresentFreeInput(backend->display, window->xwindow,
                          window->present_event_id);
    if (window->present_pixmaps[0])
        XFreePixmap(backend->display, window->present_pixmaps[0]);
    if (window->present_pixmaps[1])
        XFreePixmap(backend->display, window->present_pixmaps[1]);
    if (window->backbuffer)
        XFreePixmap(backend->display, window->backbuffer);
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

int igl_backend_set_doublebuffer(struct igl_backend *backend,
                                 struct igl_backend_window *window,
                                 int enabled)
{
    XWindowAttributes attrs;

    if (!backend || !window)
        return 0;

    if (!enabled) {
        window->doublebuffered = 0;
        if (window->backbuffer) {
            XFreePixmap(backend->display, window->backbuffer);
            window->backbuffer = 0;
            window->back_width = 0;
            window->back_height = 0;
        }
        return 1;
    }

    if (!XGetWindowAttributes(backend->display, window->xwindow, &attrs))
        return 0;

    if (!igl_backend_ensure_backbuffer(backend, window,
                                       attrs.width, attrs.height))
        return 0;

    window->doublebuffered = 1;
    return 1;
}

void igl_backend_clear(struct igl_backend *backend,
                       struct igl_backend_window *window)
{
    XWindowAttributes attrs;
    Drawable drawable;
    int present_index;

    if (!backend || !window)
        return;

    if (!XGetWindowAttributes(backend->display, window->xwindow, &attrs))
        return;

    drawable = window->xwindow;
    if (window->doublebuffered) {
        if (!igl_backend_ensure_backbuffer(backend, window,
                                           attrs.width, attrs.height))
            return;
        drawable = window->backbuffer;
    } else if (window->gsync_present_mode && backend->present_available &&
               window->present_event_id) {
        if (!igl_backend_ensure_present_pixmaps(backend, window,
                                                attrs.width, attrs.height))
            return;

        present_index = window->present_front == 0 ? 1 : 0;
        drawable = window->present_pixmaps[present_index];
        window->present_pending = present_index;
    }

    XFillRectangle(backend->display, drawable, window->gc,
                   0, 0,
                   (unsigned int)attrs.width,
                   (unsigned int)attrs.height);

    if (backend->debug_clear_delay_ms > 0) {
        struct timespec delay;

        XSync(backend->display, False);
        delay.tv_sec = backend->debug_clear_delay_ms / 1000;
        delay.tv_nsec = (backend->debug_clear_delay_ms % 1000) * 1000000L;
        nanosleep(&delay, NULL);
    } else {
        XFlush(backend->display);
    }
}

void igl_backend_swapbuffers(struct igl_backend *backend,
                             struct igl_backend_window *window)
{
    XWindowAttributes attrs;

    if (!backend || !window || !window->doublebuffered)
        return;

    if (!XGetWindowAttributes(backend->display, window->xwindow, &attrs))
        return;

    if (!igl_backend_ensure_backbuffer(backend, window,
                                       attrs.width, attrs.height))
        return;

    XCopyArea(backend->display,
              window->backbuffer,
              window->xwindow,
              window->gc,
              0, 0,
              (unsigned int)attrs.width,
              (unsigned int)attrs.height,
              0, 0);
    XFlush(backend->display);
}

static void igl_backend_wait_next_msc(struct igl_backend *backend,
                                      struct igl_backend_window *window)
{
    struct igl_present_wait wait;
    XEvent event;
    uint32_t serial;

    serial = igl_backend_next_present_serial(backend);
    XPresentNotifyMSC(backend->display, window->xwindow,
                      serial, 0, 1, 0);
    XFlush(backend->display);

    wait.opcode = backend->present_opcode;
    for (;;) {
        XPresentCompleteNotifyEvent *complete;

        XIfEvent(backend->display, &event,
                 igl_backend_present_event, (XPointer)&wait);
        if (!XGetEventData(backend->display, &event.xcookie))
            continue;

        if (event.xcookie.evtype != PresentCompleteNotify) {
            XFreeEventData(backend->display, &event.xcookie);
            continue;
        }

        complete = (XPresentCompleteNotifyEvent *)event.xcookie.data;
        if (complete &&
            complete->kind == PresentCompleteKindNotifyMSC &&
            complete->serial_number == serial &&
            complete->window == window->xwindow) {
            XFreeEventData(backend->display, &event.xcookie);
            break;
        }

        XFreeEventData(backend->display, &event.xcookie);
    }
}

static void igl_backend_present_pending(struct igl_backend *backend,
                                        struct igl_backend_window *window)
{
    struct igl_present_wait wait;
    XEvent event;
    uint32_t serial;
    Pixmap pixmap;
    Pixmap old_pixmap = None;
    int old_front;
    int complete_seen = 0;
    int old_idle_seen;

    old_front = window->present_front;
    if (old_front >= 0)
        old_pixmap = window->present_pixmaps[old_front];
    old_idle_seen = old_front < 0;

    pixmap = window->present_pixmaps[window->present_pending];
    serial = igl_backend_next_present_serial(backend);

    XPresentPixmap(backend->display,
                   window->xwindow,
                   pixmap,
                   serial,
                   None,
                   None,
                   0,
                   0,
                   None,
                   None,
                   None,
                   0,
                   0,
                   1,
                   0,
                   NULL,
                   0);
    XFlush(backend->display);

    wait.opcode = backend->present_opcode;
    while (!complete_seen || !old_idle_seen) {
        XIfEvent(backend->display, &event,
                 igl_backend_present_event, (XPointer)&wait);
        if (!XGetEventData(backend->display, &event.xcookie))
            continue;

        if (event.xcookie.evtype == PresentCompleteNotify) {
            XPresentCompleteNotifyEvent *complete;

            complete = (XPresentCompleteNotifyEvent *)event.xcookie.data;
            if (complete &&
                complete->kind == PresentCompleteKindPixmap &&
                complete->serial_number == serial &&
                complete->window == window->xwindow)
                complete_seen = 1;
        } else if (event.xcookie.evtype == PresentIdleNotify && old_front >= 0) {
            XPresentIdleNotifyEvent *idle;

            idle = (XPresentIdleNotifyEvent *)event.xcookie.data;
            if (idle && idle->window == window->xwindow &&
                idle->pixmap == old_pixmap)
                old_idle_seen = 1;
        }

        XFreeEventData(backend->display, &event.xcookie);
    }

    window->present_front = window->present_pending;
    window->present_pending = -1;
}

void igl_backend_gsync(struct igl_backend *backend,
                       struct igl_backend_window *window)
{
    if (!backend || !backend->display || !window)
        return;

    if (!backend->present_available || !window->present_event_id) {
        /*
         * Fallback only.  This preserves command completion on X servers
         * without Present, but does not emulate IRIS GL vertical retrace.
         */
        XSync(backend->display, False);
        return;
    }

    /*
     * The first gsync() establishes that this single-buffered application
     * is explicitly retrace-synchronized.  Subsequent drawing is staged in
     * alternating Present pixmaps so we never modify the pixmap currently
     * being scanned out.  PresentPixmap then makes that completed frame
     * visible on the next MSC field.  Applications which never call gsync()
     * retain the ordinary immediate single-buffer path.
     */
    if (!window->doublebuffered && window->present_pending >= 0) {
        igl_backend_present_pending(backend, window);
        return;
    }

    igl_backend_wait_next_msc(backend, window);
    if (!window->doublebuffered)
        window->gsync_present_mode = 1;
}

int igl_backend_poll_event(struct igl_backend *backend,
                           struct igl_backend_window **window,
                           long *device,
                           short *value,
                           int block)
{
    XEvent event;

    if (!backend || !backend->display)
        return 0;

    for (;;) {
        if (!block && XPending(backend->display) == 0)
            return 0;

        XNextEvent(backend->display, &event);

        switch (event.type) {
        case Expose:
        case ConfigureNotify:
            if (window)
                *window = NULL;
            if (device)
                *device = REDRAW;
            if (value)
                *value = 0;
            return 1;
        default:
            if (!block && XPending(backend->display) == 0)
                return 0;
            break;
        }
    }
}
