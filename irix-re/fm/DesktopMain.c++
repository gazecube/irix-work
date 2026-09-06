/*
 * DesktopMain.c++ - runnable IID background window host
 *
 * Binary-proven pieces used here:
 *   - fm background mode constructs ImdBackWindow("backWindow", ...)
 *   - desktop resources live below FileManager*rootShell*panel
 *   - desktop/background layout is BackLayout : ImdIconLayout
 *   - BackLayout operates on an ImdViewPanel and its adopted renderer
 *   - fm publishes the desktop child window through
 *     _SGI_DESKS_DESKTOP_WINDOW on the root window
 *   - Xsession.dt uses $HOME/.desktop-$host for the user desktop database
 *     directory and $HOME/.desktophost/$host for system/overlay desktop state
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
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SGI_DESKTOP_WINDOW_ATOM "_SGI_DESKS_DESKTOP_WINDOW"

struct DesktopState
{
    Widget shell;
    Widget panel;
    ImdRenderer *renderer;
    BackLayout *layout;
    char userDesktopDir[PATH_MAX];
    char systemDesktopDir[PATH_MAX];
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

static int
directoryExists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
resolveDesktopDirectories(DesktopState *state)
{
    const char *home = getenv("HOME");
    char host[256];

    if (!state || !home || !*home)
        return 0;

    memset(host, 0, sizeof(host));
    if (gethostname(host, sizeof(host) - 1) != 0 || !host[0])
        return 0;

    /*
     * RE: extracted IRIX Xsession.dt sets:
     *
     *   desktopdir=$HOME/.desktop-$host
     *   sysdesktopdir=.desktophost/$host
     *
     * Xsession runs this state beneath the user's home directory.  Keep both
     * paths absolute in the host reconstruction so the working directory of
     * iid-desktop cannot accidentally select the wrong desktop database.
     */
    if (snprintf(state->userDesktopDir,
                 sizeof(state->userDesktopDir),
                 "%s/.desktop-%s", home, host) >=
        (int)sizeof(state->userDesktopDir))
        return 0;

    if (snprintf(state->systemDesktopDir,
                 sizeof(state->systemDesktopDir),
                 "%s/.desktophost/%s", home, host) >=
        (int)sizeof(state->systemDesktopDir))
        return 0;

    return 1;
}

static void
reportDesktopDirectories(const DesktopState *state)
{
    if (!state)
        return;

    fprintf(stderr,
            "iid-desktop: user desktop directory: %s [%s]\n",
            state->userDesktopDir,
            directoryExists(state->userDesktopDir) ? "present" : "missing");
    fprintf(stderr,
            "iid-desktop: system desktop directory: %s [%s]\n",
            state->systemDesktopDir,
            directoryExists(state->systemDesktopDir) ? "present" : "missing");
}

static void
publishDesktopWindow(Widget panel)
{
    Display *display = XtDisplay(panel);
    Window root = RootWindowOfScreen(XtScreen(panel));
    Window desktop = XtWindow(panel);
    Atom property = XInternAtom(display, SGI_DESKTOP_WINDOW_ATOM, False);

    if (desktop == None || property == None)
        return;

    /*
     * RE: original fm calls setWindowAsProperty(root, desktopChild,
     * _SGI_DESKS_DESKTOP_WINDOW) during desktop initialization.
     */
    XChangeProperty(display,
                    root,
                    property,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    (unsigned char *)&desktop,
                    1);
}

static void
placeBehindApplications(Widget shell)
{
    Display *display = XtDisplay(shell);
    Window window = XtWindow(shell);

    if (window == None)
        return;

    /*
     * Host seam: the desktop surface occupies screen geometry but belongs at
     * the bottom of the top-level stack, underneath normal applications.
     */
    XLowerWindow(display, window);
    XFlush(display);
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    Display *display;
    DesktopState state;

    memset(&state, 0, sizeof(state));

    if (!resolveDesktopDirectories(&state)) {
        fprintf(stderr,
                "iid-desktop: cannot resolve IRIX host desktop directories\n");
        return 1;
    }

    reportDesktopDirectories(&state);

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
     * RE: the original app-defaults address the desktop as
     * FileManager*rootShell*panel. Use that proven shell name here.
     *
     * The override shell class is a host-only compatibility seam so the
     * runnable background surface is not decorated by the Linux WM while the
     * exact ImdBackWindow shell construction is still being recovered.
     */
    state.shell = XtAppCreateShell("rootShell", "FileManager",
                                   overrideShellWidgetClass,
                                   display, NULL, 0);
    sizeToScreen(state.shell);

    state.panel = ImdCreateViewPanel(state.shell,
                                     (char *)"panel",
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
    publishDesktopWindow(state.panel);
    placeBehindApplications(state.shell);

    fprintf(stderr,
            "iid-desktop: rootShell online behind applications; desktop model population pending binary proof\n");
    XtAppMainLoop(app);
    return 0;
}
