#ifndef ARCS_UEFI_GFXGUI_BRINGUP_H
#define ARCS_UEFI_GFXGUI_BRINGUP_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <stand_htport.h>

#include "htp_gop.h"

typedef struct {
    struct htp_state Htp;
    ARCS_GOP_HTP GopBackend;
} ARCS_GFXGUI_BRINGUP;

EFI_STATUS
ArcsGfxGuiBringupInit(
    ARCS_GFXGUI_BRINGUP *State,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    struct htp_font *Fonts
    );

void
ArcsGfxGuiBringupMapPalette(
    ARCS_GFXGUI_BRINGUP *State
    );

#endif
