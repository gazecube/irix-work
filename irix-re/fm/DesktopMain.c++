/*
 * DesktopMain.c++ - runnable IID background window host
 *
 * Binary-proven pieces used here:
 *   - fm background mode constructs ImdBackWindow("backWindow", ...)
 *   - desktop/background layout is BackLayout : ImdIconLayout
 *   - BackLayout operates on an ImdViewPanel and its adopted renderer
 *
 * The top-level Xt shell below is only a Linux host seam. It is not claimed
 * as the original ImdBackWindow implementation. Desktop model population is
 * intentionally deferred until the constructor/model path is proven.
 */

#include "BackLayout.h"
#include "ImdIconPanel.h"

#include <Xm/Xm.h>
#include <X11/Shell.h>

#include <stdio.h>

struct DesktopState
{
    Widget shell;
    Widget panel;
    ImdRenderer *renderer;
    BackLayout *layout;
};

static void
sizeToScreen(Widget shell)
{
    Screen *screen = XtScreen(shell);
    Dimension width = (Dimension)WidthOfScreen(screen);
    Dimension height = (Dimension)HeightOfScreen(screen);

    XtVaSetValues(shell,
                  XmNx, 0,
                  XmNy, 0,
                  XmNwidth, width,
                  XmNheight, height,
                  NULL);
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    DesktopState state;

    state.shell = XtVaAppInitialize(&app,
                                    (char *)"FileManager",
                                    NULL, 0,
                                    &argc, argv,
                                    NULL,
                                    XmNtitle, "Interactive Desktop",
                                    NULL);

    /*
     * RE: "backWindow" is the exact component name passed to the original
     * ImdBackWindow constructor from FmApp::processArgs() in -b mode.
     */
    XtVaSetValues(state.shell, XmNname, "backWindow", NULL);
    sizeToScreen(state.shell);

    state.panel = ImdCreateViewPanel(state.shell,
                                     (char *)"backgroundPanel",
                                     NULL, 0);
    XtVaSetValues(state.panel,
                  XmNx, 0,
                  XmNy, 0,
                  XmNwidth, (Dimension)WidthOfScreen(XtScreen(state.shell)),
                  XmNheight, (Dimension)HeightOfScreen(XtScreen(state.shell)),
                  NULL);
    XtManageChild(state.panel);

    /*
     * RE: the background path asks ImdViewPanel for its renderer. Reuse the
     * reconstructed IMD renderer abstraction rather than drawing desktop
     * icons independently.
     */
    state.renderer = new ImdInvertedTeeRenderer;
    ImdViewPanelAdoptRenderer(state.panel, state.renderer);

    /* RE: BackLayout derives from ImdIconLayout and owns desktop placement. */
    state.layout = new BackLayout(state.panel);
    ImdViewPanelAdoptLayout(state.panel, state.layout);

    XtRealizeWidget(state.shell);
    fprintf(stderr,
            "iid-desktop: backWindow online; desktop model population pending binary proof\n");
    XtAppMainLoop(app);
    return 0;
}
