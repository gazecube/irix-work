#include "igl_backend.h"
#include "igl_juniper.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TRI_WIDTH 256
#define TRI_HEIGHT 256
#define TRI_STRIDE 1024
#define TRI_SIZE ((uint64_t)TRI_STRIDE * (uint64_t)TRI_HEIGHT)
#define TRI_COLOR 0xff0f5c7fu

int main(void)
{
    struct igl_juniper_device dev;
    struct igl_juniper_bo color;
    struct igl_backend *backend;
    struct igl_backend_window *window;
    uint32_t *pixels;
    uint32_t center;
    uint32_t changed;
    int dma_buf_fd;
    int x;
    int y;

    memset(&dev, 0, sizeof(dev));
    memset(&color, 0, sizeof(color));
    dev.fd = -1;
    dma_buf_fd = -1;
    backend = NULL;
    window = NULL;

    if (!igl_juniper_open(&dev)) {
        fprintf(stderr, "junipertri: no supported Juniper Radeon render node\n");
        return 1;
    }

    printf("node: %s\n", dev.node);
    printf("target: %dx%d linear RGBA8 stride=%d\n",
           TRI_WIDTH, TRI_HEIGHT, TRI_STRIDE);
    printf("triangle: #0f5c7f\n");

    if (!igl_juniper_bo_create(&dev, &color, TRI_SIZE)) {
        fprintf(stderr, "junipertri: color BO allocation failed\n");
        goto fail;
    }

    if (!igl_juniper_cp_dma_clear(&dev, &color, 0, TRI_SIZE, 0xff000000u)) {
        fprintf(stderr, "junipertri: black CP DMA clear failed\n");
        goto fail;
    }

    errno = 0;
    if (!igl_juniper_draw_test_triangle(&dev, &color,
                                        TRI_WIDTH, TRI_HEIGHT, TRI_STRIDE)) {
        int saved_errno = errno;
        fprintf(stderr,
                "junipertri: native 3D submission failed: errno=%d (%s)\n",
                saved_errno,
                saved_errno ? strerror(saved_errno) : "no errno reported");
        goto fail;
    }

    pixels = (uint32_t *)color.map;
    center = pixels[(TRI_HEIGHT / 2) * (TRI_STRIDE / 4) + TRI_WIDTH / 2];
    changed = 0;
    for (y = 0; y < TRI_HEIGHT; y++) {
        for (x = 0; x < TRI_WIDTH; x++) {
            uint32_t pixel = pixels[y * (TRI_STRIDE / 4) + x];
            if (pixel != 0xff000000u)
                changed++;
        }
    }

    printf("center pixel: 0x%08x\n", center);
    printf("non-black pixels: %u\n", changed);

    if (center != TRI_COLOR || changed == 0) {
        fprintf(stderr,
                "junipertri: rasterizer verification failed "
                "(expected center 0x%08x)\n",
                TRI_COLOR);
        goto fail;
    }

    printf("native Juniper 3D triangle readback: PASS\n");

    backend = igl_backend_create();
    if (!backend) {
        fprintf(stderr, "junipertri: X11 backend creation failed\n");
        goto fail;
    }

    window = igl_backend_open_window(backend,
                                     "Juniper native triangle #0f5c7f",
                                     0, 0,
                                     TRI_WIDTH, TRI_HEIGHT, 0);
    if (!window) {
        fprintf(stderr, "junipertri: X11 window creation failed\n");
        goto fail;
    }

    if (!igl_juniper_bo_export_prime(&dev, &color, &dma_buf_fd)) {
        fprintf(stderr, "junipertri: PRIME export failed\n");
        goto fail;
    }

    if (!igl_backend_present_dma_buf(backend, window, dma_buf_fd,
                                     1, (uint32_t)TRI_SIZE,
                                     TRI_WIDTH, TRI_HEIGHT, TRI_STRIDE)) {
        fprintf(stderr, "junipertri: DRI3 presentation failed\n");
        goto fail;
    }
    close(dma_buf_fd);
    dma_buf_fd = -1;

    printf("native Juniper 3D triangle presentation: PASS\n");
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
