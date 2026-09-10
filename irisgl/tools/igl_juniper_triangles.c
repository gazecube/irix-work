#include "igl_backend.h"
#include "igl_juniper.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define W 256
#define H 256
#define STRIDE 1024
#define SIZE ((uint64_t)STRIDE * (uint64_t)H)
#define RGB 0x000f5c7fu
#define COLOR 0xff0f5c7fu
#define TRI_COUNT 16u
#define VERT_COUNT (TRI_COUNT * 3u)

int main(void)
{
    struct igl_juniper_device dev;
    struct igl_juniper_bo color;
    struct igl_backend *backend;
    struct igl_backend_window *window;
    float vertices[VERT_COUNT][4];
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

    for (i = 0; i < TRI_COUNT; ++i) {
        float cx = 32.0f + (float)(i % 4u) * 64.0f;
        float cy = 32.0f + (float)(i / 4u) * 64.0f;
        uint32_t v = i * 3u;
        vertices[v + 0u][0] = cx - 20.0f;
        vertices[v + 0u][1] = cy + 20.0f;
        vertices[v + 0u][2] = 0.0f;
        vertices[v + 0u][3] = 1.0f;
        vertices[v + 1u][0] = cx;
        vertices[v + 1u][1] = cy - 20.0f;
        vertices[v + 1u][2] = 0.0f;
        vertices[v + 1u][3] = 1.0f;
        vertices[v + 2u][0] = cx + 20.0f;
        vertices[v + 2u][1] = cy + 20.0f;
        vertices[v + 2u][2] = 0.0f;
        vertices[v + 2u][3] = 1.0f;
    }

    if (!igl_juniper_open(&dev)) {
        fprintf(stderr, "junipertris: no supported Juniper Radeon render node\n");
        return 1;
    }

    if (!igl_juniper_bo_create(&dev, &color, SIZE)) {
        fprintf(stderr, "junipertris: color BO allocation failed\n");
        goto fail;
    }

    if (!igl_juniper_cp_dma_clear(&dev, &color, 0, SIZE, 0xff000000u)) {
        fprintf(stderr, "junipertris: black CP DMA clear failed\n");
        goto fail;
    }

    if (!igl_juniper_draw_flat_triangles(&dev, &color,
                                         W, H, STRIDE,
                                         vertices, VERT_COUNT, RGB)) {
        fprintf(stderr, "junipertris: native triangle-list submission failed\n");
        goto fail;
    }

    pixels = (uint32_t *)color.map;
    changed = 0;
    for (y = 0; y < H; ++y) {
        for (x = 0; x < W; ++x) {
            if (pixels[y * (STRIDE / 4) + x] != 0xff000000u)
                ++changed;
        }
    }

    printf("triangle-list: %u triangles / %u vertices\n",
           TRI_COUNT, VERT_COUNT);
    printf("non-black pixels: %u\n", changed);
    if (!changed) {
        fprintf(stderr, "junipertris: rasterizer verification failed\n");
        goto fail;
    }

    printf("batched native Juniper triangle-list readback: PASS\n");

    backend = igl_backend_create();
    if (!backend)
        goto fail;
    window = igl_backend_open_window(backend, "Juniper triangle-list #0f5c7f",
                                     0, 0, W, H, 0);
    if (!window)
        goto fail;
    if (!igl_juniper_bo_export_prime(&dev, &color, &dma_buf_fd))
        goto fail;
    if (!igl_backend_present_dma_buf(backend, window, dma_buf_fd,
                                     3, (uint32_t)SIZE, W, H, STRIDE))
        goto fail;
    close(dma_buf_fd);
    dma_buf_fd = -1;

    printf("batched native Juniper triangle-list presentation: PASS\n");
    printf("displaying for 10 seconds...\n");
    sleep(10);

    igl_backend_destroy_window(backend, window);
    igl_backend_destroy(backend);
    igl_juniper_bo_destroy(&dev, &color);
    igl_juniper_close(&dev);
    return 0;

fail:
    if (dma_buf_fd >= 0)
        close(dma_buf_fd);
    if (backend && window)
        igl_backend_destroy_window(backend, window);
    if (backend)
        igl_backend_destroy(backend);
    igl_juniper_bo_destroy(&dev, &color);
    igl_juniper_close(&dev);
    return 1;
}
