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

void
ArcsRedrawAfterReturnedImage(void)
{
    /* The EFI child owned the framebuffer while it was running.  Its contents
     * and any software-cursor save-under pixels are therefore no longer valid.
     * The boot backend has already restored the GOP mode before we get here.
     * Reuse the existing SGI object tree and original draw path rather than
     * creating a second maintenance menu or re-running startup initialization.
     */
    Bringup.GopBackend.CursorDrawn = FALSE;
    Bringup.GopBackend.CursorSaveW = 0;
    Bringup.GopBackend.CursorSaveH = 0;

    if (Bringup.GopBackend.Gop != NULL &&
        Bringup.GopBackend.Gop->Mode != NULL &&
        Bringup.GopBackend.Gop->Mode->Info != NULL) {
        Bringup.GopBackend.Width =
            Bringup.GopBackend.Gop->Mode->Info->HorizontalResolution;
        Bringup.GopBackend.Height =
            Bringup.GopBackend.Gop->Mode->Info->VerticalResolution;
        Bringup.Htp.xscreensize = (INT32)Bringup.GopBackend.Width;
        Bringup.Htp.yscreensize = (INT32)Bringup.GopBackend.Height;
    }

    drawBackground();
    guiRefresh();
}
