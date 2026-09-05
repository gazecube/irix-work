#include <Sgm/Finder.h>

#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int activateCount;
static int valueChangedCount;

static void activateCB(Widget w, XtPointer clientData, XtPointer callData)
{
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)callData;
    (void)w;
    (void)clientData;

    if (!cb || cb->reason != XmCR_ACTIVATE)
    {
        fprintf(stderr, "SgFinder smoke: bad activate callback reason\n");
        exit(20);
    }
    ++activateCount;
}

static void valueChangedCB(Widget w, XtPointer clientData, XtPointer callData)
{
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)callData;
    (void)w;
    (void)clientData;

    if (!cb || cb->reason != XmCR_VALUE_CHANGED)
    {
        fprintf(stderr, "SgFinder smoke: bad valueChanged callback reason\n");
        exit(21);
    }
    ++valueChangedCount;
}

static void require(Boolean condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "SgFinder smoke: FAIL: %s\n", message);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell;
    Widget finder;
    Widget text;
    Widget history;
    Widget historyItem;
    Widget optional;
    Arg args[2];
    Cardinal count;
    char *value;
    int activateBeforeHistory;

    shell = XtVaAppInitialize(&app, "SgFinderSmoke",
                              NULL, 0, &argc, argv,
                              NULL, NULL);
    require(shell != NULL, "XtVaAppInitialize returned NULL");

    fprintf(stderr, "SgFinder smoke: direct sgFinderWidgetClass create\n");
    finder = XtVaCreateManagedWidget("finder",
                                     sgFinderWidgetClass,
                                     shell,
                                     XtNwidth, 360,
                                     XtNheight, 52,
                                     NULL);
    require(finder != NULL, "direct Finder creation failed");
    require(XtIsSubclass(finder, sgFinderWidgetClass),
            "direct widget is not an SgFinder subclass");

    XtAddCallback(finder, XmNactivateCallback, activateCB, NULL);
    XtAddCallback(finder, XmNvalueChangedCallback, valueChangedCB, NULL);

    require(SgFinderGetChild(finder, SgFINDER_DROP_POCKET) != NULL,
            "default drop pocket missing");
    text = SgFinderGetChild(finder, SgFINDER_TEXT);
    require(text != NULL, "text child missing");
    require(XtIsSubclass(text, xmTextFieldWidgetClass),
            "text child is not XmTextField");
    require(SgFinderGetChild(finder, SgFINDER_ZOOM_BAR) != NULL,
            "zoom bar missing");
    history = SgFinderGetChild(finder, SgFINDER_HISTORY_MENUBAR);
    require(history != NULL, "default history menubar missing");

    fprintf(stderr, "SgFinder smoke: set/get text and callbacks\n");
    SgFinderSetTextString(finder, "/usr/people/test");
    value = SgFinderGetTextString(finder);
    require(value != NULL, "GetTextString returned NULL");
    require(strcmp(value, "/usr/people/test") == 0,
            "Set/Get text round trip failed");
    XtFree(value);
    require(activateCount == 1,
            "SgFinderSetTextString did not emit one activate callback");
    require(valueChangedCount >= 1,
            "text change did not emit valueChanged callback");

    fprintf(stderr, "SgFinder smoke: text child activate forwarding\n");
    XtCallCallbacks(text, XmNactivateCallback, NULL);
    require(activateCount == 2,
            "text activate did not forward to Finder");

    fprintf(stderr, "SgFinder smoke: six-entry history compatibility\n");
    SgFinderAddHistoryItem(finder, "/history/a");
    SgFinderAddHistoryItem(finder, "/history/b");
    SgFinderAddHistoryItem(finder, "/history/c");
    SgFinderAddHistoryItem(finder, "/history/d");
    SgFinderAddHistoryItem(finder, "/history/e");
    SgFinderAddHistoryItem(finder, "/history/f");
    SgFinderAddHistoryItem(finder, "/history/g");

    require(XtNameToWidget(history, "historyItem0") != NULL,
            "history item 0 missing");
    require(XtNameToWidget(history, "historyItem5") != NULL,
            "history item 5 missing");
    require(XtNameToWidget(history, "historyItem6") == NULL,
            "history grew beyond six entries");

    activateBeforeHistory = activateCount;
    historyItem = XtNameToWidget(history, "historyItem0");
    require(historyItem != NULL, "newest history item missing");
    XtCallCallbacks(historyItem, XmNactivateCallback, NULL);
    value = SgFinderGetTextString(finder);
    require(value != NULL, "history selection left text NULL");
    require(strcmp(value, "/history/g") == 0,
            "newest history selection did not restore Finder text");
    XtFree(value);
    require(activateCount == activateBeforeHistory + 1,
            "history selection did not emit Finder activate callback");

    SgFinderClearHistory(finder);
    require(XtNameToWidget(history, "historyItem0") == NULL,
            "clear history left menu items behind");

    fprintf(stderr, "SgFinder smoke: creation-time optional children\n");
    count = 0;
    XtSetArg(args[count], SgNuseDropPocket, False); ++count;
    XtSetArg(args[count], SgNuseHistoryMenu, False); ++count;
    optional = SgCreateFinder(shell, "minimalFinder", args, count);
    require(optional != NULL, "SgCreateFinder failed");
    XtManageChild(optional);
    require(SgFinderGetChild(optional, SgFINDER_DROP_POCKET) == NULL,
            "useDropPocket=False still created a drop pocket");
    require(SgFinderGetChild(optional, SgFINDER_HISTORY_MENUBAR) == NULL,
            "useHistoryMenu=False still created a history menubar");
    require(SgFinderGetChild(optional, SgFINDER_TEXT) != NULL,
            "minimal Finder lost text child");
    require(SgFinderGetChild(optional, SgFINDER_ZOOM_BAR) != NULL,
            "minimal Finder lost zoom bar");

    /* Documented safe no-op path when history was disabled at creation. */
    SgFinderAddHistoryItem(optional, "/ignored");
    SgFinderClearHistory(optional);

    fprintf(stderr, "SgFinder smoke: realize\n");
    XtRealizeWidget(shell);
    require(XtIsRealized(finder), "Finder did not realize");
    require(XtIsRealized(optional), "minimal Finder did not realize");

    XtDestroyWidget(shell);
    fprintf(stderr, "SgFinder smoke: PASS\n");
    return 0;
}
