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

void igl_backend_clear(
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
