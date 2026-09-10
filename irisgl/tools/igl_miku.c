#include "igl_backend.h"
#include "igl_juniper.h"
#include "miku_mesh.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIKU_WIDTH 512
#define MIKU_HEIGHT 512
#define MIKU_STRIDE 2048
#define MIKU_SIZE ((uint64_t)MIKU_STRIDE * (uint64_t)MIKU_HEIGHT)
#define MIKU_RGB 0x000f5c7fu
#define MIKU_BORDER 32.0f

int main(void)
{
    struct igl_juniper_device dev;
    struct igl_juniper_bo color;
    struct igl_backend *backend;
    struct igl_backend_window *window;
    float (*stream)[4];
    float model_width;
    float model_height;
    float scale_x;
    float scale_y;
    float scale;
    float center_x;
    float center_y;
    uint32_t *pixels;
    uint32_t changed;
    uint32_t i;
    int dma_buf_fd;
    int x;
    int y;

    memset(&dev, 0, sizeof(dev));
    memset(&color, 0, sizeof(color));
    dev.fd = -1;
    dma_buf_fd = -1;
    backend = NULL;
    window = NULL;
    stream = NULL;

    if (!igl_juniper_open(&dev)) {
        fprintf(stderr, "miku: no supported Juniper Radeon render node\n");
        return 1;
    }

    if (!igl_juniper_bo_create(&dev, &color, MIKU_SIZE)) {
        fprintf(stderr, "miku: color BO allocation failed\n");
        goto fail;
    }

    if (!igl_juniper_cp_dma_clear(&dev, &color, 0, MIKU_SIZE, 0xff000000u)) {
        fprintf(stderr, "miku: black CP DMA clear failed\n");
        goto fail;
    }

    stream = malloc(sizeof(*stream) * (size_t)IGL_MIKU_INDEX_COUNT);
    if (!stream) {
        fprintf(stderr, "miku: vertex stream allocation failed\n");
        goto fail;
    }

    model_width = IGL_MIKU_MAX_X - IGL_MIKU_MIN_X;
    model_height = IGL_MIKU_MAX_Y - IGL_MIKU_MIN_Y;
    scale_x = ((float)MIKU_WIDTH - 2.0f * MIKU_BORDER) / model_width;
    scale_y = ((float)MIKU_HEIGHT - 2.0f * MIKU_BORDER) / model_height;
    scale = scale_x < scale_y ? scale_x : scale_y;
    center_x = (IGL_MIKU_MIN_X + IGL_MIKU_MAX_X) * 0.5f;
    center_y = (IGL_MIKU_MIN_Y + IGL_MIKU_MAX_Y) * 0.5f;

    /*
     * First Miku bring-up uses a flat front orthographic projection in
     * software. The current proven Juniper VS consumes screen-space XY via
     * VTX_XY_FMT, so object X/Y are centered, uniformly scaled and mapped to
     * the 512x512 viewport here. FBX Y points up; screen Y points down.
     *
     * Z is deliberately zero while depth testing is not yet part of the
     * proven native path. Every triangle uses one flat color, so overlap order
     * cannot alter the silhouette.
     */
    for (i = 0; i < IGL_MIKU_INDEX_COUNT; ++i) {
        const struct igl_miku_vertex *v;
        uint16_t index;

        index = igl_miku_indices[i];
        v = &igl_miku_vertices[index];
        stream[i][0] = (float)MIKU_WIDTH * 0.5f + (v->x - center_x) * scale;
        stream[i][1] = (float)MIKU_HEIGHT * 0.5f - (v->y - center_y) * scale;
        stream[i][2] = 0.0f;
        stream[i][3] = 1.0f;
    }

    printf("node: %s\n", dev.node);
    printf("miku: %u source vertices, %u triangles, %u submitted vertices\n",
           (unsigned)IGL_MIKU_VERTEX_COUNT,
           (unsigned)IGL_MIKU_TRIANGLE_COUNT,
           (unsigned)IGL_MIKU_INDEX_COUNT);
    printf("projection: flat front orthographic, %dx%d viewport\n",
           MIKU_WIDTH, MIKU_HEIGHT);
    printf("color: #0f5c7f\n");

    if (!igl_juniper_draw_flat_triangles(&dev, &color,
                                         MIKU_WIDTH, MIKU_HEIGHT, MIKU_STRIDE,
                                         (const float (*)[4])stream,
                                         IGL_MIKU_INDEX_COUNT,
                                         MIKU_RGB)) {
        fprintf(stderr, "miku: native triangle-list submission failed\n");
        goto fail;
    }

    pixels = (uint32_t *)color.map;
    changed = 0;
    for (y = 0; y < MIKU_HEIGHT; ++y) {
        for (x = 0; x < MIKU_WIDTH; ++x) {
            if (pixels[y * (MIKU_STRIDE / 4) + x] != 0xff000000u)
                ++changed;
        }
    }

    printf("non-black pixels: %u\n", changed);
    if (!changed) {
        fprintf(stderr, "miku: rasterizer verification failed\n");
        goto fail;
    }
    printf("native Juniper Miku readback: PASS\n");

    backend = igl_backend_create();
    if (!backend) {
        fprintf(stderr, "miku: X11 backend creation failed\n");
        goto fail;
    }

    window = igl_backend_open_window(backend,
                                     "IRIS GL / Juniper - Miku",
                                     0, 0, MIKU_WIDTH, MIKU_HEIGHT, 0);
    if (!window) {
        fprintf(stderr, "miku: X11 window creation failed\n");
        goto fail;
    }

    if (!igl_juniper_bo_export_prime(&dev, &color, &dma_buf_fd)) {
        fprintf(stderr, "miku: PRIME export failed\n");
        goto fail;
    }

    if (!igl_backend_present_dma_buf(backend, window, dma_buf_fd,
                                     4, (uint32_t)MIKU_SIZE,
                                     MIKU_WIDTH, MIKU_HEIGHT, MIKU_STRIDE)) {
        fprintf(stderr, "miku: DRI3 presentation failed\n");
        goto fail;
    }
    close(dma_buf_fd);
    dma_buf_fd = -1;

    printf("native Juniper Miku presentation: PASS\n");
    printf("MIKU IS IN THE VIEWPORT. displaying for 15 seconds...\n");
    sleep(15);

    free(stream);
    igl_backend_destroy_window(backend, window);
    igl_backend_destroy(backend);
    igl_juniper_bo_destroy(&dev, &color);
    igl_juniper_close(&dev);
    return 0;

fail:
    if (dma_buf_fd >= 0)
        close(dma_buf_fd);
    free(stream);
    if (backend && window)
        igl_backend_destroy_window(backend, window);
    if (backend)
        igl_backend_destroy(backend);
    igl_juniper_bo_destroy(&dev, &color);
    igl_juniper_close(&dev);
    return 1;
}
