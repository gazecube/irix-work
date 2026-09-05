#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Intrinsic.h>
#include <stdio.h>

#include "Sgm/VisualDrawingA.h"

int main(int argc, char **argv)
{
    static String fallback_resources[] =
    {
        "*explicit.background: gray70",
        NULL
    };

    XtAppContext app;
    Widget shell;
    Widget stock;
    Widget drawing;
    Widget explicit_drawing;
    Arg args[2];
    Cardinal n;

    shell = XtVaAppInitialize(
        &app,
        "SgmSmoke",
        NULL,
        0,
        &argc,
        argv,
        fallback_resources,
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

    fprintf(stderr, "Sg smoke: creating default SgVisualDrawingArea\n");
    drawing = SgCreateVisualDrawingArea(shell, "drawing", NULL, 0);
    if (!drawing)
    {
        fprintf(stderr, "Sg smoke: failed to create default drawing area\n");
        XtDestroyWidget(shell);
        return 4;
    }

    fprintf(stderr, "Sg smoke: managing default widget\n");
    XtManageChild(drawing);

    fprintf(stderr, "Sg smoke: realizing shell\n");
    XtRealizeWidget(shell);
    fprintf(stderr, "Sg smoke: default widget realized successfully\n");

    XtDestroyWidget(drawing);
    fprintf(stderr, "Sg smoke: default widget destroyed\n");

    n = 0;
    XtSetArg(args[n], "visual", DefaultVisualOfScreen(XtScreen(shell))); ++n;
    XtSetArg(args[n], "ditherBackground", True); ++n;

    fprintf(stderr, "Sg smoke: creating explicit-visual dither widget\n");
    explicit_drawing = SgCreateVisualDrawingArea(shell, "explicit", args, n);
    if (!explicit_drawing)
    {
        fprintf(stderr, "Sg smoke: failed to create explicit-visual widget\n");
        XtDestroyWidget(shell);
        return 5;
    }

    fprintf(stderr, "Sg smoke: managing explicit-visual dither widget\n");
    XtManageChild(explicit_drawing);
    fprintf(stderr, "Sg smoke: explicit-visual dither widget realized successfully\n");

    XtDestroyWidget(shell);
    fprintf(stderr, "Sg smoke: shell destroyed successfully\n");

    return 0;
}
