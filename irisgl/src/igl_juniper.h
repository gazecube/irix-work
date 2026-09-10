#ifndef IRISGL_IGL_JUNIPER_H
#define IRISGL_IGL_JUNIPER_H

#include <stddef.h>
#include <stdint.h>

struct igl_juniper_device {
    int fd;
    char node[64];
};

struct igl_juniper_bo {
    uint32_t handle;
    uint64_t size;
    void *map;
};

int igl_juniper_open(struct igl_juniper_device *dev);
void igl_juniper_close(struct igl_juniper_device *dev);
int igl_juniper_bo_create(struct igl_juniper_device *dev,
                          struct igl_juniper_bo *bo,
                          uint64_t size);
void igl_juniper_bo_destroy(struct igl_juniper_device *dev,
                            struct igl_juniper_bo *bo);
int igl_juniper_bo_wait(struct igl_juniper_device *dev,
                        struct igl_juniper_bo *bo);
int igl_juniper_bo_export_prime(struct igl_juniper_device *dev,
                                struct igl_juniper_bo *bo,
                                int *dma_buf_fd);
int igl_juniper_cp_dma_clear(struct igl_juniper_device *dev,
                             struct igl_juniper_bo *bo,
                             uint64_t offset,
                             uint64_t size,
                             uint32_t value);

/*
 * Reconstructed bring-up boundary for the first native Juniper 3D draw.
 * This is deliberately kept separate from the IRIS GL runtime until the
 * hardware sequence is proven. The target is a linear RGBA8/BGRA-in-memory
 * color surface with a 256-byte-aligned byte stride.
 */
int igl_juniper_draw_test_triangle(struct igl_juniper_device *dev,
                                   struct igl_juniper_bo *color,
                                   int width,
                                   int height,
                                   int stride);

#endif /* IRISGL_IGL_JUNIPER_H */
