#ifndef ARCS_UEFI_DRAWBACKGROUND_BRINGUP_H
#define ARCS_UEFI_DRAWBACKGROUND_BRINGUP_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

EFI_STATUS
ArcsDrawBackgroundBringup(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop
    );

#endif
