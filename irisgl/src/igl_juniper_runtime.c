#include "igl_juniper_runtime.h"

#include "igl_juniper.h"
#include "igl_state.h"

#include <stdint.h>
#include <stdlib.h>

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
