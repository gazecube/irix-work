#ifndef ARCS_UEFI_HTP_GOP_H
#define ARCS_UEFI_HTP_GOP_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include "../sgisrc/include/stand_htport.h"

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Palette[256];
    UINTN Width;
    UINTN Height;
    INTN CposX;
    INTN CposY;
    UINT8 ColorIndex;

    INTN CursorX;
    INTN CursorY;
    UINTN CursorSaveX;
    UINTN CursorSaveY;
    UINTN CursorSaveW;
    UINTN CursorSaveH;
    BOOLEAN CursorVisible;
    BOOLEAN CursorDrawn;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL CursorSave[16 * 16];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL CursorComposite[16 * 16];
} ARCS_GOP_HTP;

extern struct htp_fncs ArcsGopHtpFncs;

EFI_STATUS
ArcsGopHtpAttach(
    ARCS_GOP_HTP *Backend,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    struct htp_state *Htp
    );

VOID
ArcsGopCursorRefresh(
    ARCS_GOP_HTP *Backend
    );

#endif
