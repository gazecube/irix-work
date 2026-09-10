#include "igl_backend.h"

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xpresent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/dri3.h>
#include <xcb/xcb.h>

/*
 * Reconstructed host presentation bridge. This is not an original SGI API.
 *
 * These private layouts intentionally mirror igl_backend_x11.c so this first
 * DRI3 proof can remain isolated from the existing, already-passing X11
 * backend. Once the zero-copy path is proven on hardware, move these layouts
 * into a shared private backend header rather than allowing them to diverge.
 */
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

struct igl_dri3_surface {
    struct igl_backend_window *window;
    uint32_t token;
    Pixmap pixmap;
    int width;
    int height;
    int stride;
    struct igl_dri3_surface *next;
};

struct igl_dri3_window_state {
    struct igl_backend_window *window;
    Pixmap front;
    struct igl_dri3_window_state *next;
};

struct igl_dri3_present_wait {
    int opcode;
};

static struct igl_dri3_surface *igl_dri3_surfaces;
static struct igl_dri3_window_state *igl_dri3_windows;
static int igl_dri3_reported_success;
static int igl_dri3_reported_failure;

static int igl_dri3_debug_enabled(void)
{
    const char *value = getenv("IRISGL_DEBUG_DRI3");
    return value && *value && *value != '0';
}

static uint32_t igl_dri3_next_serial(struct igl_backend *backend)
{
    uint32_t serial;

    serial = ++backend->present_serial;
    if (serial == 0)
        serial = ++backend->present_serial;
    return serial;
}

static void igl_dri3_report_failure(const char *reason)
{
    if (!igl_dri3_debug_enabled() || igl_dri3_reported_failure)
        return;
    fprintf(stderr, "IRISGL DRI3: fallback (%s)\n", reason);
    igl_dri3_reported_failure = 1;
}

static void igl_dri3_trace_pixel(struct igl_backend *backend,
                                 struct igl_dri3_surface *surface)
{
    XImage *image;
    unsigned long pixel;

    if (!igl_dri3_debug_enabled() || !backend || !surface || !surface->pixmap)
        return;

    image = XGetImage(backend->display, surface->pixmap,
                      0, 0, 1, 1, AllPlanes, ZPixmap);
    if (!image) {
        fprintf(stderr,
                "IRISGL DRI3: XGetImage failed token=%u pixmap=0x%lx\n",
                surface->token, (unsigned long)surface->pixmap);
        return;
    }

    pixel = XGetPixel(image, 0, 0);
    fprintf(stderr,
            "IRISGL DRI3: X sees token=%u pixmap=0x%lx pixel=0x%08lx\n",
            surface->token, (unsigned long)surface->pixmap,
            pixel & 0xfffffffful);
    XDestroyImage(image);
}

static Bool igl_dri3_present_event(Display *display,
                                   XEvent *event,
                                   XPointer arg)
{
    struct igl_dri3_present_wait *wait;

    (void)display;
    wait = (struct igl_dri3_present_wait *)arg;
    return event->type == GenericEvent &&
           event->xcookie.extension == wait->opcode &&
           (event->xcookie.evtype == PresentCompleteNotify ||
            event->xcookie.evtype == PresentIdleNotify);
}

static struct igl_dri3_surface *igl_dri3_find(
    struct igl_backend_window *window,
    uint32_t token)
{
    struct igl_dri3_surface *surface;

    for (surface = igl_dri3_surfaces; surface; surface = surface->next) {
        if (surface->window == window && surface->token == token)
            return surface;
    }
    return NULL;
}

static struct igl_dri3_surface *igl_dri3_get(
    struct igl_backend_window *window,
    uint32_t token)
{
    struct igl_dri3_surface *surface;

    surface = igl_dri3_find(window, token);
    if (surface)
        return surface;

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return NULL;
    surface->window = window;
    surface->token = token;
    surface->next = igl_dri3_surfaces;
    igl_dri3_surfaces = surface;
    return surface;
}

static struct igl_dri3_window_state *igl_dri3_window_get(
    struct igl_backend_window *window)
{
    struct igl_dri3_window_state *state;

    for (state = igl_dri3_windows; state; state = state->next) {
        if (state->window == window)
            return state;
    }

    state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->window = window;
    state->front = None;
    state->next = igl_dri3_windows;
    igl_dri3_windows = state;
    return state;
}

static int igl_dri3_wait_present(struct igl_backend *backend,
                                 struct igl_backend_window *window,
                                 uint32_t serial,
                                 Pixmap old_front)
{
    struct igl_dri3_present_wait wait;
    XEvent event;
    int complete_seen;
    int old_idle_seen;

    complete_seen = 0;
    old_idle_seen = old_front == None;
    wait.opcode = backend->present_opcode;

    while (!complete_seen || !old_idle_seen) {
        XIfEvent(backend->display, &event,
                 igl_dri3_present_event, (XPointer)&wait);
        if (!XGetEventData(backend->display, &event.xcookie))
            continue;

        if (event.xcookie.evtype == PresentCompleteNotify) {
            XPresentCompleteNotifyEvent *complete;
            complete = (XPresentCompleteNotifyEvent *)event.xcookie.data;
            if (complete &&
                complete->kind == PresentCompleteKindPixmap &&
                complete->serial_number == serial &&
                complete->window == window->xwindow) {
                complete_seen = 1;
                if (complete->mode == PresentCompleteModeCopy)
                    old_idle_seen = 1;

                if (igl_dri3_debug_enabled())
                    fprintf(stderr,
                            "IRISGL DRI3: Present complete serial=%u mode=%u\n",
                            serial, complete->mode);
            }
        } else if (event.xcookie.evtype == PresentIdleNotify &&
                   old_front != None) {
            XPresentIdleNotifyEvent *idle;
            idle = (XPresentIdleNotifyEvent *)event.xcookie.data;
            if (idle && idle->window == window->xwindow &&
                idle->pixmap == old_front)
                old_idle_seen = 1;
        }

        XFreeEventData(backend->display, &event.xcookie);
    }

    return 1;
}

void igl_backend_wait_retrace(struct igl_backend *backend,
                              struct igl_backend_window *window)
{
    struct igl_dri3_present_wait wait;
    XEvent event;
    uint32_t serial;

    if (!backend || !backend->display || !window)
        return;

    if (!backend->present_available || !window->present_event_id) {
        XSync(backend->display, False);
        return;
    }

    serial = igl_dri3_next_serial(backend);
    XPresentNotifyMSC(backend->display, window->xwindow,
                      serial, 0, 1, 0);
    XFlush(backend->display);

    wait.opcode = backend->present_opcode;
    for (;;) {
        XPresentCompleteNotifyEvent *complete;

        XIfEvent(backend->display, &event,
                 igl_dri3_present_event, (XPointer)&wait);
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
            if (igl_dri3_debug_enabled())
                fprintf(stderr,
                        "IRISGL DRI3: native retrace complete serial=%u msc=%llu\n",
                        serial, (unsigned long long)complete->msc);
            XFreeEventData(backend->display, &event.xcookie);
            return;
        }

        XFreeEventData(backend->display, &event.xcookie);
    }
}

int igl_backend_present_dma_buf(struct igl_backend *backend,
                                struct igl_backend_window *window,
                                int dma_buf_fd,
                                uint32_t token,
                                uint32_t size,
                                int width,
                                int height,
                                int stride)
{
    struct igl_dri3_surface *surface;
    struct igl_dri3_window_state *window_state;
    xcb_connection_t *connection;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_pixmap_t pixmap;
    uint32_t serial;
    unsigned int depth;
    Pixmap old_front;

    if (!backend || !backend->display || !window || dma_buf_fd < 0 ||
        width <= 0 || height <= 0 || stride <= 0 || !size) {
        igl_dri3_report_failure("invalid presentation state");
        return 0;
    }

    if (!backend->present_available || !window->present_event_id) {
        igl_dri3_report_failure("X Present extension unavailable");
        return 0;
    }

    connection = XGetXCBConnection(backend->display);
    if (!connection) {
        igl_dri3_report_failure("XGetXCBConnection failed");
        return 0;
    }

    surface = igl_dri3_get(window, token);
    if (!surface) {
        igl_dri3_report_failure("surface cache allocation failed");
        return 0;
    }

    window_state = igl_dri3_window_get(window);
    if (!window_state) {
        igl_dri3_report_failure("window presentation state allocation failed");
        return 0;
    }

    if (!surface->pixmap ||
        surface->width != width || surface->height != height ||
        surface->stride != stride) {
        if (surface->pixmap)
            XFreePixmap(backend->display, surface->pixmap);

        pixmap = xcb_generate_id(connection);
        depth = (unsigned int)DefaultDepth(backend->display, backend->screen);
        cookie = xcb_dri3_pixmap_from_buffer_checked(
            connection,
            pixmap,
            (xcb_drawable_t)window->xwindow,
            size,
            (uint16_t)width,
            (uint16_t)height,
            (uint16_t)stride,
            (uint8_t)depth,
            32,
            dma_buf_fd);
        error = xcb_request_check(connection, cookie);
        if (error) {
            if (igl_dri3_debug_enabled())
                fprintf(stderr,
                        "IRISGL DRI3: PixmapFromBuffer error=%u major=%u minor=%u\n",
                        error->error_code,
                        error->major_code,
                        error->minor_code);
            free(error);
            surface->pixmap = None;
            igl_dri3_report_failure("PixmapFromBuffer rejected");
            return 0;
        }

        surface->pixmap = (Pixmap)pixmap;
        surface->width = width;
        surface->height = height;
        surface->stride = stride;
    }

    igl_dri3_trace_pixel(backend, surface);

    serial = igl_dri3_next_serial(backend);
    old_front = window_state->front;

    XPresentPixmap(backend->display,
                   window->xwindow,
                   surface->pixmap,
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
                   0,
                   0,
                   NULL,
                   0);
    XFlush(backend->display);

    if (window->doublebuffered &&
        !igl_dri3_wait_present(backend, window, serial, old_front))
        return 0;

    window_state->front = surface->pixmap;

    if (igl_dri3_debug_enabled() && !igl_dri3_reported_success) {
        fprintf(stderr,
                "IRISGL DRI3: Juniper dma-buf is direct presentation source\n");
        igl_dri3_reported_success = 1;
    }
    if (igl_dri3_debug_enabled() && window->doublebuffered)
        fprintf(stderr,
                "IRISGL DRI3: native Juniper front/back swap token=%u\n",
                token);
    return 1;
}
