#include <Uefi.h>

#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "original_fonts.h"
#include "gfxgui_bringup.h"

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
     * htp_state is complete.  From here on, object ownership, dimensions,
     * redraw order and GUI state come from core.c rather than a bring-up
     * facsimile. */
    initGfxGui(&Bringup.Htp);
    drawBackground();

    return EFI_SUCCESS;
}
