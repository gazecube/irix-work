#include "igl_juniper.h"
#include "igl_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <radeon_drm.h>

#define IGL_JUNIPER_CP_DMA_MAX_BYTES ((1u << 21) - 8u)
#define IGL_PKT3(op, n) \
    ((3u << 30) | (((uint32_t)(op) & 0xffu) << 8) | \
     (((uint32_t)(n) & 0x3fffu) << 16))
#define IGL_PKT3_NOP 0x10u
#define IGL_PKT3_CP_DMA 0x41u
#define IGL_CP_DMA_CP_SYNC (1u << 31)
#define IGL_CP_DMA_SRC_SEL_DATA (2u << 29)

static int igl_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret == 0;
}

int igl_juniper_open(struct igl_juniper_device *dev)
{
    struct igl_hw_device hw;
    drmVersionPtr version;

    if (!dev)
        return 0;
    memset(dev, 0, sizeof(*dev));
    dev->fd = -1;

    if (!igl_hw_probe(&hw) || hw.chip != IGL_HW_CHIP_AMD_JUNIPER ||
        !hw.drm_render[0])
        return 0;

    dev->fd = open(hw.drm_render, O_RDWR | O_CLOEXEC);
    if (dev->fd < 0)
        return 0;

    version = drmGetVersion(dev->fd);
    if (!version || version->name_len != 6 ||
        strncmp(version->name, "radeon", 6) != 0) {
        if (version)
            drmFreeVersion(version);
        close(dev->fd);
        dev->fd = -1;
        return 0;
    }
    drmFreeVersion(version);

    if (strlen(hw.drm_render) >= sizeof(dev->node)) {
        close(dev->fd);
        dev->fd = -1;
        return 0;
    }
    strcpy(dev->node, hw.drm_render);
    return 1;
}

void igl_juniper_close(struct igl_juniper_device *dev)
{
    if (!dev)
        return;
    if (dev->fd >= 0)
        close(dev->fd);
    dev->fd = -1;
    dev->node[0] = '\0';
}

int igl_juniper_bo_create(struct igl_juniper_device *dev,
                          struct igl_juniper_bo *bo,
                          uint64_t size)
{
    struct drm_radeon_gem_create create;
    struct drm_radeon_gem_mmap map;

    if (!dev || !bo || dev->fd < 0 || !size)
        return 0;
    memset(bo, 0, sizeof(*bo));
    bo->map = MAP_FAILED;

    memset(&create, 0, sizeof(create));
    create.size = size;
    create.alignment = 4096;
    create.initial_domain = RADEON_GEM_DOMAIN_GTT;
    create.flags = RADEON_GEM_CPU_ACCESS;
    if (!igl_ioctl(dev->fd, DRM_IOCTL_RADEON_GEM_CREATE, &create))
        return 0;

    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    map.size = size;
    if (!igl_ioctl(dev->fd, DRM_IOCTL_RADEON_GEM_MMAP, &map)) {
        struct drm_gem_close close_arg;
        memset(&close_arg, 0, sizeof(close_arg));
        close_arg.handle = create.handle;
        ioctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &close_arg);
        return 0;
    }

    bo->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   dev->fd, (off_t)map.addr_ptr);
    if (bo->map == MAP_FAILED) {
        struct drm_gem_close close_arg;
        memset(&close_arg, 0, sizeof(close_arg));
        close_arg.handle = create.handle;
        ioctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &close_arg);
        return 0;
    }

    bo->handle = create.handle;
    bo->size = size;
    return 1;
}

void igl_juniper_bo_destroy(struct igl_juniper_device *dev,
                            struct igl_juniper_bo *bo)
{
    struct drm_gem_close close_arg;

    if (!dev || !bo)
        return;
    if (bo->map && bo->map != MAP_FAILED)
        munmap(bo->map, bo->size);
    if (dev->fd >= 0 && bo->handle) {
        memset(&close_arg, 0, sizeof(close_arg));
        close_arg.handle = bo->handle;
        ioctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &close_arg);
    }
    memset(bo, 0, sizeof(*bo));
}

int igl_juniper_bo_wait(struct igl_juniper_device *dev,
                        struct igl_juniper_bo *bo)
{
    struct drm_radeon_gem_wait_idle wait;

    if (!dev || !bo || dev->fd < 0 || !bo->handle)
        return 0;
    memset(&wait, 0, sizeof(wait));
    wait.handle = bo->handle;
    for (;;) {
        if (ioctl(dev->fd, DRM_IOCTL_RADEON_GEM_WAIT_IDLE, &wait) == 0)
            return 1;
        if (errno != EINTR && errno != EBUSY)
            return 0;
    }
}

int igl_juniper_bo_export_prime(struct igl_juniper_device *dev,
                                struct igl_juniper_bo *bo,
                                int *dma_buf_fd)
{
    int fd;

    if (!dev || !bo || !dma_buf_fd || dev->fd < 0 || !bo->handle)
        return 0;

    fd = -1;
    if (drmPrimeHandleToFD(dev->fd, bo->handle,
                           DRM_CLOEXEC | DRM_RDWR, &fd) != 0)
        return 0;

    *dma_buf_fd = fd;
    return 1;
}

int igl_juniper_cp_dma_clear(struct igl_juniper_device *dev,
                             struct igl_juniper_bo *bo,
                             uint64_t offset,
                             uint64_t size,
                             uint32_t value)
{
    struct drm_radeon_cs_reloc reloc;
    struct drm_radeon_cs_chunk chunks[3];
    struct drm_radeon_cs cs;
    struct drm_radeon_gem_set_domain domain;
    uint64_t chunk_ptrs[3];
    uint32_t flags[3];
    uint32_t *ib;
    uint64_t remaining;
    uint64_t dst_offset;
    size_t packet_count;
    size_t dw_count;
    size_t p;

    if (!dev || !bo || dev->fd < 0 || !bo->handle || !size ||
        (offset & 3u) || (size & 3u) || offset + size > bo->size)
        return 0;

    packet_count = (size_t)((size + IGL_JUNIPER_CP_DMA_MAX_BYTES - 1u) /
                            IGL_JUNIPER_CP_DMA_MAX_BYTES);
    dw_count = packet_count * 8u;
    ib = calloc(dw_count, sizeof(*ib));
    if (!ib)
        return 0;

    remaining = size;
    dst_offset = offset;
    p = 0;
    while (remaining) {
        uint32_t byte_count = remaining > IGL_JUNIPER_CP_DMA_MAX_BYTES ?
                              IGL_JUNIPER_CP_DMA_MAX_BYTES :
                              (uint32_t)remaining;
        uint32_t sync = remaining == byte_count ? IGL_CP_DMA_CP_SYNC : 0u;

        ib[p++] = IGL_PKT3(IGL_PKT3_CP_DMA, 4);
        ib[p++] = value;
        ib[p++] = sync | IGL_CP_DMA_SRC_SEL_DATA;
        ib[p++] = (uint32_t)dst_offset;
        ib[p++] = (uint32_t)((dst_offset >> 32) & 0xffu);
        ib[p++] = byte_count;
        ib[p++] = IGL_PKT3(IGL_PKT3_NOP, 0);
        ib[p++] = 0;

        dst_offset += byte_count;
        remaining -= byte_count;
    }

    memset(&reloc, 0, sizeof(reloc));
    reloc.handle = bo->handle;
    reloc.read_domains = 0;
    reloc.write_domain = RADEON_GEM_DOMAIN_GTT;

    flags[0] = 0;
    flags[1] = RADEON_CS_RING_GFX;
    flags[2] = 0;

    memset(chunks, 0, sizeof(chunks));
    chunks[0].chunk_id = RADEON_CHUNK_ID_IB;
    chunks[0].length_dw = (uint32_t)dw_count;
    chunks[0].chunk_data = (uint64_t)(uintptr_t)ib;
    chunks[1].chunk_id = RADEON_CHUNK_ID_RELOCS;
    chunks[1].length_dw = sizeof(reloc) / sizeof(uint32_t);
    chunks[1].chunk_data = (uint64_t)(uintptr_t)&reloc;
    chunks[2].chunk_id = RADEON_CHUNK_ID_FLAGS;
    chunks[2].length_dw = 3;
    chunks[2].chunk_data = (uint64_t)(uintptr_t)flags;

    chunk_ptrs[0] = (uint64_t)(uintptr_t)&chunks[0];
    chunk_ptrs[1] = (uint64_t)(uintptr_t)&chunks[1];
    chunk_ptrs[2] = (uint64_t)(uintptr_t)&chunks[2];

    memset(&cs, 0, sizeof(cs));
    cs.num_chunks = 3;
    cs.chunks = (uint64_t)(uintptr_t)chunk_ptrs;

    if (!igl_ioctl(dev->fd, DRM_IOCTL_RADEON_CS, &cs)) {
        free(ib);
        return 0;
    }
    free(ib);

    if (!igl_juniper_bo_wait(dev, bo))
        return 0;

    memset(&domain, 0, sizeof(domain));
    domain.handle = bo->handle;
    domain.read_domains = RADEON_GEM_DOMAIN_CPU;
    domain.write_domain = RADEON_GEM_DOMAIN_CPU;
    return igl_ioctl(dev->fd, DRM_IOCTL_RADEON_GEM_SET_DOMAIN, &domain);
}
