#include "igl_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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
    struct drm_radeon_gem_info info;
    struct drm_radeon_gem_create create;
    struct drm_radeon_gem_mmap map;
    struct drm_gem_close close_arg;
    uint32_t *words;
    void *mapping;
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

    printf("node: %s\n", device.drm_render);
    printf("driver: %.*s\n", version->name_len, version->name);
    printf("drm version: %d.%d.%d\n",
           version->version_major,
           version->version_minor,
           version->version_patchlevel);

    if (version->name_len != 6 || strncmp(version->name, "radeon", 6) != 0) {
        fprintf(stderr, "expected radeon DRM driver\n");
        drmFreeVersion(version);
        goto out_close_fd;
    }
    drmFreeVersion(version);

    memset(&info, 0, sizeof(info));
    if (!do_ioctl(fd, DRM_IOCTL_RADEON_GEM_INFO, &info,
                  "DRM_IOCTL_RADEON_GEM_INFO"))
        goto out_close_fd;

    printf("gart: %" PRIu64 " bytes\n", (uint64_t)info.gart_size);
    printf("vram: %" PRIu64 " bytes\n", (uint64_t)info.vram_size);
    printf("visible vram: %" PRIu64 " bytes\n", (uint64_t)info.vram_visible);

    memset(&create, 0, sizeof(create));
    create.size = TEST_BO_SIZE;
    create.alignment = 4096;
    create.initial_domain = RADEON_GEM_DOMAIN_GTT;
    create.flags = RADEON_GEM_CPU_ACCESS;

    if (!do_ioctl(fd, DRM_IOCTL_RADEON_GEM_CREATE, &create,
                  "DRM_IOCTL_RADEON_GEM_CREATE"))
        goto out_close_fd;

    printf("bo handle: %u\n", create.handle);

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

    words = (uint32_t *)mapping;
    words[0] = TEST_PATTERN;
    if (words[0] != TEST_PATTERN) {
        fprintf(stderr, "BO readback mismatch: got 0x%08x\n", words[0]);
        munmap(mapping, TEST_BO_SIZE);
        goto out_close_bo;
    }

    printf("bo cpu map/readback: PASS (0x%08x)\n", words[0]);
    munmap(mapping, TEST_BO_SIZE);
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
