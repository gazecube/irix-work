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
    if (!cb || cb->reason != XmCR_ACTIVATE) exit(20);
    ++activateCount;
}

static void valueChangedCB(Widget w, XtPointer clientData, XtPointer callData)
{
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)callData;
    (void)w;
    (void)clientData;
    if (!cb || cb->reason != XmCR_VALUE_CHANGED) exit(21);
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
    Widget shell, finder, rawNoArgs, rawGeometry, text, history, historyItem, optional;
    Arg args[2];
    Cardinal count;
    char *value;
    int activateBeforeHistory;

    shell = XtVaAppInitialize(&app, "SgFinderSmoke", NULL, 0, &argc, argv, NULL, NULL);
    require(shell != NULL, "XtVaAppInitialize returned NULL");

    fprintf(stderr, "SgFinder smoke: raw XtCreateWidget no args\n");
    rawNoArgs = XtCreateWidget("rawNoArgs", sgFinderWidgetClass, shell, NULL, 0);
    require(rawNoArgs != NULL, "raw no-args Finder creation failed");
    XtDestroyWidget(rawNoArgs);

    fprintf(stderr, "SgFinder smoke: raw XtCreateWidget geometry args\n");
    count = 0;
    XtSetArg(args[count], XtNwidth, 360); ++count;
    XtSetArg(args[count], XtNheight, 52); ++count;
    rawGeometry = XtCreateWidget("rawGeometry", sgFinderWidgetClass, shell, args, count);
    require(rawGeometry != NULL, "raw geometry Finder creation failed");
    XtDestroyWidget(rawGeometry);

    fprintf(stderr, "SgFinder smoke: direct sgFinderWidgetClass create\n");
    finder = XtVaCreateManagedWidget("finder", sgFinderWidgetClass, shell,
                                     XtNwidth, 360, XtNheight, 52, NULL);
    require(finder != NULL, "direct Finder creation failed");
    require(XtIsSubclass(finder, sgFinderWidgetClass), "direct widget is not an SgFinder subclass");

    XtAddCallback(finder, XmNactivateCallback, activateCB, NULL);
    XtAddCallback(finder, XmNvalueChangedCallback, valueChangedCB, NULL);

    require(SgFinderGetChild(finder, SgFINDER_DROP_POCKET) != NULL, "default drop pocket missing");
    text = SgFinderGetChild(finder, SgFINDER_TEXT);
    require(text != NULL, "text child missing");
    require(XtIsSubclass(text, xmTextFieldWidgetClass), "text child is not XmTextField");
    require(SgFinderGetChild(finder, SgFINDER_ZOOM_BAR) != NULL, "zoom bar missing");
    history = SgFinderGetChild(finder, SgFINDER_HISTORY_MENUBAR);
    require(history != NULL, "default history menubar missing");

    fprintf(stderr, "SgFinder smoke: set/get text and callbacks\n");
    SgFinderSetTextString(finder, "/usr/people/test");
    value = SgFinderGetTextString(finder);
    require(value != NULL, "GetTextString returned NULL");
    require(strcmp(value, "/usr/people/test") == 0, "Set/Get text round trip failed");
    XtFree(value);
    require(activateCount == 1, "SgFinderSetTextString did not emit one activate callback");
    require(valueChangedCount >= 1, "text change did not emit valueChanged callback");

    fprintf(stderr, "SgFinder smoke: text child activate forwarding\n");
    XtCallCallbacks(text, XmNactivateCallback, NULL);
    require(activateCount == 2, "text activate did not forward to Finder");

    fprintf(stderr, "SgFinder smoke: six-entry history compatibility\n");
    SgFinderAddHistoryItem(finder, "/history/a");
    SgFinderAddHistoryItem(finder, "/history/b");
    SgFinderAddHistoryItem(finder, "/history/c");
    SgFinderAddHistoryItem(finder, "/history/d");
    SgFinderAddHistoryItem(finder, "/history/e");
    SgFinderAddHistoryItem(finder, "/history/f");
    SgFinderAddHistoryItem(finder, "/history/g");
    require(XtNameToWidget(history, "historyItem0") != NULL, "history item 0 missing");
    require(XtNameToWidget(history, "historyItem5") != NULL, "history item 5 missing");
    require(XtNameToWidget(history, "historyItem6") == NULL, "history grew beyond six entries");

    activateBeforeHistory = activateCount;
    historyItem = XtNameToWidget(history, "historyItem0");
    XtCallCallbacks(historyItem, XmNactivateCallback, NULL);
    value = SgFinderGetTextString(finder);
    require(value && strcmp(value, "/history/g") == 0, "newest history selection failed");
    XtFree(value);
    require(activateCount == activateBeforeHistory + 1, "history selection did not emit Finder activate callback");

    fprintf(stderr, "SgFinder smoke: duplicate history promotion\n");
    SgFinderAddHistoryItem(finder, "/history/c");
    historyItem = XtNameToWidget(history, "historyItem0");
    activateBeforeHistory = activateCount;
    XtCallCallbacks(historyItem, XmNactivateCallback, NULL);
    value = SgFinderGetTextString(finder);
    require(value && strcmp(value, "/history/c") == 0, "duplicate history item was not promoted");
    XtFree(value);
    require(activateCount == activateBeforeHistory + 1, "promoted duplicate did not emit Finder activate callback");
    require(XtNameToWidget(history, "historyItem5") != NULL, "duplicate promotion changed history size");
    require(XtNameToWidget(history, "historyItem6") == NULL, "duplicate promotion grew history");

    SgFinderClearHistory(finder);
    require(XtNameToWidget(history, "historyItem0") == NULL, "clear history left menu items behind");

    fprintf(stderr, "SgFinder smoke: creation-time optional children\n");
    count = 0;
    XtSetArg(args[count], SgNuseDropPocket, False); ++count;
    XtSetArg(args[count], SgNuseHistoryMenu, False); ++count;
    optional = SgCreateFinder(shell, "minimalFinder", args, count);
    require(optional != NULL, "SgCreateFinder failed");
    XtManageChild(optional);
    require(SgFinderGetChild(optional, SgFINDER_DROP_POCKET) == NULL, "useDropPocket=False still created a drop pocket");
    require(SgFinderGetChild(optional, SgFINDER_HISTORY_MENUBAR) == NULL, "useHistoryMenu=False still created history");
    require(SgFinderGetChild(optional, SgFINDER_TEXT) != NULL, "minimal Finder lost text child");
    require(SgFinderGetChild(optional, SgFINDER_ZOOM_BAR) != NULL, "minimal Finder lost zoom bar");
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
