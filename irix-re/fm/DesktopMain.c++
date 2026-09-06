/*
 * DesktopMain.c++ - runnable IID background window host
 *
 * Binary-proven pieces used here:
 *   - fm background mode constructs ImdBackWindow("backWindow", ...)
 *   - ImdBackWindow imports ImdModelBasedIconPanel construction directly
 *   - desktop resources live below FileManager*rootShell*panel
 *   - desktop/background layout is BackLayout : ImdIconLayout
 *   - BackLayout operates on an ImdViewPanel and its adopted renderer
 *   - fm publishes the desktop child window through
 *     _SGI_DESKS_DESKTOP_WINDOW on the root window
 *   - Xsession.dt uses $HOME/.desktop-$host for the user desktop database
 *     directory and $HOME/.desktophost/$host for system/overlay desktop state
 *
 * The top-level Xt shell below is only a Linux host seam. It is not claimed
 * as the original ImdBackWindow implementation. The panel class itself now
 * follows the binary-proven desktop construction path.
 */

#include "BackLayout.h"
#include "Broker.h"
#include "ImdIconPanel.h"
#include "Model.h"
#include "ModelBasedIconPanel.h"
#include "RefModel.h"

#include <Xm/Xm.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
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
    ImdBroker *broker;
    ImdModel *desktopModel;
    ImdModelBasedIconPanel *panel;
    Widget panelWidget;
    ImdRenderer *renderer;
    BackLayout *layout;
    char userDesktopDir[PATH_MAX];
    char systemDesktopDir[PATH_MAX];
    char systemDesktopIconDir[PATH_MAX];
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
ensureDirectory(const char *path)
{
    if (directoryExists(path))
        return 1;

    if (mkdir(path, 0700) == 0)
        return 1;

    return errno == EEXIST && directoryExists(path);
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

    if (snprintf(state->systemDesktopIconDir,
                 sizeof(state->systemDesktopIconDir),
                 "%s/Icons", state->systemDesktopDir) >=
        (int)sizeof(state->systemDesktopIconDir))
        return 0;

    return 1;
}

static int
prepareDesktopDirectories(const DesktopState *state)
{
    const char *home = getenv("HOME");
    char desktopHostRoot[PATH_MAX];

    if (!state || !home || !*home)
        return 0;

    if (snprintf(desktopHostRoot,
                 sizeof(desktopHostRoot),
                 "%s/.desktophost", home) >=
        (int)sizeof(desktopHostRoot))
        return 0;

    if (!ensureDirectory(state->userDesktopDir))
        return 0;
    if (!ensureDirectory(desktopHostRoot))
        return 0;
    if (!ensureDirectory(state->systemDesktopDir))
        return 0;
    if (!ensureDirectory(state->systemDesktopIconDir))
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
    fprintf(stderr,
            "iid-desktop: system icon directory: %s [%s]\n",
            state->systemDesktopIconDir,
            directoryExists(state->systemDesktopIconDir) ? "present" : "missing");
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

    if (!prepareDesktopDirectories(&state)) {
        fprintf(stderr,
                "iid-desktop: cannot prepare IRIX host desktop directories: %s\n",
                strerror(errno));
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

    state.shell = XtAppCreateShell("rootShell", "FileManager",
                                   overrideShellWidgetClass,
                                   display, NULL, 0);
    sizeToScreen(state.shell);

    const ImdDNA desktopDNA = ImdRefModel::desktopContainerDNA();
    state.broker = ImdBroker::contextBroker();
    state.desktopModel = state.broker->getModel(desktopDNA);
    if (!state.desktopModel) {
        fprintf(stderr,
                "iid-desktop: broker could not create desktop container model: %s\n",
                desktopDNA.serialize().c_str());
        return 1;
    }

    fprintf(stderr,
            "iid-desktop: desktop container DNA: %s\n",
            desktopDNA.serialize().c_str());

    /*
     * Original model-backed construction now reaches the recovered root
     * NotifyReceiver. ImdDirModel's child callback registration sends the
     * original 0xEFFF004B/4C/4D existence batch synchronously, creating and
     * adopting one ImdModelBasedIconView per current desktop child.
     */
    state.panel = new ImdModelBasedIconPanel("panel",
                                             state.shell,
                                             state.desktopModel,
                                             0,
                                             0);
    state.panelWidget = state.panel->baseWidget();

    fprintf(stderr,
            "iid-desktop: root NotifyReceiver active; adopted %lu model-backed views\n",
            (unsigned long)state.panel->portableViews().size());

    XtVaSetValues(state.panelWidget,
                  XtNx, 0,
                  XtNy, 0,
                  XtNwidth, (Dimension)WidthOfScreen(XtScreen(state.shell)),
                  XtNheight, (Dimension)HeightOfScreen(XtScreen(state.shell)),
                  NULL);
    XtManageChild(state.panelWidget);

    state.renderer = new ImdInvertedTeeRenderer;
    ImdViewPanelAdoptRenderer(state.panelWidget, state.renderer);

    state.layout = new BackLayout(state.panelWidget);
    ImdViewPanelAdoptLayout(state.panelWidget, state.layout);

    XtRealizeWidget(state.shell);
    publishDesktopWindow(state.panelWidget);
    placeBehindApplications(state.shell);

    fprintf(stderr,
            "iid-desktop: model-backed rootShell online behind applications; desktop container model active\n");
    XtAppMainLoop(app);
    return 0;
}
