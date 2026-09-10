#include "igl_backend.h"

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
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

static struct igl_dri3_surface *igl_dri3_surfaces;
static int igl_dri3_reported_success;
static int igl_dri3_reported_failure;

static int igl_dri3_debug_enabled(void)
{
    const char *value = getenv("IRISGL_DEBUG_DRI3");
    return value && *value && *value != '0';
}

static void igl_dri3_report_failure(const char *reason)
{
    if (!igl_dri3_debug_enabled() || igl_dri3_reported_failure)
        return;
    fprintf(stderr, "IRISGL DRI3: fallback (%s)\n", reason);
    igl_dri3_reported_failure = 1;
}

static struct igl_dri3_surface *igl_dri3_find(
    struct igl_backend_window *window)
{
    struct igl_dri3_surface *surface;

    for (surface = igl_dri3_surfaces; surface; surface = surface->next) {
        if (surface->window == window)
            return surface;
    }
    return NULL;
}

static struct igl_dri3_surface *igl_dri3_get(
    struct igl_backend_window *window)
{
    struct igl_dri3_surface *surface;

    surface = igl_dri3_find(window);
    if (surface)
        return surface;

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return NULL;
    surface->window = window;
    surface->next = igl_dri3_surfaces;
    igl_dri3_surfaces = surface;
    return surface;
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
    xcb_connection_t *connection;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_pixmap_t pixmap;
    uint32_t serial;
    unsigned int depth;

    if (!backend || !backend->display || !window || dma_buf_fd < 0 ||
        width <= 0 || height <= 0 || stride <= 0 || !size) {
        igl_dri3_report_failure("invalid presentation state");
        return 0;
    }

    if (window->doublebuffered) {
        igl_dri3_report_failure("native double buffering not implemented yet");
        return 0;
    }
    if (!backend->present_available) {
        igl_dri3_report_failure("X Present extension unavailable");
        return 0;
    }

    connection = XGetXCBConnection(backend->display);
    if (!connection) {
        igl_dri3_report_failure("XGetXCBConnection failed");
        return 0;
    }

    surface = igl_dri3_get(window);
    if (!surface) {
        igl_dri3_report_failure("surface cache allocation failed");
        return 0;
    }

    if (!surface->pixmap || surface->token != token ||
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
        surface->token = token;
        surface->width = width;
        surface->height = height;
        surface->stride = stride;
    }

    serial = ++backend->present_serial;
    if (serial == 0)
        serial = ++backend->present_serial;

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

    if (igl_dri3_debug_enabled() && !igl_dri3_reported_success) {
        fprintf(stderr,
                "IRISGL DRI3: Juniper dma-buf is direct presentation source\n");
        igl_dri3_reported_success = 1;
    }
    return 1;
}
