#ifndef IRISGL_IGL_HW_H
#define IRISGL_IGL_HW_H

#include <stdint.h>

/*
 * Reconstructed host-hardware boundary.
 *
 * This is not an original SGI interface.  It deliberately sits below the
 * reconstructed IRIS GL state machine so host GPU discovery and command
 * submission can evolve without leaking host architecture into the public
 * IRIS GL API.
 */
enum igl_hw_family {
    IGL_HW_FAMILY_NONE = 0,
    IGL_HW_FAMILY_AMD_R600
};

enum igl_hw_chip {
    IGL_HW_CHIP_UNKNOWN = 0,
    IGL_HW_CHIP_AMD_JUNIPER
};

struct igl_hw_device {
    enum igl_hw_family family;
    enum igl_hw_chip chip;
    uint16_t vendor_id;
    uint16_t device_id;
    char drm_card[64];
    char drm_render[64];
};

int igl_hw_probe(struct igl_hw_device *device);
const char *igl_hw_family_name(enum igl_hw_family family);
const char *igl_hw_chip_name(enum igl_hw_chip chip);

#endif /* IRISGL_IGL_HW_H */
