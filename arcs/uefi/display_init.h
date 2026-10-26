#ifndef ARCS_UEFI_DISPLAY_INIT_H
#define ARCS_UEFI_DISPLAY_INIT_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

/* Choose the GOP (one per GPU may exist) with a 1280x1024 mode, else the
 * largest one; log everything to \\EFI\\ARCS\\arcs.log on the ESP. */
EFI_GRAPHICS_OUTPUT_PROTOCOL *ArcsUefiPickGop(void);

EFI_GRAPHICS_OUTPUT_PROTOCOL *ArcsUefiGetGop(VOID);

EFI_STATUS ArcsUefiPrepareDisplay(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop);

#endif
