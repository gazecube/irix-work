#include <Library/BaseMemoryLib.h>

#include <style.h>

#include "gfxgui_bringup.h"
#include "profile_reality.h"

EFI_STATUS
ArcsGfxGuiBringupInit(
    ARCS_GFXGUI_BRINGUP *State,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    struct htp_font *Fonts
    )
{
    EFI_STATUS Status;

    if (!State || !Gop || !Fonts)
        return EFI_INVALID_PARAMETER;

    ZeroMem(State, sizeof(*State));

    Status = ArcsGopHtpAttach(&State->GopBackend, Gop, &State->Htp);
    if (EFI_ERROR(Status))
        return Status;

    State->Htp.fonts = Fonts;
    ArcsUseRealityLogo(&State->Htp);
    ArcsGfxGuiBringupMapPalette(State);

    return EFI_SUCCESS;
}

void
ArcsGfxGuiBringupMapPalette(
    ARCS_GFXGUI_BRINGUP *State
    )
{
    struct htp_fncs *Fncs;
    struct htp_state *Htp;
    int IncR, IncG, IncB;
    int R, G, B;
    int I;

    if (!State)
        return;

    Htp = &State->Htp;
    Fncs = Htp->fncs;
    if (!Fncs || !Fncs->mapcolor)
        return;

    /* Minimal palette needed by the original drawBackground() path.
     * These values are copied from the SGI standalone textport palette.
     * Replace this helper with the original txMap() once the complete
     * textport object is linked into the UEFI build. */
    Fncs->mapcolor(Htp->hw, COMPANYNAMECOLOR, 0xff, 0xff, 0x00);
    Fncs->mapcolor(Htp->hw, CLOGOCOLOR, 0x4b, 0x00, 0x83);
    Fncs->mapcolor(Htp->hw, CLOGOSHADOWCOLOR, 0x5f, 0x5f, 0x92);

    IncR = ((TOPCOLOR_R - BOTTOMCOLOR_R) << 16) / RAMPSIZE;
    IncG = ((TOPCOLOR_G - BOTTOMCOLOR_G) << 16) / RAMPSIZE;
    IncB = ((TOPCOLOR_B - BOTTOMCOLOR_B) << 16) / RAMPSIZE;
    R = BOTTOMCOLOR_R << 16;
    G = BOTTOMCOLOR_G << 16;
    B = BOTTOMCOLOR_B << 16;

    for (I = 0; I < RAMPSIZE; ++I) {
        Fncs->mapcolor(Htp->hw, I + RAMPSTART, R >> 16, G >> 16, B >> 16);
        R += IncR;
        G += IncG;
        B += IncB;
    }
}
