#ifndef ARCS_UEFI_DISPLAY_INIT_H
#define ARCS_UEFI_DISPLAY_INIT_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

EFI_STATUS ArcsUefiPrepareDisplay(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop);

#endif
