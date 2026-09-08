#include <gfxgui.h>
#include <style.h>
#include <pcbm/pcbm.h>
#include <pcbm/sysstart.h>
#include <pcbm/install.h>
#include <pcbm/diags.h>
#include <pcbm/recover.h>
#include <pcbm/console.h>

#include "maintenance_menu_bringup.h"
#include "uefi_boot.h"

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

static struct Button *MenuButtons[5];

static void
MaintenanceMenuCallBack(struct Button *Button, __scunsigned_t Value)
{
    (void)Button;
    ArcsMaintenanceMenuSelect((UINTN)Value);
}

void
ArcsMaintenanceMenuSelect(UINTN OneBasedIndex)
{
    EFI_STATUS Status;
    UINTN I;

    if (OneBasedIndex < 1 || OneBasedIndex > 5)
        return;

    for (I = 0; I < 5; ++I) {
        if (!MenuButtons[I])
            continue;

        stickButton(MenuButtons[I], I == (OneBasedIndex - 1));
        drawObject(guiobj(MenuButtons[I]));
    }

    /* The original PROM menu dispatches item 1 to menu_boot_sys(), which
     * enters autoboot() and ultimately calls ARCS Execute().  Until the full
     * libsc autoboot/environment layer is linked into the UEFI build, keep
     * that menu semantics and cross the platform boundary here: the UEFI
     * backend performs the final image-load/execute operation.
     */
    if (OneBasedIndex == 1) {
        Status = ArcsUefiBootDefault();
        (void)Status;

        /* A booted OS does not return.  If the child image fails or returns,
         * restore the maintenance menu rather than leaving Start System
         * latched as if execution were still in progress.
         */
        if (MenuButtons[0]) {
            stickButton(MenuButtons[0], 0);
            drawObject(guiobj(MenuButtons[0]));
        }
    }
}

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
        MenuButtons[I] = Button;
        addButtonImage(Button, MenuIcons[I]);
        addButtonCallBack(
            Button,
            MaintenanceMenuCallBack,
            (__scunsigned_t)(I + 1)
            );

        moveTextPoint(Bx + MENUBUTTW + 10, Base + 15);
        (void)createText((char *)MenuItems[I], ncenB18);

        Base -= MENUBUTTH + MENUVSPACE;
    }

    guiRefresh();
}
