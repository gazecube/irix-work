#include "IconFinder.h"

#include "ArgList.h"
#include "ModelBasedIconPanel.h"
#include <Sgm/Finder.h>

#include <Vk/VkMenu.h>
#include <Vk/VkMenuBar.h>
#include <Vk/VkMenuItem.h>

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <string.h>

/*
 * Exact descriptor shape recovered from the original libimdApp.so data.
 */
static VkMenuDesc iconFinderPopupMenuDesc[] = {
    { END, 0, 0, 0, 0, 0 }
};

static VkMenuDesc iconFinderMenuBarDesc[] = {
    { SUBMENU, (char *)"finderSelectionMenu", 0,
      iconFinderPopupMenuDesc, 0, 0 },
    { END, 0, 0, 0, 0, 0 }
};

static Dimension preferredWidth(Widget widget)
{
    XtWidgetGeometry preferred;
    Dimension width = 1;

    if (!widget)
        return 1;

    memset(&preferred, 0, sizeof(preferred));
    XtQueryGeometry(widget, NULL, &preferred);
    if (preferred.request_mode & CWWidth)
        width = preferred.width;
    else
        XtVaGetValues(widget, XmNwidth, &width, NULL);

    return width ? width : 1;
}

/*
 * The original constructor attaches iconFinderFrame to top/bottom/left with
 * right NONE, and the ViewKit menu bar to top/bottom/right with left NONE.
 * Host SgFinder is currently an XmManager and therefore does not interpret
 * those Form-style constraint resources. Reproduce only the proven attachment
 * semantics here: preserve each child's preferred width, stretch vertically,
 * and anchor it to the recovered edge. The center Finder children remain under
 * SgFinder's own layout.
 */
static void layoutIconFinderChrome(ImdIconFinder *finder)
{
    Widget base;
    Widget frame;
    Widget menu;
    Widget selection;
    Dimension finderWidth = 1;
    Dimension finderHeight = 1;
    Dimension frameWidth = 1;
    Dimension menuWidth = 1;
    Dimension frameBorder = 0;
    Dimension menuBorder = 0;
    Position menuX = 0;

    if (!finder)
        return;

    base = finder->getWidget();
    frame = finder->portableIconFinderFrame();
    menu = finder->portableSelectionMenuBarWidget();
    selection = finder->portableSelectionMenuWidget();

    if (!base)
        return;

    XtVaGetValues(base,
                  XmNwidth, &finderWidth,
                  XmNheight, &finderHeight,
                  NULL);

    if (frame)
    {
        frameWidth = preferredWidth(frame);
        XtVaGetValues(frame, XmNborderWidth, &frameBorder, NULL);
        if (frameWidth > finderWidth)
            frameWidth = finderWidth;

        XtConfigureWidget(frame, 0, 0,
                          frameWidth, finderHeight,
                          frameBorder);
    }

    if (menu && selection)
    {
        /*
         * VkMenuBar expands to its parent width once shown, so its own
         * geometry is not a useful preferred-width source. The original
         * menu contains one recovered cascade; size from that cascade and
         * retain the small MenuBar margin that wraps it.
         */
        menuWidth = preferredWidth(selection);
        menuWidth = (menuWidth < 4) ? 4 : (Dimension)(menuWidth + 4);
        if (menuWidth > finderWidth)
            menuWidth = finderWidth;

        if (finderWidth > menuWidth)
            menuX = (Position)(finderWidth - menuWidth);

        XtVaGetValues(menu, XmNborderWidth, &menuBorder, NULL);
        XtConfigureWidget(menu, menuX, 0,
                          menuWidth, finderHeight,
                          menuBorder);
    }
}

static void iconFinderConfigureEH(Widget, XtPointer client,
                                  XEvent *event, Boolean *)
{
    ImdIconFinder *finder = (ImdIconFinder *)client;
    if (!finder || !event || event->type != ConfigureNotify)
        return;

    layoutIconFinderChrome(finder);
}

static void iconFinderMenuMapEH(Widget, XtPointer client,
                                XEvent *event, Boolean *)
{
    ImdIconFinder *finder = (ImdIconFinder *)client;
    if (!finder || !event || event->type != MapNotify)
        return;

    layoutIconFinderChrome(finder);
}

ImdIconFinder::ImdIconFinder(
    const char *name,
    Widget parent,
    int constructorValue)
    : _baseWidget(0),
      _textWidget(0),
      _iconFinderFrame(0),
      _iconFinderPanel(0),
      _menuBar(0),
      _finderSelectionMenu(0),
      _changingContent(false),
      _constructorValue(constructorValue)
{
    ImdArgList args;

    _baseWidget = XtCreateWidget(
        const_cast<char *>(name ? name : "iconFinder"),
        sgFinderWidgetClass,
        parent,
        args.getArgList(),
        args.getNumArgs());

    if (!_baseWidget)
        return;

    _textWidget = SgFinderGetChild(_baseWidget, SgFINDER_TEXT);

    _iconFinderFrame = XtVaCreateManagedWidget(
        "iconFinderFrame",
        xmFrameWidgetClass,
        _baseWidget,
        NULL);

    /* Exact attachment values recovered from libimdApp.so. */
    args.setArg(XmNtopAttachment, XmATTACH_FORM);
    args.setArg(XmNbottomAttachment, XmATTACH_FORM);
    args.setArg(XmNleftAttachment, XmATTACH_FORM);
    args.setArg(XmNrightAttachment, XmATTACH_NONE);
    args.setValues(_iconFinderFrame);

    args.clear();
    args.setArg(XmNtraversalOn, False);

    _iconFinderPanel = new ImdModelBasedIconPanel(
        "iconFinderPanel",
        _iconFinderFrame,
        0,
        args.getArgList(),
        args.getNumArgs());

    if (_iconFinderPanel && _iconFinderPanel->baseWidget())
        XtManageChild(_iconFinderPanel->baseWidget());

    _menuBar = new VkMenuBar(iconFinderMenuBarDesc, (XtPointer)this, False);
    if (_menuBar)
    {
        _menuBar->show(_baseWidget);
        _finderSelectionMenu =
            _menuBar->findNamedItem("finderSelectionMenu", False);

        Widget menuWidget = _menuBar->baseWidget();
        if (menuWidget)
        {
            XtAddEventHandler(menuWidget,
                              StructureNotifyMask,
                              False,
                              iconFinderMenuMapEH,
                              (XtPointer)this);
            XtAddEventHandler(_baseWidget,
                              StructureNotifyMask,
                              False,
                              iconFinderConfigureEH,
                              (XtPointer)this);

            layoutIconFinderChrome(this);
        }
    }
}

ImdIconFinder::~ImdIconFinder()
{
    if (_baseWidget)
        XtRemoveEventHandler(_baseWidget,
                             StructureNotifyMask,
                             False,
                             iconFinderConfigureEH,
                             (XtPointer)this);

    if (_menuBar && _menuBar->baseWidget())
        XtRemoveEventHandler(_menuBar->baseWidget(),
                             StructureNotifyMask,
                             False,
                             iconFinderMenuMapEH,
                             (XtPointer)this);

    delete _menuBar;
    _menuBar = 0;
    _finderSelectionMenu = 0;

    delete _iconFinderPanel;
    _iconFinderPanel = 0;
    _iconFinderFrame = 0;
    _textWidget = 0;

    if (_baseWidget && XtIsWidget(_baseWidget))
        XtDestroyWidget(_baseWidget);
    _baseWidget = 0;
}

void ImdIconFinder::setContent(const char *content, bool notify)
{
    (void)notify;

    if (!_baseWidget)
        return;

    _changingContent = true;
    SgFinderSetTextString(
        _baseWidget,
        const_cast<char *>(content ? content : ""));
    _changingContent = false;
}

std::string ImdIconFinder::getContent() const
{
    char *content;
    std::string result;

    if (!_baseWidget)
        return result;

    content = SgFinderGetTextString(_baseWidget);
    if (content)
    {
        result = content;
        XtFree(content);
    }

    return result;
}

Widget ImdIconFinder::getWidget() const
{
    return _baseWidget;
}

Widget ImdIconFinder::portableTextWidget() const
{
    return _textWidget;
}

Widget ImdIconFinder::portableIconFinderFrame() const
{
    return _iconFinderFrame;
}

ImdIconPanel *ImdIconFinder::portableIconFinderPanel() const
{
    return _iconFinderPanel;
}

Widget ImdIconFinder::portableSelectionMenuBarWidget() const
{
    return _menuBar ? _menuBar->baseWidget() : 0;
}

Widget ImdIconFinder::portableSelectionMenuWidget() const
{
    return _finderSelectionMenu ? _finderSelectionMenu->baseWidget() : 0;
}
