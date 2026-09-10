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

/*
 * Reconstructed generalized bring-up boundary. Vertices are screen-space
 * float4 values matching the proven PA_CL_VTE_CNTL/VTX_XY_FMT path. rgb is
 * packed 0x00RRGGBB; alpha remains 1.0 in the fixed flat-color PS.
 *
 * Keep this separate from the public IRIS GL API until original primitive
 * entry-point behavior is recovered and verified.
 */
int igl_juniper_draw_flat_triangle(struct igl_juniper_device *dev,
                                   struct igl_juniper_bo *color,
                                   int width,
                                   int height,
                                   int stride,
                                   const float vertices[3][4],
                                   uint32_t rgb);

/*
 * Reconstructed batched triangle-list boundary. The stream is non-indexed
 * float4 screen-space vertices and vertex_count must be a multiple of three.
 * It uses one vertex BO and one DRAW_INDEX_AUTO submission for the list.
 */
int igl_juniper_draw_flat_triangles(struct igl_juniper_device *dev,
                                    struct igl_juniper_bo *color,
                                    int width,
                                    int height,
                                    int stride,
                                    const float (*vertices)[4],
                                    uint32_t vertex_count,
                                    uint32_t rgb);

#endif /* IRISGL_IGL_JUNIPER_H */
