#include "igl_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <radeon_drm.h>

#define TEST_BO_SIZE 4096u
#define TEST_PATTERN 0x49524953u /* "IRIS" */

/* Evergreen PM4 definitions used by the Radeon kernel command parser. */
#define PM4_PACKET3(op, n) \
    ((3u << 30) | (((uint32_t)(op) & 0xffu) << 8) | \
     (((uint32_t)(n) & 0x3fffu) << 16))
#define PM4_PACKET3_EVENT_WRITE_EOP 0x47u
#define PM4_EVENT_TYPE(x) ((uint32_t)(x) << 0)
#define PM4_EVENT_INDEX(x) ((uint32_t)(x) << 8)
#define PM4_CACHE_FLUSH_AND_INV_EVENT_TS 0x14u
#define PM4_DATA_SEL(x) ((uint32_t)(x) << 29)
#define PM4_INT_SEL(x) ((uint32_t)(x) << 24)

static int do_ioctl(int fd, unsigned long request, void *arg, const char *name)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        return 0;
    }
    return 1;
}

int main(void)
{
    struct igl_hw_device device;
    drmVersionPtr version;
    struct drm_radeon_gem_create create;
    struct drm_radeon_gem_mmap map;
    struct drm_radeon_gem_wait_idle wait;
    struct drm_radeon_gem_set_domain domain;
    struct drm_gem_close close_arg;
    struct drm_radeon_cs_reloc reloc;
    struct drm_radeon_cs_chunk chunks[3];
    struct drm_radeon_cs cs;
    uint64_t chunk_ptrs[3];
    uint32_t ib[6];
    uint32_t flags[3];
    volatile uint32_t *words;
    void *mapping = MAP_FAILED;
    unsigned int i;
    int fd;
    int ok = 0;

    if (!igl_hw_probe(&device)) {
        fprintf(stderr, "Juniper XT Radeon HD 6770 not found\n");
        return 1;
    }

    if (!device.drm_render[0]) {
        fprintf(stderr, "Juniper render node not found\n");
        return 1;
    }

    fd = open(device.drm_render, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", device.drm_render, strerror(errno));
        return 1;
    }

    version = drmGetVersion(fd);
    if (!version) {
        fprintf(stderr, "drmGetVersion failed\n");
        goto out_close_fd;
    }

    if (version->name_len != 6 || strncmp(version->name, "radeon", 6) != 0) {
        fprintf(stderr, "expected radeon DRM driver\n");
        drmFreeVersion(version);
        goto out_close_fd;
    }
    drmFreeVersion(version);

    memset(&create, 0, sizeof(create));
    create.size = TEST_BO_SIZE;
    create.alignment = 4096;
    create.initial_domain = RADEON_GEM_DOMAIN_GTT;
    create.flags = RADEON_GEM_CPU_ACCESS;

    if (!do_ioctl(fd, DRM_IOCTL_RADEON_GEM_CREATE, &create,
                  "DRM_IOCTL_RADEON_GEM_CREATE"))
        goto out_close_fd;

    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    map.size = TEST_BO_SIZE;
    if (!do_ioctl(fd, DRM_IOCTL_RADEON_GEM_MMAP, &map,
                  "DRM_IOCTL_RADEON_GEM_MMAP"))
        goto out_close_bo;

    mapping = mmap(NULL, TEST_BO_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, (off_t)map.addr_ptr);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "mmap BO: %s\n", strerror(errno));
        goto out_close_bo;
    }

    words = (volatile uint32_t *)mapping;
    words[0] = 0;

    /*
     * EVENT_WRITE_EOP with DATA_SEL=1 asks the command processor to write
     * the low 32-bit payload to memory at end-of-pipe. The Radeon CS parser
     * replaces the zero address below with the next relocation's GPU address.
     */
    ib[0] = PM4_PACKET3(PM4_PACKET3_EVENT_WRITE_EOP, 4);
    ib[1] = PM4_EVENT_TYPE(PM4_CACHE_FLUSH_AND_INV_EVENT_TS) |
            PM4_EVENT_INDEX(5);
    ib[2] = 0; /* relocation supplies address low */
    ib[3] = PM4_DATA_SEL(1) | PM4_INT_SEL(2); /* address high patched here */
    ib[4] = TEST_PATTERN;
    ib[5] = 0;

    memset(&reloc, 0, sizeof(reloc));
    reloc.handle = create.handle;
    reloc.read_domains = RADEON_GEM_DOMAIN_GTT;
    reloc.write_domain = RADEON_GEM_DOMAIN_GTT;

    flags[0] = 0;
    flags[1] = RADEON_CS_RING_GFX;
    flags[2] = 0;

    memset(chunks, 0, sizeof(chunks));
    chunks[0].chunk_id = RADEON_CHUNK_ID_IB;
    chunks[0].length_dw = sizeof(ib) / sizeof(ib[0]);
    chunks[0].chunk_data = (uint64_t)(uintptr_t)ib;

    chunks[1].chunk_id = RADEON_CHUNK_ID_RELOCS;
    chunks[1].length_dw = sizeof(reloc) / sizeof(uint32_t);
    chunks[1].chunk_data = (uint64_t)(uintptr_t)&reloc;

    chunks[2].chunk_id = RADEON_CHUNK_ID_FLAGS;
    chunks[2].length_dw = 3;
    chunks[2].chunk_data = (uint64_t)(uintptr_t)flags;

    for (i = 0; i < 3; ++i)
        chunk_ptrs[i] = (uint64_t)(uintptr_t)&chunks[i];

    memset(&cs, 0, sizeof(cs));
    cs.num_chunks = 3;
    cs.chunks = (uint64_t)(uintptr_t)chunk_ptrs;

    printf("node: %s\n", device.drm_render);
    printf("scratch before GPU: 0x%08x\n", (uint32_t)words[0]);
    printf("submitting EVENT_WRITE_EOP payload 0x%08x\n", TEST_PATTERN);

    if (!do_ioctl(fd, DRM_IOCTL_RADEON_CS, &cs, "DRM_IOCTL_RADEON_CS"))
        goto out_unmap;

    memset(&wait, 0, sizeof(wait));
    wait.handle = create.handle;
    for (;;) {
        if (ioctl(fd, DRM_IOCTL_RADEON_GEM_WAIT_IDLE, &wait) == 0)
            break;
        if (errno == EINTR || errno == EBUSY)
            continue;
        fprintf(stderr, "DRM_IOCTL_RADEON_GEM_WAIT_IDLE: %s\n",
                strerror(errno));
        goto out_unmap;
    }

    memset(&domain, 0, sizeof(domain));
    domain.handle = create.handle;
    domain.read_domains = RADEON_GEM_DOMAIN_CPU;
    domain.write_domain = RADEON_GEM_DOMAIN_CPU;
    if (!do_ioctl(fd, DRM_IOCTL_RADEON_GEM_SET_DOMAIN, &domain,
                  "DRM_IOCTL_RADEON_GEM_SET_DOMAIN"))
        goto out_unmap;

    printf("scratch after GPU:  0x%08x\n", (uint32_t)words[0]);
    if (words[0] != TEST_PATTERN) {
        fprintf(stderr, "GPU write verification: FAIL\n");
        goto out_unmap;
    }

    printf("radeon cs submit: PASS\n");
    printf("native Juniper GPU write: PASS\n");
    ok = 1;

out_unmap:
    if (mapping != MAP_FAILED)
        munmap(mapping, TEST_BO_SIZE);
out_close_bo:
    memset(&close_arg, 0, sizeof(close_arg));
    close_arg.handle = create.handle;
    if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_arg) < 0)
        fprintf(stderr, "DRM_IOCTL_GEM_CLOSE: %s\n", strerror(errno));
out_close_fd:
    close(fd);
    return ok ? 0 : 1;
}
