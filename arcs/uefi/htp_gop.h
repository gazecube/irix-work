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

/* The backend of the PROM's GUI (gfxgui) over the real GOP. */
ARCS_GOP_HTP *ArcsGopBackend(VOID);

/* Draw somewhere else for a while: removes the software cursor from the real
 * screen, hides it, and points every GUI primitive at New.  Redirect back
 * with the saved state; Compose redraws the cursor at the end. */
EFI_GRAPHICS_OUTPUT_PROTOCOL *
ArcsGopRedirectBegin(ARCS_GOP_HTP *Backend, EFI_GRAPHICS_OUTPUT_PROTOCOL *New, BOOLEAN *SavedVisible);

VOID
ArcsGopRedirectEnd(ARCS_GOP_HTP *Backend, EFI_GRAPHICS_OUTPUT_PROTOCOL *Old, BOOLEAN SavedVisible, BOOLEAN Compose);

VOID
ArcsGopCursorRefresh(
    ARCS_GOP_HTP *Backend
    );

#endif
