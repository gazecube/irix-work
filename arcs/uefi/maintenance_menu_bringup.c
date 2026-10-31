#include <gfxgui.h>
#include <style.h>
#include <pcbm/pcbm.h>
#include <pcbm/sysstart.h>
#include <pcbm/install.h>
#include <pcbm/diags.h>
#include <pcbm/recover.h>
#include <pcbm/console.h>

#include "drawbackground_bringup.h"
#include "maintenance_menu_bringup.h"
#include "bootmenu.h"
#include <Library/UefiBootServicesTableLib.h>
#include "manualmode_bringup.h"

extern int menu_boot_sys(void);
extern int ArcsBootVerbose;

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
static struct Text *MenuTexts[5];
static int MenuParkDy;      /* nonzero while the items are parked off screen */

static void
MaintenanceMenuCallBack(struct Button *Button, __scunsigned_t Value)
{
    (void)Button;
    ArcsMaintenanceMenuSelect((UINTN)Value);
}

void
ArcsMaintenanceMenuSelect(UINTN OneBasedIndex)
{
    UINTN I;

    if (OneBasedIndex < 1 || OneBasedIndex > 5)
        return;

    for (I = 0; I < 5; ++I) {
        if (!MenuButtons[I])
            continue;

        stickButton(MenuButtons[I], I == (OneBasedIndex - 1));
        drawObject(guiobj(MenuButtons[I]));
    }

    /* Original pmenu_cmd.c dispatches item 1 to menu_boot_sys().  That path
     * now enters the imported SystemPartition/OSLoader boot policy and reaches
     * UEFI only at the reconstructed Execute() boundary.
     */
    if (OneBasedIndex == 1) {
        /* Start System: the picker when there is a choice, then the normal
         * autoboot path with its "Starting up the system..." box.  Holding
         * Shift skips the box and boots the verbose textport. */
        BOOLEAN Shift = ArcsBootMenuShiftHeld(gST);

        if (ArcsBootMenuStartSystem(gST) == ARCS_START_PROCEED) {
            ArcsBootVerbose = Shift ? 1 : 0;
            (void)menu_boot_sys();
            ArcsBootVerbose = 0;
        }

        /* A booted OS does not return.  If an EFI utility/loader does return,
         * the Execute backend has restored GOP.  Release the latched Start
         * System state and repaint the existing PROM object tree.
         */
        if (MenuButtons[0])
            stickButton(MenuButtons[0], 0);

        ArcsRedrawAfterReturnedImage();
        return;
    }

    /* Original prom_menu item 5 dispatches menu_manualmode(), then the PROM
     * main loop enters command_parser().  The UEFI bridge preserves those two
     * source-backed steps until the complete machine-dependent main loop is
     * linked. */
    if (OneBasedIndex == 5)
        ArcsEnterCommandMonitor();
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
        MenuTexts[I] = createText((char *)MenuItems[I], ncenB18);

        Base -= MENUBUTTH + MENUVSPACE;
    }

    guiRefresh();
}

/* Screen rectangle (GUI coordinates, inclusive) of a menu item: its icon button
 * and the label to the right of it.  Used by the boot menu's transitions. */
int
ArcsMenuItemRect(int Index, int *X1, int *Y1, int *X2, int *Y2)
{
    struct gui_obj *G;

    if (Index < 0 || Index > 4 || MenuButtons[Index] == 0 || MenuParkDy)
        return 0;
    G = guiobj(MenuButtons[Index]);
    *X1 = G->x1;
    *Y1 = G->y1;
    *X2 = G->x2 + 10 + textWidth(MenuItems[Index], ncenB18);
    *Y2 = G->y2;
    return 1;
}

/* Move the items off screen (their pixels are being replaced by the flyout) or
 * back.  Nothing is drawn; hover and clicks cannot reach a parked item. */
void
ArcsMenuPark(int Park)
{
    int I;
    int Dy = 4000;

    if ((Park != 0) == (MenuParkDy != 0))
        return;
    for (I = 0; I < 5; ++I) {
        if (MenuButtons[I])
            moveObject(guiobj(MenuButtons[I]), guiobj(MenuButtons[I])->x1,
                       guiobj(MenuButtons[I])->y1 + (Park ? Dy : -Dy));
        if (MenuTexts[I])
            moveObject(guiobj(MenuTexts[I]), guiobj(MenuTexts[I])->x1,
                       guiobj(MenuTexts[I])->y1 + (Park ? Dy : -Dy));
    }
    MenuParkDy = Park ? Dy : 0;
}
