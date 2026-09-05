#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Intrinsic.h>
#include <stdio.h>

#include "Sgm/VisualDrawingA.h"

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell;
    Widget stock;
    Widget drawing;

    shell = XtVaAppInitialize(
        &app,
        "SgmSmoke",
        NULL,
        0,
        &argc,
        argv,
        NULL,
        NULL);

    if (!shell)
    {
        fprintf(stderr, "Sg smoke: failed to create shell\n");
        return 2;
    }

    fprintf(stderr, "Sg smoke: initializing Sg widget class\n");
    XtInitializeWidgetClass(sgVisualDrawingAreaWidgetClass);
    fprintf(stderr, "Sg smoke: Sg widget class initialized\n");

    fprintf(stderr, "Sg smoke: creating stock XmDrawingArea control\n");
    stock = XmCreateDrawingArea(shell, "stock", NULL, 0);
    if (!stock)
    {
        fprintf(stderr, "Sg smoke: failed to create stock drawing area\n");
        XtDestroyWidget(shell);
        return 3;
    }
    fprintf(stderr, "Sg smoke: stock XmDrawingArea created\n");
    XtDestroyWidget(stock);
    fprintf(stderr, "Sg smoke: stock XmDrawingArea destroyed\n");

    fprintf(stderr, "Sg smoke: creating SgVisualDrawingArea\n");
    drawing = SgCreateVisualDrawingArea(shell, "drawing", NULL, 0);
    if (!drawing)
    {
        fprintf(stderr, "Sg smoke: failed to create drawing area\n");
        XtDestroyWidget(shell);
        return 4;
    }

    fprintf(stderr, "Sg smoke: managing widget\n");
    XtManageChild(drawing);

    fprintf(stderr, "Sg smoke: realizing shell\n");
    XtRealizeWidget(shell);

    fprintf(stderr, "Sg smoke: realized successfully\n");
    XtDestroyWidget(shell);
    fprintf(stderr, "Sg smoke: destroyed successfully\n");

    return 0;
}
