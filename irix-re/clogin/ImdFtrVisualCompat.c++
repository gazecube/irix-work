#include "ImdFtrVisualCompat.h"

#include <X11/Shell.h>
#include <X11/StringDefs.h>

namespace
{
Widget findVisualShell(Widget widget)
{
    Widget current = widget;

    while (current && !XtIsShell(current))
        current = XtParent(current);

    return current;
}
}

Boolean imdFtrChooseVisualArgs(Widget widget,
                               int visualType,
                               ArgList args,
                               Cardinal *count,
                               int *auxVisualValue)
{
    Widget shell;
    Visual *visual = NULL;
    Colormap colormap = None;
    int depth = 0;

    if (auxVisualValue)
        *auxVisualValue = 0;

    if (!widget || !args || !count)
        return False;

    /*
     * Binary-proven wrapper behavior: the original uses
     * _XtCheckSubclassFlag(..., 0x20) and XtParent() to reach the containing
     * shell before fetching these three resources. XtIsShell expresses the
     * same host-side relationship without relying on private Xt flag values.
     */
    shell = findVisualShell(widget);
    if (!shell)
        return False;

    XtVaGetValues(shell,
                  XtNvisual, &visual,
                  XtNcolormap, &colormap,
                  XtNdepth, &depth,
                  NULL);

    if (!visual || colormap == None || depth <= 0)
        return False;

    /*
     * IRIX FastStrDefs.h proves the three resource IDs used by the original:
     *   -39 = XtNvisual
     *   -33 = XtNcolormap
     *   -34 = XtNdepth
     * and the MIPS wrapper appends them in exactly this order.
     *
     * internalChooseVisual() may replace this tuple according to visualType.
     * The mode-2 visual-selection policy is still under reconstruction. On a
     * host without the SGI icon-visual registry, preserving the shell's valid
     * tuple is the compatibility behavior; keep this policy isolated here so
     * it can be replaced directly when that table is fully recovered.
     */
    (void)visualType;

    XtSetArg(args[*count], XtNvisual, visual);
    ++*count;
    XtSetArg(args[*count], XtNcolormap, colormap);
    ++*count;
    XtSetArg(args[*count], XtNdepth, depth);
    ++*count;

    return True;
}
