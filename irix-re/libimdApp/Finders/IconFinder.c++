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
#include <stdio.h>
#include <string.h>

/* Exact descriptor shape recovered from the original libimdApp.so data. */
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
 * Temporary diagnostic while reconstructing the SGI XmManager constraint
 * behaviour used by ImdIconFinder.  Finder.o itself only lays out its three
 * internal children; libimdApp adds iconFinderFrame and the ViewKit menu as
 * additional direct children.
 */
static void dumpFinderChildren(Widget finder, const char *where)
{
    CompositeWidget composite;
    Cardinal i;

    if (!finder || !XtIsComposite(finder))
        return;

    composite = (CompositeWidget)finder;
    fprintf(stderr,
            "[ImdIconFinder][TEMP] %s finder=%s core=%ux%u children=%u\n",
            where ? where : "?",
            XtName(finder),
            (unsigned)finder->core.width,
            (unsigned)finder->core.height,
            (unsigned)composite->composite.num_children);

    for (i = 0; i < composite->composite.num_children; ++i)
    {
        Widget child = composite->composite.children[i];
        if (!child)
            continue;

        fprintf(stderr,
                "[ImdIconFinder][TEMP]   child[%u] name=%s class=%s managed=%d geom=%d,%d %ux%u border=%u\n",
                (unsigned)i,
                XtName(child),
                XtClass(child) ? XtClass(child)->core_class.class_name : "?",
                XtIsManaged(child) ? 1 : 0,
                (int)child->core.x,
                (int)child->core.y,
                (unsigned)child->core.width,
                (unsigned)child->core.height,
                (unsigned)child->core.border_width);
    }
}

/*
 * Keep only the part of the host seam that is proven useful: collapse the
 * ViewKit MenuBar to the width of its single recovered cascade and pin it to
 * the right edge.  Do NOT force either external child to the Finder's full
 * height.  That was a host-only approximation and visibly compressed the
 * original Finder row.
 *
 * The original libimdApp constructor sets top/bottom/right attachment
 * resources on this MenuBar.  IRIX XmManager carries SGI-private constraint
 * behaviour that host OpenMotif lacks; until that is reconstructed, preserve
 * ViewKit's natural y/height and change only x/width.
 */
static void placeIconFinderMenu(ImdIconFinder *finder)
{
    Widget base;
    Widget menu;
    Widget selection;
    Dimension finderWidth = 1;
    Dimension menuWidth = 1;
    Dimension menuHeight = 1;
    Dimension menuBorder = 0;
    Position menuY = 0;
    Position menuX = 0;

    if (!finder)
        return;

    base = finder->getWidget();
    menu = finder->portableSelectionMenuBarWidget();
    selection = finder->portableSelectionMenuWidget();

    if (!base || !menu || !selection)
        return;

    XtVaGetValues(base, XmNwidth, &finderWidth, NULL);

    menuWidth = preferredWidth(selection);
    menuWidth = (menuWidth < 4) ? 4 : (Dimension)(menuWidth + 4);
    if (menuWidth > finderWidth)
        menuWidth = finderWidth;

    if (finderWidth > menuWidth)
        menuX = (Position)(finderWidth - menuWidth);

    XtVaGetValues(menu,
                  XmNy, &menuY,
                  XmNheight, &menuHeight,
                  XmNborderWidth, &menuBorder,
                  NULL);

    XtConfigureWidget(menu, menuX, menuY,
                      menuWidth, menuHeight ? menuHeight : 1,
                      menuBorder);
}

static void iconFinderConfigureEH(Widget, XtPointer client,
                                  XEvent *event, Boolean *)
{
    ImdIconFinder *finder = (ImdIconFinder *)client;
    if (!finder || !event || event->type != ConfigureNotify)
        return;

    placeIconFinderMenu(finder);
}

static void iconFinderMenuMapEH(Widget, XtPointer client,
                                XEvent *event, Boolean *)
{
    ImdIconFinder *finder = (ImdIconFinder *)client;
    if (!finder || !event || event->type != MapNotify)
        return;

    placeIconFinderMenu(finder);
    dumpFinderChildren(finder->getWidget(), "menu-map");
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

    dumpFinderChildren(_baseWidget, "after-finder-create");

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

    dumpFinderChildren(_baseWidget, "after-frame-create");

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

            placeIconFinderMenu(this);
            dumpFinderChildren(_baseWidget, "after-menu-create");
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
