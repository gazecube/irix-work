#include "Finders/IconFinder.h"

#include <Sgm/Finder.h>
#include <Xm/Xm.h>
#include <Xm/Display.h>
#include <Xm/TextF.h>
#include <X11/Shell.h>

#include <cstdio>
#include <cstdlib>

static int activateCount;
static int valueChangedCount;
static int lastActivateReason;
static int lastValueChangedReason;

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "ImdIconFinder smoke: FAIL: %s\n", message);
        std::exit(1);
    }
}

static void activateCB(Widget, XtPointer, XtPointer callData)
{
    XmAnyCallbackStruct *cb = static_cast<XmAnyCallbackStruct *>(callData);

    ++activateCount;
    lastActivateReason = cb ? cb->reason : -1;
}

static void valueChangedCB(Widget, XtPointer, XtPointer callData)
{
    XmAnyCallbackStruct *cb = static_cast<XmAnyCallbackStruct *>(callData);

    ++valueChangedCount;
    lastValueChangedReason = cb ? cb->reason : -1;
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell = XtVaAppInitialize(
        &app,
        const_cast<char *>("ImdIconFinderSmoke"),
        0,
        0,
        &argc,
        argv,
        0,
        NULL);

    /*
     * Original ImdIconFinder is a ViewKit component and normally enters here
     * after Motif/ViewKit has initialized the per-display XmDisplay object.
     * The bare smoke harness must establish the same prerequisite before the
     * first SgFinder resource conversion.
     */
    Widget xmDisplay = XmGetXmDisplay(XtDisplay(shell));
    require(xmDisplay != 0, "XmGetXmDisplay failed during application setup");

    ImdIconFinder finder("iconFinder", shell);
    Widget widget = finder.getWidget();

    require(widget != 0, "application finder did not create a widget");
    require(XtIsSubclass(widget, sgFinderWidgetClass),
            "application finder base widget is not SgFinder");

    /*
     * Check the libSgm -> libimdApp boundary directly.  These are public
     * SgFinder resources/children used by the original ImdIconFinder path;
     * keeping the assertions here catches a compatible-looking widget that
     * sends the wrong state or callback semantics to its application user.
     */
    unsigned char separator = 0;
    Boolean useDropPocket = False;
    Boolean useHistoryMenu = False;
    XtPointer setTextSectionFunc = 0;

    XtVaGetValues(widget,
                  SgNseparator, &separator,
                  SgNuseDropPocket, &useDropPocket,
                  SgNuseHistoryMenu, &useHistoryMenu,
                  SgNsetTextSectionFunc, &setTextSectionFunc,
                  NULL);

    require(separator == '/', "SgFinder separator is not '/'");
    require(useDropPocket == True, "SgFinder did not report useDropPocket=True");
    require(useHistoryMenu == True, "SgFinder did not report useHistoryMenu=True");
    require(setTextSectionFunc != 0, "SgFinder did not supply setTextSectionFunc");

    Widget dropPocket = SgFinderGetChild(widget, SgFINDER_DROP_POCKET);
    Widget text = SgFinderGetChild(widget, SgFINDER_TEXT);
    Widget zoomBar = SgFinderGetChild(widget, SgFINDER_ZOOM_BAR);
    Widget historyMenu = SgFinderGetChild(widget, SgFINDER_HISTORY_MENUBAR);

    require(dropPocket != 0, "application finder has no SgFinder drop pocket");
    require(text != 0, "application finder has no SgFinder text child");
    require(XtIsSubclass(text, xmTextFieldWidgetClass),
            "SgFinder text child is not XmTextField");
    require(zoomBar != 0, "application finder has no SgFinder zoom bar");
    require(historyMenu != 0, "application finder has no SgFinder history menu");
    require(finder.portableTextWidget() == text,
            "application finder did not retain the SgFinder text child");
    require(finder.portableIconFinderFrame() != 0,
            "iconFinderFrame was not created");
    require(finder.portableIconFinderPanel() != 0,
            "iconFinderPanel was not created");

    XtAddCallback(widget, XmNactivateCallback, activateCB, 0);
    XtAddCallback(widget, XmNvalueChangedCallback, valueChangedCB, 0);

    int activateBefore = activateCount;
    int valueBefore = valueChangedCount;

    finder.setContent("/usr/people/guest", false);
    require(finder.getContent() == "/usr/people/guest",
            "application setContent/getContent did not cross libSgm");
    require(activateCount == activateBefore + 1,
            "libSgm did not send one activate callback for setContent");
    require(lastActivateReason == XmCR_ACTIVATE,
            "libSgm sent the wrong activate callback reason");
    require(valueChangedCount > valueBefore,
            "libSgm did not send valueChanged for setContent");
    require(lastValueChangedReason == XmCR_VALUE_CHANGED,
            "libSgm sent the wrong valueChanged callback reason");

    char *wireText = SgFinderGetTextString(widget);
    require(wireText != 0, "SgFinderGetTextString returned NULL at app boundary");
    require(std::string(wireText) == "/usr/people/guest",
            "SgFinderGetTextString returned the wrong application value");
    XtFree(wireText);

    std::fprintf(stderr,
                 "ImdIconFinder smoke: SgFinder wire: separator='%c' drop=%d history=%d "
                 "activate=%d/%d valueChanged=%d/%d\n",
                 separator,
                 static_cast<int>(useDropPocket),
                 static_cast<int>(useHistoryMenu),
                 activateCount,
                 lastActivateReason,
                 valueChangedCount,
                 lastValueChangedReason);

    SgFinderAddHistoryItem(widget, const_cast<char *>("/usr/people/guest"));
    SgFinderAddHistoryItem(widget, const_cast<char *>("/usr/local"));
    require(SgFinderGetChild(widget, SgFINDER_HISTORY_MENUBAR) == historyMenu,
            "SgFinder changed history-menu identity after history updates");

    XtManageChild(widget);
    XtRealizeWidget(shell);
    XSync(XtDisplay(shell), False);

    require(XtIsRealized(widget), "application SgFinder did not realize");
    require(XtIsRealized(text), "application SgFinder text child did not realize");

    std::fprintf(stderr,
                 "ImdIconFinder smoke: PASS: libimdApp -> libSgm -> SgFinder\n");
    return 0;
}
