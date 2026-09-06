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
#include <X11/StringDefs.h>

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
                  XtNx, 0,
                  XtNy, 0,
                  XtNwidth, width,
                  XtNheight, height,
                  NULL);
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    Display *display;
    DesktopState state;

    XtToolkitInitialize();
    app = XtCreateApplicationContext();
    display = XtOpenDisplay(app, NULL,
                            "backWindow", "FileManager",
                            NULL, 0, &argc, argv);
    if (!display) {
        fprintf(stderr, "iid-desktop: cannot open display\n");
        return 1;
    }

    /*
     * RE: "backWindow" is the exact component name passed to the original
     * ImdBackWindow constructor from FmApp::processArgs() in -b mode.
     */
    state.shell = XtAppCreateShell("backWindow", "FileManager",
                                   applicationShellWidgetClass,
                                   display, NULL, 0);
    sizeToScreen(state.shell);

    state.panel = ImdCreateViewPanel(state.shell,
                                     (char *)"backgroundPanel",
                                     NULL, 0);
    XtVaSetValues(state.panel,
                  XtNx, 0,
                  XtNy, 0,
                  XtNwidth, (Dimension)WidthOfScreen(XtScreen(state.shell)),
                  XtNheight, (Dimension)HeightOfScreen(XtScreen(state.shell)),
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
