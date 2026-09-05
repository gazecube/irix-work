#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "font_bridge.h"
#include "gfxgui_bringup.h"

struct gfxgui gfxgui;

static ARCS_GFXGUI_BRINGUP Bringup;
static struct htp_font Fonts[NUMFONTS];

int
gfxWidth(void)
{
    return gfxgui.htp ? gfxgui.htp->xscreensize : 0;
}

int
gfxHeight(void)
{
    return gfxgui.htp ? gfxgui.htp->yscreensize : 0;
}

EFI_STATUS
ArcsDrawBackgroundBringup(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_STATUS Status;

    ArcsInitBringupFonts(Fonts);

    Status = ArcsGfxGuiBringupInit(&Bringup, Gop, Fonts);
    if (EFI_ERROR(Status))
        return Status;

    ZeroMem(&gfxgui, sizeof(gfxgui));
    gfxgui.htp = &Bringup.Htp;
    gfxgui.fncs = Bringup.Htp.fncs;
    gfxgui.hw = Bringup.Htp.hw;
    gfxgui.next = gfxgui.prev = (struct gui_obj *)&gfxgui;

    /* First visual checkpoint: execute the original SGI renderer unchanged.
     * This temporary direct initialization is removed when core.c and the
     * original initGfxGui() dependency set are fully linked. */
    drawBackground();

    return EFI_SUCCESS;
}
