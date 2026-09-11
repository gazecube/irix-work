/*
 * FmMain.c++ - runnable file manager reconstruction
 *
 * Widget names and control identities are taken from the preserved fm
 * executable and recovered FileManager app-defaults. The directory content
 * path now shares the same model-backed ImdModelBasedIconPanel ->
 * ImdModelBasedIconView -> ImdInvertedTeeRenderer pipeline as iid-desktop.
 */

#include "ModelBasedIconPanel.h"
#include "ModelBasedIconView.h"
#include "ImdIconPanel.h"
#include "IconLayout.h"
#include "Broker.h"
#include "FileModel.h"
#include "Model.h"
#include "IconFinder.h"
#include <Sgm/ThumbWheel.h>

#include <Xm/Xm.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/MainW.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
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
    Widget status;
    Widget panelWidget;
    Widget scaleZoom;
    ImdIconFinder *finder;
    ImdBroker *broker;
    ImdModel *directoryModel;
    ImdModelBasedIconPanel *dirPanel;
    ImdIconLayout *iconLayout;
    bool showHidden;
    std::string directory;
    std::vector<FmEntry> entries;
    ImdDNA selectedDNA;
    bool haveSelectedDNA;
    Time lastClickTime;
    ImdView *lastClickView;
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

static std::string joinPath(const std::string &left, const std::string &right)
{
    if (left == "/") return left + right;
    return left + "/" + right;
}

static bool entryLess(const FmEntry &left, const FmEntry &right)
{
    if (left.directory != right.directory) return left.directory;
    return left.name < right.name;
}

static std::string canonicalDirectory(const char *path)
{
    char resolved[PATH_MAX];
    struct stat st;
    if (!path || !*path) return std::string();
    if (!realpath(path, resolved)) return std::string();
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) return std::string();
    return resolved;
}

static void setStatus(FmState *state, const char *text)
{
    XmString label = XmStringCreateLocalized((char *)(text ? text : ""));
    XtVaSetValues(state->status, XmNlabelString, label, NULL);
    XmStringFree(label);
}

static void setStatusErrno(FmState *state, const char *operation)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s: %s", operation, strerror(errno));
    setStatus(state, buffer);
}

static ImdModel *modelForDirectory(FmState *state, const std::string &directory)
{
    ImdString selector = ImdFileModel::dnaSelectorFromPath(ImdString(directory), ImdString());
    ImdDNA dna(ImdString("ImdDirModel"), selector);
    return state->broker ? state->broker->getModel(dna) : 0;
}

static void populateDirectory(FmState *state, const std::string &directory)
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
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (!state->showHidden && de->d_name[0] == '.') continue;
        entry.name = de->d_name;
        path = joinPath(directory, entry.name);
        entry.directory = (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        next.push_back(entry);
    }
    closedir(dir);
    std::sort(next.begin(), next.end(), entryLess);

    ImdModel *nextModel = modelForDirectory(state, directory);
    if (!nextModel) {
        setStatus(state, "Cannot create directory model");
        return;
    }

    state->dirPanel->changeModel(nextModel);
    if (state->directoryModel) state->broker->releaseModel(state->directoryModel);
    state->directoryModel = nextModel;

    state->entries.swap(next);
    state->directory = directory;
    state->haveSelectedDNA = false;
    state->lastClickTime = 0;
    state->lastClickView = 0;
    if (state->finder)
        state->finder->setContent(state->directory.c_str(), false);
    else if (state->location)
        XmTextFieldSetString(state->location, (char *)state->directory.c_str());

    {
        char title[PATH_MAX + 64];
        char status[128];
        snprintf(title, sizeof(title), "File Manager - %s", state->directory.c_str());
        XtVaSetValues(state->shell, XmNtitle, title, NULL);
        snprintf(status, sizeof(status), "%lu items", (unsigned long)state->entries.size());
        setStatus(state, status);
    }
}

static void changeDirectory(FmState *state, const char *path)
{
    std::string directory = canonicalDirectory(path);
    if (directory.empty()) {
        setStatus(state, "Not a directory");
        return;
    }
    populateDirectory(state, directory);
}

static void openFile(FmState *state, const std::string &path)
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

static bool selectedEntry(FmState *state, FmEntry *entry)
{
    if (!state->haveSelectedDNA) return false;
    const std::string path = ImdFileModel::fileNameFromDNA(state->selectedDNA).stdString();
    if (path.empty()) return false;
    const std::string::size_type slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (std::vector<FmEntry>::const_iterator it = state->entries.begin(); it != state->entries.end(); ++it) {
        if (it->name == name) {
            *entry = *it;
            return true;
        }
    }
    return false;
}

static void openEntry(FmState *state, const FmEntry &entry)
{
    std::string path = joinPath(state->directory, entry.name);
    if (entry.directory) changeDirectory(state, path.c_str());
    else openFile(state, path);
}

static void panelEventHandler(Widget widget, XtPointer client, XEvent *event, Boolean *)
{
    FmState *state = (FmState *)client;
    if (!event || event->type != ButtonPress || event->xbutton.button != Button1) return;

    ImdView *hit = ImdViewPanelFindViewByLocation(widget, event->xbutton.x, event->xbutton.y);
    ImdModelBasedIconView *modelView = dynamic_cast<ImdModelBasedIconView *>(hit);
    if (!modelView) return;

    const bool doubleClick = state->lastClickView == hit &&
        event->xbutton.time >= state->lastClickTime &&
        event->xbutton.time - state->lastClickTime <= 400;

    if (event->xbutton.state & ShiftMask)
        state->dirPanel->extendSelect(modelView);
    else
        state->dirPanel->select(modelView);

    state->selectedDNA = modelView->getDNA();
    state->haveSelectedDNA = true;
    state->lastClickTime = event->xbutton.time;
    state->lastClickView = hit;

    if (doubleClick) {
        const std::string path = ImdFileModel::fileNameFromDNA(state->selectedDNA).stdString();
        if (!path.empty()) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) changeDirectory(state, path.c_str());
            else openFile(state, path);
        }
        state->lastClickTime = 0;
        state->lastClickView = 0;
    }
}

static void locationActivateCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    (void)widget;
    (void)call;
    if (state->finder) {
        const std::string text = state->finder->getContent();
        changeDirectory(state, text.c_str());
        return;
    }
    if (state->location) {
        char *text = XmTextFieldGetString(state->location);
        if (text) {
            changeDirectory(state, text);
            XtFree(text);
        }
    }
}

static void refreshCB(Widget, XtPointer client, XtPointer)
{
    FmState *state = (FmState *)client;
    populateDirectory(state, state->directory);
}

static void openSelectedCB(Widget, XtPointer client, XtPointer)
{
    FmState *state = (FmState *)client;
    FmEntry entry;
    if (!selectedEntry(state, &entry)) {
        setStatus(state, "No item selected");
        return;
    }
    openEntry(state, entry);
}

/*
 * Original fm exports selectAllHelper(ImdView *, void *). The N32 body:
 *   - converts the generic ImdView to an icon view,
 *   - rejects views whose visible/state bit is clear,
 *   - rejects views already carrying icon-state flag 8, and
 *   - asks the selection holder to extend the selection.
 *
 * The portable dynamic_cast/portableVisible() calls are host ABI seams; the
 * ordering and selection test come from the original fm disassembly.
 */
static void selectAllHelper(ImdView *rawView, void *client)
{
    ImdModelBasedIconPanel *selectionHolder = (ImdModelBasedIconPanel *)client;
    ImdIconView *view = dynamic_cast<ImdIconView *>(rawView);
    if (!selectionHolder || !view) return;
    if (!view->portableVisible()) return;
    if (view->getFlag(8u)) return;
    selectionHolder->extendSelect(view);
}

static void selectAllCB(Widget, XtPointer client, XtPointer)
{
    FmState *state = (FmState *)client;
    ImdModelBasedIconPanel *selectionHolder = state->dirPanel;
    if (!selectionHolder) return;

    const std::vector<ImdView *> &views = selectionHolder->portableViews();
    selectionHolder->beginSelectBlock();
    for (std::vector<ImdView *>::const_iterator it = views.begin(); it != views.end(); ++it)
        selectAllHelper(*it, selectionHolder);
    selectionHolder->endSelectBlock();
}

static void showHiddenCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call;
    state->showHidden = cbs->set ? true : false;
    populateDirectory(state, state->directory);
    (void)widget;
}

static void closeCB(Widget, XtPointer client, XtPointer)
{
    FmState *state = (FmState *)client;
    if (state->directoryModel && state->broker) {
        state->broker->releaseModel(state->directoryModel);
        state->directoryModel = 0;
    }
    XtDestroyWidget(state->shell);
    exit(0);
}

static void unimplementedCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    const char *name = XtName(widget);
    char text[256];
    (void)call;
    snprintf(text, sizeof(text), "%s is not reconstructed yet", name ? name : "Command");
    setStatus(state, text);
}

static void layoutButtonCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    const char *name = XtName(widget);
    char text[128];
    (void)call;
    if (name && (!strcmp(name, "iconicLayout") || !strcmp(name, "viewAsIconsMenuItem"))) {
        populateDirectory(state, state->directory);
        setStatus(state, "Icon layout");
        return;
    }
    snprintf(text, sizeof(text), "%s layout path is not reconstructed yet", name ? name : "Selected");
    setStatus(state, text);
}

static void scaleZoomCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    SgThumbWheelCallbackStruct *cbs = (SgThumbWheelCallbackStruct *)call;
    int value = cbs ? cbs->value : 100;
    float scale = (float)value / 100.0f;
    char text[64];
    ImdViewPanelSetScale(state->panelWidget, scale, 0, 0);
    snprintf(text, sizeof(text), "Icon scale %d%%", value);
    setStatus(state, text);
    (void)widget;
}

static Widget makeMenu(Widget menuBar, const char *name)
{
    Widget menu = XmCreatePulldownMenu(menuBar, (char *)name, NULL, 0);
    XtVaCreateManagedWidget(name, xmCascadeButtonWidgetClass, menuBar, XmNsubMenuId, menu, NULL);
    return menu;
}

static Widget makePush(Widget parent, const char *name, XtCallbackProc callback, FmState *state)
{
    Widget item = XtVaCreateManagedWidget(name, xmPushButtonWidgetClass, parent, NULL);
    if (callback) XtAddCallback(item, XmNactivateCallback, callback, state);
    return item;
}

static void makeSeparator(Widget menu)
{
    XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, menu, NULL);
}

static Widget createMenuBar(Widget parent, FmState *state)
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
    hidden = XtVaCreateManagedWidget("showHiddenMenuItem", xmToggleButtonWidgetClass, menu, XmNset, False, NULL);
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

static Widget createControlBoard(Widget parent, FmState *state)
{
    Widget rc1;
    Widget rc2;
    rc1 = XtVaCreateManagedWidget("rc1", xmRowColumnWidgetClass, parent,
                                  XmNorientation, XmHORIZONTAL,
                                  XmNpacking, XmPACK_TIGHT,
                                  XmNtopAttachment, XmATTACH_FORM,
                                  XmNleftAttachment, XmATTACH_FORM,
                                  XmNbottomAttachment, XmATTACH_FORM,
                                  NULL);
    rc2 = XtVaCreateManagedWidget("rc2", xmRowColumnWidgetClass, rc1,
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

    /* FM composition: the original control strip places scaleZoom directly
     * beneath the mode buttons. The ThumbWheel widget implementation itself
     * remains the binary-backed reconstruction in libSgm. */
    state->scaleZoom = XtVaCreateManagedWidget("scaleZoom", sgThumbWheelWidgetClass, rc2,
                                                XmNorientation, XmVERTICAL,
                                                XmNminimum, 60,
                                                XmNmaximum, 500,
                                                XmNvalue, 100,
                                                SgNhomePosition, 100,
                                                XmNtraversalOn, False,
                                                NULL);
    XtAddCallback(state->scaleZoom, XmNvalueChangedCallback, scaleZoomCB, state);
    XtAddCallback(state->scaleZoom, XmNdragCallback, scaleZoomCB, state);
    return rc1;
}

static void reapChildren(XtPointer client, XtIntervalId *id)
{
    FmState *state = (FmState *)client;
    while (waitpid(-1, 0, WNOHANG) > 0) ;
    XtAppAddTimeOut(XtWidgetToApplicationContext(state->shell), 1000, reapChildren, client);
    (void)id;
}

int main(int argc, char **argv)
{
    XtAppContext app;
    FmState state;
    Widget mainWindow;
    Widget menuBar;
    Widget form;
    Widget finderForm;
    Widget finderWidget;
    Widget controlBoard;
    Widget scroller;
    ImdInvertedTeeRenderer *renderer;
    const char *initial = (argc > 1) ? argv[1] : ".";

    state.shell = 0;
    state.location = 0;
    state.status = 0;
    state.panelWidget = 0;
    state.scaleZoom = 0;
    state.finder = 0;
    state.broker = ImdBroker::contextBroker();
    state.directoryModel = 0;
    state.dirPanel = 0;
    state.iconLayout = 0;
    state.showHidden = false;
    state.haveSelectedDNA = false;
    state.lastClickTime = 0;
    state.lastClickView = 0;

    state.shell = XtVaAppInitialize(&app, (char *)"FileManager",
                                    NULL, 0, &argc, argv,
                                    fallbackResources, NULL);

    mainWindow = XtVaCreateManagedWidget("mainWindow", xmMainWindowWidgetClass, state.shell, NULL);
    menuBar = createMenuBar(mainWindow, &state);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, mainWindow, NULL);

    finderForm = XtVaCreateManagedWidget("finderForm", xmFormWidgetClass, form,
                                         XmNtopAttachment, XmATTACH_FORM,
                                         XmNleftAttachment, XmATTACH_FORM,
                                         XmNrightAttachment, XmATTACH_FORM,
                                         NULL);

    /*
     * Original FileManager resources define finderForm.iconFinder, not the
     * temporary Up/Refresh pair that the portable bootstrap used. Wire the
     * binary-backed ImdIconFinder reconstruction into that original slot.
     */
    state.finder = new ImdIconFinder("iconFinder", finderForm, 0);
    finderWidget = state.finder->getWidget();
    state.location = state.finder->portableTextWidget();
    if (!finderWidget || !state.location) {
        fprintf(stderr, "fm: failed to construct ImdIconFinder\n");
        return 1;
    }
    XtVaSetValues(finderWidget,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  NULL);
    XtManageChild(finderWidget);

    state.status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, form,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_FORM,
                                           XmNbottomAttachment, XmATTACH_FORM,
                                           XmNalignment, XmALIGNMENT_BEGINNING,
                                           NULL);

    controlBoard = createControlBoard(form, &state);
    XtVaSetValues(controlBoard,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, finderForm,
                  NULL);

    scroller = XtVaCreateManagedWidget("scrollW", xmScrolledWindowWidgetClass, form,
                                       XmNscrollingPolicy, XmAUTOMATIC,
                                       XmNtopAttachment, XmATTACH_WIDGET,
                                       XmNtopWidget, finderForm,
                                       XmNleftAttachment, XmATTACH_WIDGET,
                                       XmNleftWidget, controlBoard,
                                       XmNrightAttachment, XmATTACH_FORM,
                                       XmNbottomAttachment, XmATTACH_WIDGET,
                                       XmNbottomWidget, state.status,
                                       NULL);

    state.dirPanel = new ImdModelBasedIconPanel("panel", scroller, 0, NULL, 0);
    state.dirPanel->enableMultipleSelect(true);
    state.panelWidget = state.dirPanel->baseWidget();

    /*
     * The original FileManager app-default hierarchy is
     *   FileManager*scrollW.panel
     * and _createPanels() creates the live ImdModelBasedIconPanel as the
     * scrolled directory-view child. On Motif, merely managing a generic
     * DrawingArea-family child does not reliably make it the XmScrolledWindow
     * work window. Bind that relationship explicitly so the IMD panel owns the
     * visible viewport instead of remaining at its tiny requested geometry.
     *
     * The 575x400 request is only the portable pre-realize geometry seam; the
     * shipped FileManager resources already establish width=575 and content
     * pane height=400. XmScrolledWindow remains responsible for viewport and
     * scrollbar geometry after realization.
     */
    XtVaSetValues(state.panelWidget,
                  XmNwidth, 575,
                  XmNheight, 400,
                  NULL);
    XtManageChild(state.panelWidget);
    XtVaSetValues(scroller,
                  XmNworkWindow, state.panelWidget,
                  NULL);

    renderer = new ImdInvertedTeeRenderer;
    ImdViewPanelAdoptRenderer(state.panelWidget, renderer);

    state.iconLayout = new ImdIconLayout(state.panelWidget);
    ImdViewPanelAdoptLayout(state.panelWidget, state.iconLayout);

    XtAddEventHandler(state.panelWidget,
                      ButtonPressMask,
                      False,
                      panelEventHandler,
                      &state);

    XtAddCallback(state.location, XmNactivateCallback, locationActivateCB, &state);

    XmMainWindowSetAreas(mainWindow, menuBar, NULL, NULL, NULL, form);

    changeDirectory(&state, initial);
    XtRealizeWidget(state.shell);

    {
        Dimension pw = 0, ph = 0, sw = 0, sh = 0;
        XtVaGetValues(state.panelWidget, XmNwidth, &pw, XmNheight, &ph, NULL);
        XtVaGetValues(scroller, XmNwidth, &sw, XmNheight, &sh, NULL);
        fprintf(stderr,
                "fm: icon panel geometry %ux%u in scrollW %ux%u; %lu model-backed views\n",
                (unsigned)pw, (unsigned)ph,
                (unsigned)sw, (unsigned)sh,
                (unsigned long)state.dirPanel->portableViews().size());
        ImdViewPanelRedraw(state.panelWidget);
    }

    XtAppAddTimeOut(app, 1000, reapChildren, &state);
    XtAppMainLoop(app);
    return 0;
}
