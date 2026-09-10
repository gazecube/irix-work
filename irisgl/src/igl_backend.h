#ifndef IRISGL_IGL_BACKEND_H
#define IRISGL_IGL_BACKEND_H

#include <stdint.h>

struct igl_backend;
struct igl_backend_window;

struct igl_backend *igl_backend_create(void);
void igl_backend_destroy(struct igl_backend *backend);

struct igl_backend_window *igl_backend_open_window(
    struct igl_backend *backend,
    const char *title,
    int x,
    int y,
    int width,
    int height,
    int have_position);

void igl_backend_destroy_window(
    struct igl_backend *backend,
    struct igl_backend_window *window);

int igl_backend_get_geometry(
    struct igl_backend *backend,
    struct igl_backend_window *window,
    int *x,
    int *y,
    int *width,
    int *height);

void igl_backend_set_rgb(
    struct igl_backend *backend,
    struct igl_backend_window *window,
    int16_t red,
    int16_t green,
    int16_t blue);

int igl_backend_set_doublebuffer(
    struct igl_backend *backend,
    struct igl_backend_window *window,
    int enabled);

void igl_backend_clear(
    struct igl_backend *backend,
    struct igl_backend_window *window);

void igl_backend_swapbuffers(
    struct igl_backend *backend,
    struct igl_backend_window *window);

/*
 * Reconstructed host zero-copy presentation boundary. Imports a dma-buf as
 * an X11 DRI3 pixmap and presents that pixmap directly. token is a host-side
 * surface lifetime identity, not a Radeon GEM handle; GEM handles may be
 * reused immediately when gconfig() replaces a surface.
 */
int igl_backend_present_dma_buf(
    struct igl_backend *backend,
    struct igl_backend_window *window,
    int dma_buf_fd,
    uint32_t token,
    uint32_t size,
    int width,
    int height,
    int stride);

/*
 * Reconstructed native retrace barrier. Waits for the next Present MSC
 * without entering the compatibility X11 staging-pixmap path. This is used
 * when a hardware-backed Juniper surface is already the visible source.
 */
void igl_backend_wait_retrace(
    struct igl_backend *backend,
    struct igl_backend_window *window);

/*
 * Compatibility synchronization boundary for IRIS GL gsync(). The X11
 * backend may stage frames in alternating X pixmaps after gsync() is used in
 * single-buffer mode. Native Juniper presentation bypasses that staging and
 * uses igl_backend_wait_retrace() instead.
 */
void igl_backend_gsync(
    struct igl_backend *backend,
    struct igl_backend_window *window);

/* Returns 1 if an event was produced, 0 otherwise. */
int igl_backend_poll_event(
    struct igl_backend *backend,
    struct igl_backend_window **window,
    long *device,
    short *value,
    int block);

#endif /* IRISGL_IGL_BACKEND_H */
