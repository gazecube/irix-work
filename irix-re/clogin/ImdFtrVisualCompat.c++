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
    Screen *screen;
    Visual *visual = NULL;
    Colormap colormap = None;
    int depth = 0;

    if (auxVisualValue)
        *auxVisualValue = 0;

    if (!widget || !args || !count)
        return False;

    /*
     * Binary-proven wrapper behavior from IRIX libimdFtrIcon.so:
     * _XtCheckSubclassFlag(..., 0x20) and XtParent() are used to reach the
     * containing shell before reading visual/colormap/depth. XtIsShell is the
     * portable expression of the same relationship.
     */
    shell = findVisualShell(widget);
    if (!shell)
        return False;

    XtVaGetValues(shell,
                  XtNvisual, &visual,
                  XtNcolormap, &colormap,
                  XtNdepth, &depth,
                  NULL);

    /*
     * The original explicitly tolerates a NULL XtNvisual. In that case it
     * calls XtScreen(shell) and substitutes Screen.root_visual before entering
     * the internal SGI visual chooser. Do not reject NULL here.
     */
    screen = XtScreen(shell);
    if (!visual && screen)
        visual = DefaultVisualOfScreen(screen);

    if (!visual || colormap == None || depth <= 0)
        return False;

    /*
     * IRIX FastStrDefs.h / machine-code resource IDs:
     *   -39 = XtNvisual
     *   -33 = XtNcolormap
     *   -34 = XtNdepth
     * The original appends the three Arg entries in exactly this order.
     *
     * Clogin passes visualType == 2 on both recovered call sites. The SGI
     * mode-2 chooser consults a cached per-screen visual table populated from
     * a root-window property before it may replace this tuple. That policy is
     * still being recovered. Until it is complete, preserving a valid shell
     * tuple is the compatibility fallback; keep the gap explicit here rather
     * than inventing a different visual-selection rule.
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
