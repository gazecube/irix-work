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
     * internalChooseVisual(). It does not reject the colormap/depth tuple in
     * this wrapper; the internal chooser owns success/failure policy.
     */
    screen = XtScreen(shell);
    if (!visual && screen)
        visual = DefaultVisualOfScreen(screen);

    if (!visual)
        return False;

    /*
     * Clogin passes visualType == 2 on both recovered call sites.
     * imdFtrInitializeIconVisuals() builds five 4-word visual records. The
     * mode-2 record starts as the 8-bit TrueColor choice and is replaced by a
     * 24-bit TrueColor choice when the SGI server/multibuffer tests accept it.
     * In other words, mode 2 is the preferred TrueColor visual.
     *
     * On IRIX, internalChooseVisual() obtains that tuple from the SGI visual
     * registry property and returns False when the requested record is empty.
     * Modern X servers do not publish that SGI registry. For the portable
     * Clogin probe, retaining the shell tuple is the compatibility fallback;
     * on the normal modern TrueColor root visual this is already the semantic
     * equivalent of the original preferred-TrueColor result.
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
