#include "PandoraViewKit.h"

#include "Finders/IconFinder.h"
#include <Sgm/Finder.h>
#include <Vk/VkApp.h>
#include <Xm/TextF.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PANDORA_APP_DEFAULTS_PATH
#define PANDORA_APP_DEFAULTS_PATH "%N"
#endif

#ifndef PANDORA_USER_TOKEN_FTI_PATH
#define PANDORA_USER_TOKEN_FTI_PATH ""
#endif

static int finderActivateCount;
static int finderValueChangedCount;
static Boolean finderActivateReasonOK = True;
static Boolean finderValueChangedReasonOK = True;

static void finderActivateCB(Widget, XtPointer, XtPointer callData)
{
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)callData;

    ++finderActivateCount;
    if (!cb || cb->reason != XmCR_ACTIVATE)
        finderActivateReasonOK = False;
}

static void finderValueChangedCB(Widget, XtPointer, XtPointer callData)
{
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)callData;

    ++finderValueChangedCount;
    if (!cb || cb->reason != XmCR_VALUE_CHANGED)
        finderValueChangedReasonOK = False;
}

static Boolean runIconFinderSmoke(Widget parent)
{
    ImdIconFinder finder("iconFinderSmoke", parent);
    Widget widget = finder.getWidget();
    Widget text;
    Widget dropPocket;
    Widget zoomBar;
    Widget historyMenu;
    unsigned char separator = 0;
    Boolean useDropPocket = False;
    Boolean useHistoryMenu = False;
    XtPointer setTextSectionFunc = 0;
    char *wireText = 0;
    Boolean ok = True;

    /* exercise the application side of the reconstructed SgFinder interface.
     */
    if (!widget || !XtIsSubclass(widget, sgFinderWidgetClass))
    {
        fprintf(stderr, "ImdIconFinder probe: FAIL: base widget is not SgFinder\n");
        return False;
    }

    text = SgFinderGetChild(widget, SgFINDER_TEXT);
    dropPocket = SgFinderGetChild(widget, SgFINDER_DROP_POCKET);
    zoomBar = SgFinderGetChild(widget, SgFINDER_ZOOM_BAR);
    historyMenu = SgFinderGetChild(widget, SgFINDER_HISTORY_MENUBAR);

    XtVaGetValues(
        widget,
        SgNseparator, &separator,
        SgNuseDropPocket, &useDropPocket,
        SgNuseHistoryMenu, &useHistoryMenu,
        SgNsetTextSectionFunc, &setTextSectionFunc,
        NULL);

    finderActivateCount = 0;
    finderValueChangedCount = 0;
    finderActivateReasonOK = True;
    finderValueChangedReasonOK = True;
    XtAddCallback(widget, XmNactivateCallback, finderActivateCB, 0);
    XtAddCallback(widget, XmNvalueChangedCallback, finderValueChangedCB, 0);

    finder.setContent("/usr/people/iconfinder-probe", false);
    wireText = SgFinderGetTextString(widget);

    if (separator != '/' || !useDropPocket || !useHistoryMenu ||
        !setTextSectionFunc || !dropPocket || !text || !zoomBar ||
        !historyMenu || !XtIsSubclass(text, xmTextFieldWidgetClass) ||
        finder.portableTextWidget() != text || !wireText ||
        strcmp(wireText, "/usr/people/iconfinder-probe") != 0 ||
        finder.getContent() != "/usr/people/iconfinder-probe" ||
        finderActivateCount != 1 || finderValueChangedCount < 1 ||
        !finderActivateReasonOK || !finderValueChangedReasonOK)
        ok = False;

    fprintf(stderr,
            "ImdIconFinder probe: class=%s separator='%c' drop=%s history=%s\n",
            XtClass(widget)->core_class.class_name,
            separator ? separator : '?',
            useDropPocket ? "True" : "False",
            useHistoryMenu ? "True" : "False");
    fprintf(stderr,
            "ImdIconFinder probe: children drop=%p text=%p zoom=%p history=%p\n",
            (void *)dropPocket, (void *)text, (void *)zoomBar,
            (void *)historyMenu);
    fprintf(stderr,
            "ImdIconFinder probe: text=%s activate=%d reason=%s valueChanged=%d reason=%s\n",
            wireText ? wireText : "NULL",
            finderActivateCount,
            finderActivateReasonOK ? "PASS" : "FAIL",
            finderValueChangedCount,
            finderValueChangedReasonOK ? "PASS" : "FAIL");
    fprintf(stderr, "ImdIconFinder probe: %s\n", ok ? "PASS" : "FAIL");

    if (wireText)
        XtFree(wireText);

    return ok;
}

int main(int argc, char **argv)
{
    /* use the normal Clogin app-defaults search path.
     */
    setenv("XFILESEARCHPATH", PANDORA_APP_DEFAULTS_PATH, 1);

    /* expose the verified generic user artwork to the renderer probe.
     */
    if (PANDORA_USER_TOKEN_FTI_PATH[0] != '\0')
        setenv("PANDORA_USER_TOKEN_FTI", PANDORA_USER_TOKEN_FTI_PATH, 1);

    VkApp app("Clogin", &argc, argv);
    PandoraWindow window("pandoraWindow");

    app.setMainWindow(&window);
    window.setTitle("Clogin Pandora ViewKit probe");
    window.show();

    runIconFinderSmoke(window.baseWidget());

    app.run();
    return 0;
}
