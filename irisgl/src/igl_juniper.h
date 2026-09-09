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
int igl_juniper_cp_dma_clear(struct igl_juniper_device *dev,
                             struct igl_juniper_bo *bo,
                             uint64_t offset,
                             uint64_t size,
                             uint32_t value);

#endif /* IRISGL_IGL_JUNIPER_H */
