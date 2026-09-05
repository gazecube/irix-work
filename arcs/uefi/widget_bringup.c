#include <gfxgui.h>
#include <style.h>

#include "widget_bringup.h"

static char MenuTitle[] = "System Maintenance Menu";

static const char *MenuItems[] = {
    "Start System",
    "Install System Software",
    "Run Diagnostics",
    "Recover System",
    "Enter Command Monitor"
};

void
ArcsDrawWidgetBringup(void)
{
    struct Canvas *Canvas;
    struct Button *Button;
    int CanvasW = 520;
    int CanvasH = 300;
    int ButtonW = 360;
    int ButtonH = TEXTBUTH;
    int ButtonGap = 12;
    int X;
    int Y;
    int TitleW;
    int TitleH;
    int I;

    Canvas = createCanvas(CanvasW, CanvasH);

    textSize(MenuTitle, &TitleW, &TitleH, ncenB18);
    X = Canvas->gui.x1 + ((CanvasW - TitleW) >> 1);
    Y = Canvas->gui.y2 - DIALOGBDW - DIALOGMARGIN - TitleH - 8;
    moveTextPoint(X, Y);
    (void)createText(MenuTitle, ncenB18);

    X = Canvas->gui.x1 + ((CanvasW - ButtonW) >> 1);
    Y = Canvas->gui.y2 - 82;

    for (I = 0; I < 5; ++I) {
        Button = createButton(X, Y, ButtonW, ButtonH);
        addButtonText(Button, MenuItems[I]);
        if (I == 0)
            setDefaultButton(Button, 1);
        Y -= ButtonH + ButtonGap;
    }

    /* Draw in object creation order: canvas, title, then buttons.  This is
     * the original SGI object list/redraw machinery; only the temporary
     * checkpoint layout above is UEFI-side scaffolding. */
    guiRefresh();
}
