/*
 * FmMain.c++ - runnable file manager reconstruction
 *
 * Keep the application useful while the recovered FmApp/FmWindow and IMD
 * bodies are brought online. Widget names and control identities are taken
 * from the preserved fm executable and recovered FileManager app-defaults.
 */

#include "ModelBasedIconPanel.h"
#include "ImdIconPanel.h"
#include "IconLayout.h"
#include "FmIconSurface.h"

#include <Xm/Xm.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/MainW.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/ScrolledW.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <X11/Shell.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

struct FmEntry {
    std::string name;
    bool directory;
};

struct FmState {
    Widget shell;
    Widget location;
    FmIconSurface *surface;
    Widget status;
    Widget panelWidget;
    Widget scaleZoom;
    ImdModelBasedIconPanel *dirPanel;
    ImdIconLayout *iconLayout;
    bool showHidden;
    std::string directory;
    std::vector<FmEntry> entries;
};

/*
 * Fallbacks only. A real FileManager app-default database remains
 * authoritative when installed.
 */
static String fallbackResources[] = {
    (char *)"FileManager.width: 575",
    (char *)"FileManager.height: 600",
    (char *)"*ApplicationShell.minWidth: 300",
    (char *)"*ApplicationShell.minHeight: 200",

    (char *)"*actionsMenu.labelString: Actions",
    (char *)"*fileMenu.labelString: Selected",
    (char *)"*arrangeMenu.labelString: Sort",
    (char *)"*viewMenu.labelString: View",
    (char *)"*optionsMenu.labelString: Options",
    (char *)"*internetMenu.labelString: Internet",

    (char *)"*newDirMenuItem.labelString: Add New Directory",
    (char *)"*showFiltersMenuItem.labelString: Search / Filter Files",
    (char *)"*selectAllMenuItem.labelString: Select All",
    (char *)"*fetchMenuItem.labelString: File QuickFind...",
    (char *)"*quitMenuItem.labelString: Close",
    (char *)"*openMenuItem.labelString: Open",
    (char *)"*copyMenuItem.labelString: Make Copy",
    (char *)"*referenceMenuItem.labelString: Make Reference",
    (char *)"*removeMenuItem.labelString: Remove",
    (char *)"*printMenuItem.labelString: Print",
    (char *)"*getInfoMenuItem.labelString: Get Info",
    (char *)"*arrangeMenu.sortByNameMenuItem.labelString: by Name",
    (char *)"*arrangeMenu.sortByDateMenuItem.labelString: by Date",
    (char *)"*arrangeMenu.sortBySizeMenuItem.labelString: by Size",
    (char *)"*arrangeMenu.sortByTypeMenuItem.labelString: by Type",
    (char *)"*viewMenu.viewAsIconsMenuItem.labelString: as Icons",
    (char *)"*viewMenu.viewAsListMenuItem.labelString: as List",
    (char *)"*viewMenu.viewAsColumnsMenuItem.labelString: as Columns",
    (char *)"*viewMenu.viewAsGalleryMenuItem.labelString: as Thumbnail Images",
    (char *)"*viewMenu.showHiddenMenuItem.labelString: Hidden Files",
    (char *)"*viewMenu.truncateLabelsMenuItem.labelString: Truncate Names",
    (char *)"*applyDefaultLayoutMenuItem.labelString: Apply Default Layout",
    (char *)"*defaultLayoutMenuItem.labelString: Default Layout...",
    (char *)"*ignoreLayoutMenuItem.labelString: Preserve Layout",
    (char *)"*showShelfMenuItem.labelString: Show Shelf",
    (char *)"*showContentMenuItem.labelString: Show Content Viewer",
    (char *)"*httpMenuItem.labelString: Icon View of a Web Site",
    (char *)"*ftpMenuItem.labelString: Icon View of an FTP Site",

    /*
     * Original control names. The real app-defaults replaces these labels
     * with the recovered pixmaps when those assets are installed.
     */
    (char *)"*pathfinder.labelString: Path",
    (char *)"*iconicLayout.labelString: Icons",
    (char *)"*listLayout.labelString: List",
    (char *)"*treeLayout.labelString: Tree",
    (char *)"*columnLayout.labelString: Columns",
    (char *)"*galleryMode.labelString: Gallery",
    (char *)"*shelf.labelString: Shelf",

    (char *)"*rc1.orientation: HORIZONTAL",
    (char *)"*rc2.orientation: VERTICAL",
    (char *)"*rc2.packing: PACK_TIGHT",
    (char *)"*scaleZoom.orientation: VERTICAL",
    (char *)"*scaleZoom.processingDirection: MAX_ON_TOP",
    (char *)"*scaleZoom.maximum: 500",
    (char *)"*scaleZoom.minimum: 60",
    (char *)"*scaleZoom.value: 100",
    (char *)"*scaleZoom.showValue: false",
    (char *)"*scaleZoom.traversalOn: false",
    NULL
};

static std::string
joinPath(const std::string &left, const std::string &right)
{
    if (left == "/")
        return left + right;
    return left + "/" + right;
}

static bool
entryLess(const FmEntry &left, const FmEntry &right)
{
    if (left.directory != right.directory)
        return left.directory;
    return left.name < right.name;
}

static std::string
canonicalDirectory(const char *path)
{
    char resolved[PATH_MAX];
    struct stat st;

    if (!path || !*path)
        return std::string();
    if (!realpath(path, resolved))
        return std::string();
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode))
        return std::string();
    return resolved;
}

static void
setStatus(FmState *state, const char *text)
{
    XmString label = XmStringCreateLocalized((char *)(text ? text : ""));
    XtVaSetValues(state->status, XmNlabelString, label, NULL);
    XmStringFree(label);
}

static void
setStatusErrno(FmState *state, const char *operation)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s: %s", operation, strerror(errno));
    setStatus(state, buffer);
}

static void
populateDirectory(FmState *state, const std::string &directory)
{
    DIR *dir;
    struct dirent *de;
    std::vector<FmEntry> next;

    dir = opendir(directory.c_str());
    if (!dir) {
        setStatusErrno(state, "Cannot open directory");
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        FmEntry entry;
        struct stat st;
        std::string path;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (!state->showHidden && de->d_name[0] == '.')
            continue;

        entry.name = de->d_name;
        path = joinPath(directory, entry.name);
        entry.directory = (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        next.push_back(entry);
    }
    closedir(dir);

    std::sort(next.begin(), next.end(), entryLess);

    std::vector<FmIconSurfaceEntry> surfaceEntries;
    surfaceEntries.reserve(next.size());
    for (std::vector<FmEntry>::const_iterator it = next.begin();
         it != next.end(); ++it) {
        FmIconSurfaceEntry surfaceEntry;
        surfaceEntry.name = it->name;
        surfaceEntry.directory = it->directory;
        surfaceEntries.push_back(surfaceEntry);
    }
    state->surface->setEntries(surfaceEntries);

    state->entries.swap(next);
    state->directory = directory;
    XmTextFieldSetString(state->location, (char *)state->directory.c_str());

    {
        char title[PATH_MAX + 64];
        char status[128];
        snprintf(title, sizeof(title), "File Manager - %s", state->directory.c_str());
        XtVaSetValues(state->shell, XmNtitle, title, NULL);
        snprintf(status, sizeof(status), "%lu items",
                 (unsigned long)state->entries.size());
        setStatus(state, status);
    }
}

static void
changeDirectory(FmState *state, const char *path)
{
    std::string directory = canonicalDirectory(path);

    if (directory.empty()) {
        setStatus(state, "Not a directory");
        return;
    }
    populateDirectory(state, directory);
}

static void
openFile(FmState *state, const std::string &path)
{
    pid_t pid = fork();

    if (pid < 0) {
        setStatusErrno(state, "Cannot launch file");
        return;
    }

    if (pid == 0) {
        execlp("xdg-open", "xdg-open", path.c_str(), (char *)0);
        execlp("gio", "gio", "open", path.c_str(), (char *)0);
        _exit(127);
    }

    setStatus(state, "Opening file");
}

static bool
selectedEntry(FmState *state, FmEntry *entry)
{
    size_t index = 0;

    if (!state->surface->selectedIndex(&index) || index >= state->entries.size())
        return false;

    *entry = state->entries[index];
    return true;
}

static void
openEntry(FmState *state, const FmEntry &entry)
{
    std::string path = joinPath(state->directory, entry.name);
    if (entry.directory)
        changeDirectory(state, path.c_str());
    else
        openFile(state, path);
}

static void
locationActivateCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    char *text = XmTextFieldGetString(widget);

    (void)call;
    if (text) {
        changeDirectory(state, text);
        XtFree(text);
    }
}

static void
upCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    std::string parent;
    std::string::size_type slash;

    (void)widget;
    (void)call;

    if (state->directory == "/")
        return;

    slash = state->directory.rfind('/');
    parent = (slash == 0) ? "/" : state->directory.substr(0, slash);
    changeDirectory(state, parent.c_str());
}

static void
refreshCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    (void)widget;
    (void)call;
    populateDirectory(state, state->directory);
}

static void
surfaceActivateCB(size_t index, void *closure)
{
    FmState *state = (FmState *)closure;

    if (index >= state->entries.size())
        return;
    openEntry(state, state->entries[index]);
}

static void
openSelectedCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    FmEntry entry;

    (void)widget;
    (void)call;
    if (!selectedEntry(state, &entry)) {
        setStatus(state, "No item selected");
        return;
    }
    openEntry(state, entry);
}

static void
selectAllCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    (void)widget;
    (void)call;

    state->surface->selectAll();
}

static void
showHiddenCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call;

    state->showHidden = cbs->set ? true : false;
    populateDirectory(state, state->directory);
    (void)widget;
}

static void
closeCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    (void)widget;
    (void)call;
    XtDestroyWidget(state->shell);
    exit(0);
}

static void
unimplementedCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    const char *name = XtName(widget);
    char text[256];

    (void)call;
    snprintf(text, sizeof(text), "%s is not reconstructed yet",
             name ? name : "Command");
    setStatus(state, text);
}

static void
layoutButtonCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    const char *name = XtName(widget);
    char text[128];

    (void)call;

    /*
     * RE: IconicLayout, ListLayout and ColumnLayout are binary-proven
     * ImdIControlBoard modes. The portable list remains the only populated
     * view population now runs through adopted ImdIconView objects.
     */
    if (name && (!strcmp(name, "listLayout") ||
                 !strcmp(name, "viewAsListMenuItem"))) {
        populateDirectory(state, state->directory);
        setStatus(state, "List layout");
        return;
    }

    snprintf(text, sizeof(text), "%s layout path is not reconstructed yet",
             name ? name : "Selected");
    setStatus(state, text);
}

static void
scaleZoomCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    XmScaleCallbackStruct *cbs = (XmScaleCallbackStruct *)call;
    float scale = (float)cbs->value / 100.0f;
    char text[64];

    ImdViewPanelSetScale(state->panelWidget, scale, 0, 0);
    snprintf(text, sizeof(text), "Icon scale %d%%", cbs->value);
    setStatus(state, text);
    (void)widget;
}

static Widget
makeMenu(Widget menuBar, const char *name)
{
    Widget menu = XmCreatePulldownMenu(menuBar, (char *)name, NULL, 0);
    XtVaCreateManagedWidget(name,
                            xmCascadeButtonWidgetClass,
                            menuBar,
                            XmNsubMenuId, menu,
                            NULL);
    return menu;
}

static Widget
makePush(Widget parent, const char *name,
         XtCallbackProc callback, FmState *state)
{
    Widget item = XtVaCreateManagedWidget(name, xmPushButtonWidgetClass,
                                          parent, NULL);
    if (callback)
        XtAddCallback(item, XmNactivateCallback, callback, state);
    return item;
}

static void
makeSeparator(Widget menu)
{
    XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, menu, NULL);
}

static Widget
createMenuBar(Widget parent, FmState *state)
{
    Widget menuBar;
    Widget menu;
    Widget hidden;

    menuBar = XmCreateMenuBar(parent, (char *)"menuBar", NULL, 0);

    menu = makeMenu(menuBar, "actionsMenu");
    makePush(menu, "newDirMenuItem", unimplementedCB, state);
    makePush(menu, "showFiltersMenuItem", unimplementedCB, state);
    makePush(menu, "selectAllMenuItem", selectAllCB, state);
    makeSeparator(menu);
    makePush(menu, "fetchMenuItem", unimplementedCB, state);
    makeSeparator(menu);
    makePush(menu, "quitMenuItem", closeCB, state);

    menu = makeMenu(menuBar, "fileMenu");
    makePush(menu, "openMenuItem", openSelectedCB, state);
    makePush(menu, "copyMenuItem", unimplementedCB, state);
    makePush(menu, "referenceMenuItem", unimplementedCB, state);
    makePush(menu, "removeMenuItem", unimplementedCB, state);
    makeSeparator(menu);
    makePush(menu, "printMenuItem", unimplementedCB, state);
    makePush(menu, "getInfoMenuItem", unimplementedCB, state);

    menu = makeMenu(menuBar, "arrangeMenu");
    makePush(menu, "sortByNameMenuItem", refreshCB, state);
    makePush(menu, "sortByDateMenuItem", unimplementedCB, state);
    makePush(menu, "sortBySizeMenuItem", unimplementedCB, state);
    makePush(menu, "sortByTypeMenuItem", unimplementedCB, state);

    menu = makeMenu(menuBar, "viewMenu");
    makePush(menu, "viewAsIconsMenuItem", layoutButtonCB, state);
    makePush(menu, "viewAsListMenuItem", layoutButtonCB, state);
    makePush(menu, "viewAsColumnsMenuItem", layoutButtonCB, state);
    makePush(menu, "viewAsGalleryMenuItem", layoutButtonCB, state);
    makeSeparator(menu);
    hidden = XtVaCreateManagedWidget("showHiddenMenuItem",
                                     xmToggleButtonWidgetClass,
                                     menu,
                                     XmNset, False,
                                     NULL);
    XtAddCallback(hidden, XmNvalueChangedCallback, showHiddenCB, state);
    makePush(menu, "truncateLabelsMenuItem", unimplementedCB, state);

    menu = makeMenu(menuBar, "optionsMenu");
    makePush(menu, "applyDefaultLayoutMenuItem", unimplementedCB, state);
    makePush(menu, "defaultLayoutMenuItem", unimplementedCB, state);
    makePush(menu, "ignoreLayoutMenuItem", unimplementedCB, state);
    makeSeparator(menu);
    makePush(menu, "showShelfMenuItem", unimplementedCB, state);
    makePush(menu, "showContentMenuItem", unimplementedCB, state);

    menu = makeMenu(menuBar, "internetMenu");
    makePush(menu, "httpMenuItem", unimplementedCB, state);
    makePush(menu, "ftpMenuItem", unimplementedCB, state);

    XtManageChild(menuBar);
    return menuBar;
}

static Widget
createControlBoard(Widget parent, FmState *state)
{
    Widget rc1;
    Widget rc2;

    /*
     * RE: original fm constructs ImdIControlBoard(Widget, ImdIconPanel &)
     * and populates it through addButton(). rc1/rc2 orientation and these
     * child names are recovered from the original FileManager resources.
     * This Motif shell is temporary until ImdIControlBoard itself is ported.
     */
    rc1 = XtVaCreateManagedWidget("rc1",
                                  xmRowColumnWidgetClass,
                                  parent,
                                  XmNorientation, XmHORIZONTAL,
                                  XmNpacking, XmPACK_TIGHT,
                                  XmNtopAttachment, XmATTACH_FORM,
                                  XmNleftAttachment, XmATTACH_FORM,
                                  XmNbottomAttachment, XmATTACH_FORM,
                                  NULL);

    rc2 = XtVaCreateManagedWidget("rc2",
                                  xmRowColumnWidgetClass,
                                  rc1,
                                  XmNorientation, XmVERTICAL,
                                  XmNpacking, XmPACK_TIGHT,
                                  NULL);

    makePush(rc2, "pathfinder", unimplementedCB, state);
    makePush(rc2, "iconicLayout", layoutButtonCB, state);
    makePush(rc2, "listLayout", layoutButtonCB, state);
    makePush(rc2, "treeLayout", layoutButtonCB, state);
    makePush(rc2, "columnLayout", layoutButtonCB, state);
    makePush(rc2, "galleryMode", layoutButtonCB, state);
    makePush(rc2, "shelf", unimplementedCB, state);

    state->scaleZoom = XtVaCreateManagedWidget("scaleZoom",
                                                xmScaleWidgetClass,
                                                rc1,
                                                XmNorientation, XmVERTICAL,
                                                XmNprocessingDirection, XmMAX_ON_TOP,
                                                XmNminimum, 60,
                                                XmNmaximum, 500,
                                                XmNvalue, 100,
                                                XmNshowValue, False,
                                                NULL);
    XtAddCallback(state->scaleZoom, XmNvalueChangedCallback,
                  scaleZoomCB, state);
    XtAddCallback(state->scaleZoom, XmNdragCallback,
                  scaleZoomCB, state);

    return rc1;
}

static void
reapChildren(XtPointer client, XtIntervalId *id)
{
    FmState *state = (FmState *)client;

    while (waitpid(-1, 0, WNOHANG) > 0)
        ;

    XtAppAddTimeOut(XtWidgetToApplicationContext(state->shell),
                    1000, reapChildren, client);
    (void)id;
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    FmState state;
    Widget mainWindow;
    Widget menuBar;
    Widget form;
    Widget finderForm;
    Widget controlBoard;
    Widget up;
    Widget refresh;
    Widget scroller;
    ImdInvertedTeeRenderer *renderer;
    const char *initial = (argc > 1) ? argv[1] : ".";

    state.shell = 0;
    state.location = 0;
    state.surface = 0;
    state.status = 0;
    state.panelWidget = 0;
    state.scaleZoom = 0;
    state.dirPanel = 0;
    state.iconLayout = 0;
    state.showHidden = false;

    state.shell = XtVaAppInitialize(&app, (char *)"FileManager",
                                    NULL, 0, &argc, argv,
                                    fallbackResources,
                                    NULL);

    mainWindow = XtVaCreateManagedWidget("mainWindow",
                                         xmMainWindowWidgetClass,
                                         state.shell, NULL);
    menuBar = createMenuBar(mainWindow, &state);

    form = XtVaCreateManagedWidget("form",
                                   xmFormWidgetClass,
                                   mainWindow, NULL);

    /*
     * finderForm/iconFinder are both present in the original binary/resource
     * hierarchy. The current text field still provides the bootstrap path
     * navigation function while ImdViewerFinder is reconstructed.
     */
    finderForm = XtVaCreateManagedWidget("finderForm",
                                         xmFormWidgetClass,
                                         form,
                                         XmNtopAttachment, XmATTACH_FORM,
                                         XmNleftAttachment, XmATTACH_FORM,
                                         XmNrightAttachment, XmATTACH_FORM,
                                         NULL);

    up = XtVaCreateManagedWidget("Up",
                                 xmPushButtonWidgetClass,
                                 finderForm,
                                 XmNtopAttachment, XmATTACH_FORM,
                                 XmNleftAttachment, XmATTACH_FORM,
                                 XmNbottomAttachment, XmATTACH_FORM,
                                 NULL);

    refresh = XtVaCreateManagedWidget("Refresh",
                                      xmPushButtonWidgetClass,
                                      finderForm,
                                      XmNtopAttachment, XmATTACH_FORM,
                                      XmNleftAttachment, XmATTACH_WIDGET,
                                      XmNleftWidget, up,
                                      XmNbottomAttachment, XmATTACH_FORM,
                                      NULL);

    state.location = XtVaCreateManagedWidget("iconFinder",
                                              xmTextFieldWidgetClass,
                                              finderForm,
                                              XmNtopAttachment, XmATTACH_FORM,
                                              XmNleftAttachment, XmATTACH_WIDGET,
                                              XmNleftWidget, refresh,
                                              XmNrightAttachment, XmATTACH_FORM,
                                              XmNbottomAttachment, XmATTACH_FORM,
                                              NULL);

    state.status = XtVaCreateManagedWidget("status",
                                           xmLabelWidgetClass,
                                           form,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_FORM,
                                           XmNbottomAttachment, XmATTACH_FORM,
                                           XmNalignment, XmALIGNMENT_BEGINNING,
                                           NULL);

    controlBoard = createControlBoard(form, &state);
    XtVaSetValues(controlBoard,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, finderForm,
                  XmNbottomAttachment, XmATTACH_WIDGET,
                  XmNbottomWidget, state.status,
                  NULL);

    scroller = XtVaCreateManagedWidget("scrollW",
                                       xmScrolledWindowWidgetClass,
                                       form,
                                       XmNscrollingPolicy, XmAUTOMATIC,
                                       XmNtopAttachment, XmATTACH_WIDGET,
                                       XmNtopWidget, finderForm,
                                       XmNleftAttachment, XmATTACH_WIDGET,
                                       XmNleftWidget, controlBoard,
                                       XmNrightAttachment, XmATTACH_FORM,
                                       XmNbottomAttachment, XmATTACH_WIDGET,
                                       XmNbottomWidget, state.status,
                                       NULL);

    /*
     * RE: original fm constructs ImdModelBasedIconPanel in _createPanels(),
     * stores it at FmWindow +0xa4 and registers it through setDirPanel().
     * The broker/model path is not online yet. Directory entries are now
     * adopted as ImdIconView objects directly by the reconstructed panel.
     */
    state.dirPanel = new ImdModelBasedIconPanel("panel", scroller, 0, NULL, 0);
    state.panelWidget = state.dirPanel->baseWidget();
    XtManageChild(state.panelWidget);

    renderer = new ImdInvertedTeeRenderer;
    ImdViewPanelAdoptRenderer(state.panelWidget, renderer);

    /*
     * RE: original FmWindow::_createPanels constructs ImdIconLayout with
     * the live panel widget and adopts it before normal directory viewing.
     */
    state.iconLayout = new ImdIconLayout(state.panelWidget);
    ImdViewPanelAdoptLayout(state.panelWidget, state.iconLayout);

    /*
     * RE: libimdIconPanel proves ImdIconPanel::adoptView(ImdIconView *, ...)
     * and ImdViewPanelDeleteAllViews(). FmIconSurface uses that recovered
     * ownership direction while renderer/view drawing is brought online.
     * There is intentionally no Motif child widget inside the IMD panel.
     */
    state.surface = new FmIconSurface(state.dirPanel, state.panelWidget);
    state.surface->setActivateProc(surfaceActivateCB, &state);

    XtAddCallback(up, XmNactivateCallback, upCB, &state);
    XtAddCallback(refresh, XmNactivateCallback, refreshCB, &state);
    XtAddCallback(state.location, XmNactivateCallback,
                  locationActivateCB, &state);

    XmMainWindowSetAreas(mainWindow, menuBar, NULL, NULL, NULL, form);

    changeDirectory(&state, initial);
    XtRealizeWidget(state.shell);
    XtAppAddTimeOut(app, 1000, reapChildren, &state);
    XtAppMainLoop(app);
    return 0;
}
