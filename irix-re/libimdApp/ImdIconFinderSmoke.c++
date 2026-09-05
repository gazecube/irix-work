#include "Finders/IconFinder.h"

#include <Sgm/Finder.h>
#include <Xm/Xm.h>
#include <Xm/Display.h>
#include <X11/Shell.h>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "ImdIconFinder smoke: FAIL: %s\n", message);
        std::exit(1);
    }
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
    require(finder.portableTextWidget() ==
                SgFinderGetChild(widget, SgFINDER_TEXT),
            "application finder did not retain the SgFinder text child");
    require(finder.portableIconFinderFrame() != 0,
            "iconFinderFrame was not created");
    require(finder.portableIconFinderPanel() != 0,
            "iconFinderPanel was not created");

    finder.setContent("/usr/people/guest", false);
    require(finder.getContent() == "/usr/people/guest",
            "application setContent/getContent did not cross libSgm");

    SgFinderAddHistoryItem(widget, const_cast<char *>("/usr/people/guest"));
    SgFinderAddHistoryItem(widget, const_cast<char *>("/usr/local"));
    require(SgFinderGetChild(widget, SgFINDER_HISTORY_MENUBAR) != 0,
            "application finder has no reconstructed history menu");

    XtManageChild(widget);
    XtRealizeWidget(shell);
    XSync(XtDisplay(shell), False);

    std::fprintf(stderr,
                 "ImdIconFinder smoke: PASS: libimdApp -> libSgm -> SgFinder\n");
    return 0;
}
