#include <Library/BaseMemoryLib.h>

#include <style.h>
#include <stand_htport.h>

#include "gfxgui_bringup.h"

/* Pristine stand/arcs/lib/libsk/graphics/htport.c owns these interfaces.
 * Keep the UEFI bringup dependent only on the textport contract instead of
 * dragging the full libsk.h machine-dependent header graph into X64. */
extern struct htp_state *gethtp(void);
extern void htp_init(void);

EFI_STATUS
ArcsGfxGuiBringupInit(
    ARCS_GFXGUI_BRINGUP *State,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    struct htp_font *Fonts
    )
{
    EFI_STATUS Status;

    if (!State || !Gop)
        return EFI_INVALID_PARAMETER;

    ZeroMem(State, sizeof(*State));

    /* Pristine htport.c owns hosttextport/htp.  UEFI supplies only the
     * hardware function vector and GOP state underneath it. */
    State->Htp = gethtp();
    if (!State->Htp)
        return EFI_NOT_READY;

    Status = ArcsGopHtpAttach(&State->GopBackend, Gop, State->Htp);
    if (EFI_ERROR(Status))
        return Status;

    /* htp_init() performs the original SGI txConfig/initFonts/txReset flow,
     * initializes gfxgui against the canonical htp, and leaves video in the
     * original VOFF state until the first textport write.  The X64 product
     * splash is supplied directly through the original clogo_* contract. */
    htp_init();

    /* The bring-up font argument predates pristine htport ownership.  Keep
     * the ABI temporarily while callers are collapsed onto htp_init(). */
    (void)Fonts;

    return EFI_SUCCESS;
}

void
ArcsGfxGuiBringupMapPalette(
    ARCS_GFXGUI_BRINGUP *State
    )
{
    /* Palette ownership moved back to pristine htport.c::txMap(), reached by
     * htp_init().  Retain this entry point while old bring-up callers vanish. */
    (void)State;
}
