#include <Uefi.h>

#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "original_fonts.h"
#include "gfxgui_bringup.h"
#include "maintenance_menu_bringup.h"

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

    initGfxGui(&Bringup.Htp);
    drawBackground();
    ArcsDrawMaintenanceMenuBringup();

    return EFI_SUCCESS;
}
