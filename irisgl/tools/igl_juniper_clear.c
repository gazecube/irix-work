#include "igl_juniper.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 256u
#define HEIGHT 256u
#define BYTES_PER_PIXEL 4u
#define CLEAR_VALUE 0xff3366ccu
#define SENTINEL_VALUE 0xdeadbeefu
#define BO_SIZE ((uint64_t)WIDTH * HEIGHT * BYTES_PER_PIXEL)

int main(void)
{
    struct igl_juniper_device dev;
    struct igl_juniper_bo bo;
    volatile uint32_t *pixels;
    size_t words;
    size_t i;
    int ok = 0;

    if (!igl_juniper_open(&dev)) {
        fprintf(stderr, "failed to open Juniper Radeon device\n");
        return 1;
    }

    if (!igl_juniper_bo_create(&dev, &bo, BO_SIZE)) {
        fprintf(stderr, "failed to allocate Juniper test BO\n");
        goto out_close;
    }

    pixels = (volatile uint32_t *)bo.map;
    words = (size_t)(BO_SIZE / sizeof(uint32_t));
    for (i = 0; i < words; ++i)
        pixels[i] = SENTINEL_VALUE;

    printf("node: %s\n", dev.node);
    printf("surface: %ux%u RGBA8 linear (%llu bytes)\n",
           WIDTH, HEIGHT, (unsigned long long)BO_SIZE);
    printf("before GPU clear: first=0x%08x last=0x%08x\n",
           (uint32_t)pixels[0], (uint32_t)pixels[words - 1]);
    printf("submitting Juniper CP DMA clear: 0x%08x\n", CLEAR_VALUE);

    if (!igl_juniper_cp_dma_clear(&dev, &bo, 0, BO_SIZE, CLEAR_VALUE)) {
        fprintf(stderr, "Juniper CP DMA clear: FAIL\n");
        goto out_bo;
    }

    printf("after GPU clear:  first=0x%08x last=0x%08x\n",
           (uint32_t)pixels[0], (uint32_t)pixels[words - 1]);

    for (i = 0; i < words; ++i) {
        if (pixels[i] != CLEAR_VALUE) {
            fprintf(stderr, "verification mismatch at dword %zu: 0x%08x\n",
                    i, (uint32_t)pixels[i]);
            goto out_bo;
        }
    }

    printf("native Juniper CP DMA clear: PASS\n");
    printf("full linear RGBA8 buffer verification: PASS\n");
    ok = 1;

out_bo:
    igl_juniper_bo_destroy(&dev, &bo);
out_close:
    igl_juniper_close(&dev);
    return ok ? 0 : 1;
}
