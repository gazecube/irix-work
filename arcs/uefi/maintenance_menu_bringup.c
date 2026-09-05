#include <gfxgui.h>
#include <style.h>
#include <pcbm/pcbm.h>
#include <pcbm/sysstart.h>
#include <pcbm/install.h>
#include <pcbm/diags.h>
#include <pcbm/recover.h>
#include <pcbm/console.h>

#include "maintenance_menu_bringup.h"

static const char *MenuItems[] = {
    "Start System",
    "Install System Software",
    "Run Diagnostics",
    "Recover System",
    "Enter Command Monitor"
};

static struct pcbm *MenuIcons[] = {
    &sysstart,
    &install,
    &diags,
    &recover,
    &console
};

void
ArcsDrawMaintenanceMenuBringup(void)
{
    int MenuH;
    int GfxH;
    int AvailH;
    int Base;
    int I;

    /* Match the original graphical display_menu() layout from libsc/menu.c:
     * with the logo present, the menu is centered in the upper two-thirds of
     * the screen.  Each item is a 50x50 icon button with its label to the
     * right and a 25-pixel vertical gap. */
    MenuH = 5 * MENUBUTTH + 4 * MENUVSPACE;
    GfxH = gfxHeight();
    AvailH = noLogo() ? GfxH : ((GfxH << 1) / 3);
    Base = GfxH - ((AvailH - MenuH) >> 1) - MENUBUTTH;

    for (I = 0; I < 5; ++I) {
        struct Button *Button;
        int Bx = MSGX;

        Button = createButton(Bx, Base, MENUBUTTW, MENUBUTTH);
        addButtonImage(Button, MenuIcons[I]);

        moveTextPoint(Bx + MENUBUTTW + 10, Base + 15);
        (void)createText((char *)MenuItems[I], ncenB18);

        Base -= MENUBUTTH + MENUVSPACE;
    }

    guiRefresh();
}
