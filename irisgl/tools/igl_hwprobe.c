#include <stdio.h>

#include "igl_hw.h"

int main(void)
{
    struct igl_hw_device device;

    if (!igl_hw_probe(&device)) {
        puts("No supported IRIS GL hardware target found.");
        return 1;
    }

    printf("family: %s\n", igl_hw_family_name(device.family));
    printf("chip: %s\n", igl_hw_chip_name(device.chip));
    printf("pci: %04x:%04x\n",
           (unsigned int)device.vendor_id,
           (unsigned int)device.device_id);
    printf("drm card: %s\n",
           device.drm_card[0] ? device.drm_card : "none");
    printf("drm render: %s\n",
           device.drm_render[0] ? device.drm_render : "none");

    return 0;
}
