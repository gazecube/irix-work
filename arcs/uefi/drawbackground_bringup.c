#include <Uefi.h>

#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "original_fonts.h"
#include "gfxgui_bringup.h"
#include "widget_bringup.h"

static ARCS_GFXGUI_BRINGUP Bringup;
static struct htp_font Fonts[NUMFONTS];

EFI_STATUS
ArcsDrawBackgroundBringup(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_STATUS Status;

    ArcsInitOriginalFonts(Fonts);

    Status = ArcsGfxGuiBringupInit(&Bringup, Gop, Fonts);
    if (EFI_ERROR(Status))
        return Status;

    /* Hand control to the original SGI GUI core now that the UEFI-backed
     * htp_state is complete. */
    initGfxGui(&Bringup.Htp);
    drawBackground();

    /* First widget checkpoint: original SGI canvas/button/text objects and
     * original object-list redraw machinery, over the already-proven splash.
     * The temporary layout is replaced by the original menu mechanics next. */
    ArcsDrawWidgetBringup();

    return EFI_SUCCESS;
}
