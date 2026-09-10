#include "igl_juniper_runtime.h"

#include "igl_backend.h"
#include "igl_juniper.h"
#include "igl_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

void igl_juniper_runtime_init(struct igl_runtime_state *runtime)
{
    struct igl_juniper_device *dev;

    if (!runtime || runtime->juniper)
        return;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return;

    if (!igl_juniper_open(dev)) {
        free(dev);
        return;
    }

    runtime->juniper = dev;
}

int igl_juniper_runtime_ensure_surface(struct igl_runtime_state *runtime,
                                       struct igl_window_state *window)
{
    struct igl_juniper_bo *bo;
    uint64_t size;
    int width;
    int height;

    if (!runtime || !runtime->juniper || !window)
        return 0;

    width = window->pane.clip.xsize;
    height = window->pane.clip.ysize;
    if (width <= 0 || height <= 0)
        return 0;

    if (window->host_color.bo &&
        window->host_color.width == width &&
        window->host_color.height == height)
        return 1;

    if (window->host_color.bo) {
        igl_juniper_bo_destroy(runtime->juniper, window->host_color.bo);
        free(window->host_color.bo);
        window->host_color.bo = NULL;
    }

    size = (uint64_t)(unsigned int)width *
           (uint64_t)(unsigned int)height * 4u;

    bo = calloc(1, sizeof(*bo));
    if (!bo)
        return 0;

    if (!igl_juniper_bo_create(runtime->juniper, bo, size)) {
        free(bo);
        return 0;
    }

    window->host_color.bo = bo;
    window->host_color.width = width;
    window->host_color.height = height;
    return 1;
}

int igl_juniper_runtime_clear(struct igl_runtime_state *runtime,
                              struct igl_window_state *window,
                              uint32_t rgba8)
{
    uint64_t size;

    if (!igl_juniper_runtime_ensure_surface(runtime, window))
        return 0;

    size = window->host_color.bo->size;
    return igl_juniper_cp_dma_clear(runtime->juniper,
                                    window->host_color.bo,
                                    0,
                                    size,
                                    rgba8);
}

int igl_juniper_runtime_present(struct igl_runtime_state *runtime,
                                struct igl_window_state *window)
{
    struct igl_juniper_bo *bo;
    int dma_buf_fd;
    int stride;
    int ok;

    if (!runtime || !runtime->juniper || !runtime->backend || !window ||
        !window->backend_window || !window->host_color.bo)
        return 0;

    bo = window->host_color.bo;
    if (bo->size > UINT32_MAX)
        return 0;

    dma_buf_fd = -1;
    if (!igl_juniper_bo_export_prime(runtime->juniper, bo, &dma_buf_fd))
        return 0;

    stride = window->host_color.width * 4;
    ok = igl_backend_present_dma_buf(runtime->backend,
                                     window->backend_window,
                                     dma_buf_fd,
                                     bo->handle,
                                     (uint32_t)bo->size,
                                     window->host_color.width,
                                     window->host_color.height,
                                     stride);
    close(dma_buf_fd);
    return ok;
}
