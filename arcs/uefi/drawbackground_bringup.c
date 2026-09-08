#include <Uefi.h>

#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "gfxgui_bringup.h"

static ARCS_GFXGUI_BRINGUP Bringup;

EFI_STATUS
ArcsDrawBackgroundBringup(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_STATUS Status;

    /* Pristine htport.c now owns the original SGI font table and initializes
     * it in htp_init().  Keep UEFI below that ownership boundary. */
    Status = ArcsGfxGuiBringupInit(&Bringup, Gop, NULL);
    if (EFI_ERROR(Status))
        return Status;

    /* htp_init() has already initialized gfxgui against the canonical SGI
     * hosttextport.  Draw the original root presentation on that state. */
    drawBackground();

    /* Match the original PROM startup ordering: initialize the graphical
     * environment and background first, but do not enter/display the System
     * Maintenance Menu until startup()/autoboot() has returned. */

    return EFI_SUCCESS;
}

void
ArcsRedrawAfterReturnedImage(void)
{
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
        if (Bringup.Htp) {
            Bringup.Htp->xscreensize = (INT32)Bringup.GopBackend.Width;
            Bringup.Htp->yscreensize = (INT32)Bringup.GopBackend.Height;
        }
    }

    drawBackground();
    guiRefresh();
}
