#include "igl_juniper_runtime.h"

#include "igl_backend.h"
#include "igl_juniper.h"
#include "igl_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static void igl_juniper_runtime_destroy_surfaces(
    struct igl_runtime_state *runtime,
    struct igl_window_state *window)
{
    int i;

    if (!runtime || !runtime->juniper || !window)
        return;

    for (i = 0; i < 2; ++i) {
        if (!window->host_color.bo[i])
            continue;
        igl_juniper_bo_destroy(runtime->juniper, window->host_color.bo[i]);
        free(window->host_color.bo[i]);
        window->host_color.bo[i] = NULL;
    }

    window->host_color.width = 0;
    window->host_color.height = 0;
    window->host_color.count = 0;
    window->host_color.draw = 0;
    window->host_color.front = -1;
}

static struct igl_juniper_bo *igl_juniper_runtime_draw_bo(
    struct igl_window_state *window)
{
    int draw;

    if (!window || window->host_color.count <= 0)
        return NULL;

    draw = window->host_color.draw;
    if (draw < 0 || draw >= window->host_color.count)
        return NULL;
    return window->host_color.bo[draw];
}

static int igl_juniper_runtime_present_bo(struct igl_runtime_state *runtime,
                                          struct igl_window_state *window,
                                          struct igl_juniper_bo *bo)
{
    int dma_buf_fd;
    int stride;
    int ok;

    if (!runtime || !runtime->juniper || !runtime->backend || !window ||
        !window->backend_window || !bo)
        return 0;

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
    int wanted_count;
    int i;

    if (!runtime || !runtime->juniper || !window)
        return 0;

    width = window->pane.clip.xsize;
    height = window->pane.clip.ysize;
    if (width <= 0 || height <= 0)
        return 0;

    wanted_count = window->committed.double_buffer ? 2 : 1;

    if (window->host_color.count == wanted_count &&
        window->host_color.width == width &&
        window->host_color.height == height) {
        for (i = 0; i < wanted_count; ++i) {
            if (!window->host_color.bo[i])
                break;
        }
        if (i == wanted_count)
            return 1;
    }

    igl_juniper_runtime_destroy_surfaces(runtime, window);

    size = (uint64_t)(unsigned int)width *
           (uint64_t)(unsigned int)height * 4u;

    for (i = 0; i < wanted_count; ++i) {
        bo = calloc(1, sizeof(*bo));
        if (!bo) {
            igl_juniper_runtime_destroy_surfaces(runtime, window);
            return 0;
        }

        if (!igl_juniper_bo_create(runtime->juniper, bo, size)) {
            free(bo);
            igl_juniper_runtime_destroy_surfaces(runtime, window);
            return 0;
        }
        window->host_color.bo[i] = bo;
    }

    window->host_color.width = width;
    window->host_color.height = height;
    window->host_color.count = wanted_count;
    window->host_color.draw = 0;
    window->host_color.front = -1;
    return 1;
}

int igl_juniper_runtime_clear(struct igl_runtime_state *runtime,
                              struct igl_window_state *window,
                              uint32_t rgba8)
{
    struct igl_juniper_bo *bo;

    if (!igl_juniper_runtime_ensure_surface(runtime, window))
        return 0;

    bo = igl_juniper_runtime_draw_bo(window);
    if (!bo)
        return 0;

    return igl_juniper_cp_dma_clear(runtime->juniper,
                                    bo,
                                    0,
                                    bo->size,
                                    rgba8);
}

int igl_juniper_runtime_present(struct igl_runtime_state *runtime,
                                struct igl_window_state *window)
{
    struct igl_juniper_bo *bo;

    if (!igl_juniper_runtime_ensure_surface(runtime, window))
        return 0;

    bo = igl_juniper_runtime_draw_bo(window);
    if (!bo)
        return 0;

    if (!igl_juniper_runtime_present_bo(runtime, window, bo))
        return 0;

    window->host_color.front = window->host_color.draw;
    return 1;
}

int igl_juniper_runtime_swap(struct igl_runtime_state *runtime,
                             struct igl_window_state *window)
{
    int presented;

    if (!runtime || !window || !window->committed.double_buffer)
        return 0;

    if (!igl_juniper_runtime_ensure_surface(runtime, window) ||
        window->host_color.count != 2)
        return 0;

    presented = window->host_color.draw;
    if (!igl_juniper_runtime_present_bo(runtime,
                                        window,
                                        window->host_color.bo[presented]))
        return 0;

    window->host_color.front = presented;
    window->host_color.draw = presented ^ 1;
    return 1;
}
