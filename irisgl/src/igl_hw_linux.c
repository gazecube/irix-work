#include "igl_hw.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IGL_AMD_VENDOR_ID 0x1002u
#define IGL_AMD_JUNIPER_XT_HD6770 0x68bau

static int igl_hw_read_hex_file(const char *path, unsigned int *value)
{
    FILE *fp;
    char buf[64];
    char *end;
    unsigned long parsed;

    fp = fopen(path, "r");
    if (!fp)
        return 0;

    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    errno = 0;
    parsed = strtoul(buf, &end, 0);
    if (errno != 0 || end == buf || parsed > 0xfffful)
        return 0;

    *value = (unsigned int)parsed;
    return 1;
}

static int igl_hw_is_card_entry(const char *name)
{
    const char *p;

    if (strncmp(name, "card", 4) != 0)
        return 0;

    p = name + 4;
    if (*p == '\0')
        return 0;

    while (*p) {
        if (*p < '0' || *p > '9')
            return 0;
        ++p;
    }

    return 1;
}

static void igl_hw_find_render_node(struct igl_hw_device *device,
                                    const char *card_name)
{
    DIR *dir;
    struct dirent *entry;
    char path[256];
    char linkpath[256];
    char target[256];
    ssize_t length;

    dir = opendir("/sys/class/drm");
    if (!dir)
        return;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "renderD", 7) != 0)
            continue;

        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/drm/%s",
                 card_name, entry->d_name);
        if (access(path, F_OK) == 0) {
            snprintf(device->drm_render, sizeof(device->drm_render),
                     "/dev/dri/%s", entry->d_name);
            break;
        }

        snprintf(linkpath, sizeof(linkpath), "/sys/class/drm/%s/device",
                 entry->d_name);
        length = readlink(linkpath, target, sizeof(target) - 1);
        if (length <= 0)
            continue;
        target[length] = '\0';

        if (strstr(target, card_name) != NULL) {
            snprintf(device->drm_render, sizeof(device->drm_render),
                     "/dev/dri/%s", entry->d_name);
            break;
        }
    }

    closedir(dir);
}

int igl_hw_probe(struct igl_hw_device *device)
{
    DIR *dir;
    struct dirent *entry;
    char path[256];
    unsigned int vendor;
    unsigned int product;

    if (!device)
        return 0;

    memset(device, 0, sizeof(*device));

    dir = opendir("/sys/class/drm");
    if (!dir)
        return 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!igl_hw_is_card_entry(entry->d_name))
            continue;

        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/vendor",
                 entry->d_name);
        if (!igl_hw_read_hex_file(path, &vendor))
            continue;

        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/device",
                 entry->d_name);
        if (!igl_hw_read_hex_file(path, &product))
            continue;

        if (vendor != IGL_AMD_VENDOR_ID ||
            product != IGL_AMD_JUNIPER_XT_HD6770)
            continue;

        device->family = IGL_HW_FAMILY_AMD_R600;
        device->chip = IGL_HW_CHIP_AMD_JUNIPER;
        device->vendor_id = (uint16_t)vendor;
        device->device_id = (uint16_t)product;
        snprintf(device->drm_card, sizeof(device->drm_card),
                 "/dev/dri/%s", entry->d_name);
        igl_hw_find_render_node(device, entry->d_name);
        closedir(dir);
        return 1;
    }

    closedir(dir);
    return 0;
}

const char *igl_hw_family_name(enum igl_hw_family family)
{
    switch (family) {
    case IGL_HW_FAMILY_AMD_R600:
        return "AMD R600-family";
    case IGL_HW_FAMILY_NONE:
    default:
        return "none";
    }
}

const char *igl_hw_chip_name(enum igl_hw_chip chip)
{
    switch (chip) {
    case IGL_HW_CHIP_AMD_JUNIPER:
        return "Juniper XT (Radeon HD 6770)";
    case IGL_HW_CHIP_UNKNOWN:
    default:
        return "unknown";
    }
}
