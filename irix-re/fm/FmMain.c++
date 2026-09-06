/*
 * FmMain.c++ - first runnable file manager reconstruction
 *
 * This is the application-side bootstrap for the Linux reconstruction.  The
 * window/panel classes will move toward the recovered FmApp/FmWindow split as
 * those bodies are reconstructed.  Keep this path useful in the meantime: it
 * should be possible to use fm as an ordinary directory browser while the IMD
 * panel and FTR icon machinery is brought online.
 */

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>
#include <Xm/TextF.h>

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
    Widget list;
    Widget status;
    std::string directory;
    std::vector<FmEntry> entries;
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

        entry.name = de->d_name;
        path = joinPath(directory, entry.name);
        entry.directory = (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        next.push_back(entry);
    }
    closedir(dir);

    std::sort(next.begin(), next.end(), entryLess);

    XmListDeleteAllItems(state->list);
    for (std::vector<FmEntry>::const_iterator it = next.begin();
         it != next.end(); ++it) {
        std::string visible = it->name;
        if (it->directory)
            visible += "/";
        XmString item = XmStringCreateLocalized((char *)visible.c_str());
        XmListAddItemUnselected(state->list, item, 0);
        XmStringFree(item);
    }

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
    if (slash == 0)
        parent = "/";
    else
        parent = state->directory.substr(0, slash);
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
listDefaultActionCB(Widget widget, XtPointer client, XtPointer call)
{
    FmState *state = (FmState *)client;
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)call;
    int index;

    (void)widget;

    index = cbs->item_position - 1;
    if (index < 0 || (size_t)index >= state->entries.size())
        return;

    {
        const FmEntry &entry = state->entries[(size_t)index];
        std::string path = joinPath(state->directory, entry.name);
        if (entry.directory)
            changeDirectory(state, path.c_str());
        else
            openFile(state, path);
    }
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
    Widget form;
    Widget controls;
    Widget up;
    Widget refresh;
    Widget scroller;
    const char *initial = (argc > 1) ? argv[1] : ".";

    memset(&state, 0, sizeof(state));

    state.shell = XtVaAppInitialize(&app, (char *)"Fm",
                                    NULL, 0, &argc, argv,
                                    NULL,
                                    XmNwidth, 620,
                                    XmNheight, 480,
                                    NULL);

    mainWindow = XtVaCreateManagedWidget("mainWindow",
                                         xmMainWindowWidgetClass,
                                         state.shell, NULL);
    form = XtVaCreateManagedWidget("fileManagerForm",
                                    xmFormWidgetClass,
                                    mainWindow, NULL);

    controls = XtVaCreateManagedWidget("navigation",
                                        xmRowColumnWidgetClass,
                                        form,
                                        XmNorientation, XmHORIZONTAL,
                                        XmNpacking, XmPACK_TIGHT,
                                        XmNtopAttachment, XmATTACH_FORM,
                                        XmNleftAttachment, XmATTACH_FORM,
                                        XmNrightAttachment, XmATTACH_FORM,
                                        NULL);

    up = XtVaCreateManagedWidget("Up",
                                  xmPushButtonWidgetClass,
                                  controls, NULL);
    refresh = XtVaCreateManagedWidget("Refresh",
                                       xmPushButtonWidgetClass,
                                       controls, NULL);
    state.location = XtVaCreateManagedWidget("location",
                                              xmTextFieldWidgetClass,
                                              controls,
                                              XmNcolumns, 48,
                                              NULL);

    state.status = XtVaCreateManagedWidget("status",
                                            xmLabelWidgetClass,
                                            form,
                                            XmNleftAttachment, XmATTACH_FORM,
                                            XmNrightAttachment, XmATTACH_FORM,
                                            XmNbottomAttachment, XmATTACH_FORM,
                                            XmNalignment, XmALIGNMENT_BEGINNING,
                                            NULL);

    scroller = XtVaCreateManagedWidget("scroller",
                                        xmScrolledWindowWidgetClass,
                                        form,
                                        XmNscrollingPolicy, XmAUTOMATIC,
                                        XmNtopAttachment, XmATTACH_WIDGET,
                                        XmNtopWidget, controls,
                                        XmNleftAttachment, XmATTACH_FORM,
                                        XmNrightAttachment, XmATTACH_FORM,
                                        XmNbottomAttachment, XmATTACH_WIDGET,
                                        XmNbottomWidget, state.status,
                                        NULL);

    state.list = XtVaCreateManagedWidget("directoryList",
                                          xmListWidgetClass,
                                          scroller,
                                          XmNselectionPolicy, XmEXTENDED_SELECT,
                                          XmNvisibleItemCount, 18,
                                          NULL);

    XtAddCallback(up, XmNactivateCallback, upCB, &state);
    XtAddCallback(refresh, XmNactivateCallback, refreshCB, &state);
    XtAddCallback(state.location, XmNactivateCallback,
                  locationActivateCB, &state);
    XtAddCallback(state.list, XmNdefaultActionCallback,
                  listDefaultActionCB, &state);

    XmMainWindowSetAreas(mainWindow, NULL, NULL, NULL, NULL, form);

    changeDirectory(&state, initial);
    XtRealizeWidget(state.shell);
    XtAppAddTimeOut(app, 1000, reapChildren, &state);
    XtAppMainLoop(app);
    return 0;
}
