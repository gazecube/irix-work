#include "igl_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <xf86drm.h>
#include <radeon_drm.h>

#define TEST_BO_SIZE 4096u
#define PM4_PACKET2_NOP 0x80000000u
#define TEST_IB_DWORDS 16u

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
    struct drm_radeon_gem_wait_idle wait;
    struct drm_gem_close close_arg;
    struct drm_radeon_cs_reloc reloc;
    struct drm_radeon_cs_chunk chunks[3];
    struct drm_radeon_cs cs;
    uint64_t chunk_ptrs[3];
    uint32_t ib[TEST_IB_DWORDS];
    uint32_t flags[3];
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

    for (i = 0; i < TEST_IB_DWORDS; ++i)
        ib[i] = PM4_PACKET2_NOP;

    memset(&reloc, 0, sizeof(reloc));
    reloc.handle = create.handle;
    reloc.read_domains = RADEON_GEM_DOMAIN_GTT;
    reloc.write_domain = RADEON_GEM_DOMAIN_GTT;

    flags[0] = 0;
    flags[1] = RADEON_CS_RING_GFX;
    flags[2] = 0;

    memset(chunks, 0, sizeof(chunks));

    chunks[0].chunk_id = RADEON_CHUNK_ID_IB;
    chunks[0].length_dw = TEST_IB_DWORDS;
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
    printf("submitting %u PM4 PACKET2 no-ops to Radeon GFX ring\n",
           TEST_IB_DWORDS);

    if (!do_ioctl(fd, DRM_IOCTL_RADEON_CS, &cs, "DRM_IOCTL_RADEON_CS"))
        goto out_close_bo;

    memset(&wait, 0, sizeof(wait));
    wait.handle = create.handle;

    for (;;) {
        if (ioctl(fd, DRM_IOCTL_RADEON_GEM_WAIT_IDLE, &wait) == 0)
            break;
        if (errno == EINTR || errno == EBUSY)
            continue;
        fprintf(stderr, "DRM_IOCTL_RADEON_GEM_WAIT_IDLE: %s\n",
                strerror(errno));
        goto out_close_bo;
    }

    printf("radeon cs submit: PASS\n");
    printf("gfx ring retirement: PASS\n");
    ok = 1;

out_close_bo:
    memset(&close_arg, 0, sizeof(close_arg));
    close_arg.handle = create.handle;
    if (ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_arg) < 0)
        fprintf(stderr, "DRM_IOCTL_GEM_CLOSE: %s\n", strerror(errno));
out_close_fd:
    close(fd);
    return ok ? 0 : 1;
}
