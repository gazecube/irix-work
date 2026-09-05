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

    /* Mirror the palette entries used by the original standalone txMap().
     * Keep palette policy here only until the complete original host textport
     * is linked; the widgets now consume the exact historical indices. */
    Fncs->mapcolor(Htp->hw, BLACK,       0,   0,   0);
    Fncs->mapcolor(Htp->hw, RED,       255,   0,   0);
    Fncs->mapcolor(Htp->hw, GREEN,       0, 255,   0);
    Fncs->mapcolor(Htp->hw, YELLOW,    255, 255,   0);
    Fncs->mapcolor(Htp->hw, BLUE,        0,   0, 255);
    Fncs->mapcolor(Htp->hw, MAGENTA,   255,   0, 255);
    Fncs->mapcolor(Htp->hw, CYAN,        0, 255, 255);
    Fncs->mapcolor(Htp->hw, WHITE,     240, 240, 240);

    Fncs->mapcolor(Htp->hw, BLACK2,      85,  85,  85);
    Fncs->mapcolor(Htp->hw, RED2,       198, 113, 113);
    Fncs->mapcolor(Htp->hw, GREEN2,     113, 198, 113);
    Fncs->mapcolor(Htp->hw, YELLOW2,    142, 142,  56);
    Fncs->mapcolor(Htp->hw, BLUE2,      113, 113, 198);
    Fncs->mapcolor(Htp->hw, MAGENTA2,   142,  56, 142);
    Fncs->mapcolor(Htp->hw, CYAN2,       56, 131, 131);
    Fncs->mapcolor(Htp->hw, WHITE2,     170, 170, 170);

    Fncs->mapcolor(Htp->hw, GRAY42,      42,  42,  42);
    Fncs->mapcolor(Htp->hw, GRAY85,      85,  85,  85);
    Fncs->mapcolor(Htp->hw, GRAY128,    128, 128, 128);
    Fncs->mapcolor(Htp->hw, GRAY170,    170, 170, 170);
    Fncs->mapcolor(Htp->hw, GRAY213,    213, 213, 213);

    Fncs->mapcolor(Htp->hw, lightGrayBlue,     85,  85, 213);
    Fncs->mapcolor(Htp->hw, lightBlue,        128, 128, 255);
    Fncs->mapcolor(Htp->hw, lightGrayCyan,     85, 213, 213);
    Fncs->mapcolor(Htp->hw, lightCyan,        128, 255, 255);
    Fncs->mapcolor(Htp->hw, lightGrayGreen,    85, 213,  85);
    Fncs->mapcolor(Htp->hw, lightGreen,       128, 255, 128);
    Fncs->mapcolor(Htp->hw, lightGrayMagenta, 213,  85, 213);
    Fncs->mapcolor(Htp->hw, lightMagenta,     255, 128, 255);
    Fncs->mapcolor(Htp->hw, lightGrayRed,     213,  85,  85);
    Fncs->mapcolor(Htp->hw, lightRed,         255, 128, 128);
    Fncs->mapcolor(Htp->hw, TerraCotta,       194, 142, 142);
    Fncs->mapcolor(Htp->hw, DarkTerraCotta,   142,  99,  99);
    Fncs->mapcolor(Htp->hw, SlateBlue,        113, 113, 198);

    Fncs->mapcolor(Htp->hw, COMPANYNAMECOLOR, 0xff, 0xff, 0x00);
    Fncs->mapcolor(Htp->hw, CLOGOCOLOR,        0x4b, 0x00, 0x83);
    Fncs->mapcolor(Htp->hw, CLOGOSHADOWCOLOR,  0x5f, 0x5f, 0x92);

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
