#include "IconFinder.h"

#include "ArgList.h"
#include "ModelBasedIconPanel.h"
#include <Sgm/Finder.h>

#include <Vk/VkMenu.h>
#include <Vk/VkMenuBar.h>

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <X11/StringDefs.h>

/*
 * Exact descriptor shape recovered from the original libimdApp.so data:
 *
 *   _menuBarDesc:
 *     { SUBMENU, "finderSelectionMenu", 0, _popupMenuDesc, 0, 0 }
 *     { END,     0,                     0, 0,              0, 0 }
 *
 *   _popupMenuDesc:
 *     { END, 0, 0, 0, 0, 0 }
 *
 * The original addMenuItem() populates this submenu dynamically through
 * VkMenu::addAction(). Keeping it empty here matches the constructor state.
 */
static VkMenuDesc iconFinderPopupMenuDesc[] = {
    { END, 0, 0, 0, 0, 0 }
};

static VkMenuDesc iconFinderMenuBarDesc[] = {
    { SUBMENU, (char *)"finderSelectionMenu", 0,
      iconFinderPopupMenuDesc, 0, 0 },
    { END, 0, 0, 0, 0, 0 }
};

/*
 * Host layout seam.
 *
 * The original SgFinder accepts the Form-style constraints used by the
 * ImdIconFinder constructor. Our current SgFinder host reconstruction is an
 * XmManager with manual layout, so position the recovered VkMenuBar at the
 * right edge after it has been built. This does not claim original private
 * SgFinder geometry; it preserves the proven ownership/widget identity until
 * that superclass layout is reconstructed.
 */
static void placeIconFinderMenuBar(Widget finder, Widget menuBar)
{
    Dimension finderWidth = 1;
    Dimension finderHeight = 1;
    Dimension menuWidth = 1;
    Dimension menuHeight = 1;
    Position x = 0;

    if (!finder || !menuBar)
        return;

    XtVaGetValues(finder,
                  XmNwidth, &finderWidth,
                  XmNheight, &finderHeight,
                  NULL);
    XtVaGetValues(menuBar,
                  XmNwidth, &menuWidth,
                  XmNheight, &menuHeight,
                  NULL);

    if (menuWidth > finderWidth)
        menuWidth = finderWidth;
    if (menuHeight < finderHeight)
        menuHeight = finderHeight;
    if (finderWidth > menuWidth)
        x = (Position)(finderWidth - menuWidth);

    XtConfigureWidget(menuBar, x, 0, menuWidth, menuHeight, 0);
}

static void iconFinderConfigureEH(Widget widget, XtPointer client,
                                  XEvent *event, Boolean *)
{
    ImdIconFinder *finder = (ImdIconFinder *)client;
    if (!finder || !event || event->type != ConfigureNotify)
        return;

    Widget menu = finder->portableSelectionMenuBarWidget();
    if (menu)
        placeIconFinderMenuBar(widget, menu);
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
    /* make the finder base widget
     *
     * RE: the original constructor clears a local ImdArgList and creates
     * the SgFinder with no arguments at 0x0963edc8..0x0963edf4.
     */
    ImdArgList args;
    _baseWidget = XtCreateWidget(
        const_cast<char *>(name ? name : "iconFinder"),
        sgFinderWidgetClass,
        parent,
        args.getArgList(),
        args.getNumArgs());

    if (!_baseWidget)
        return;

    /*
     * Host access seam. The original IRIX 6.5.5 public Finder.h proves that
     * SgFINDER_TEXT is selector 0. The original ImdIconFinder constructor's
     * own completion-field plumbing is richer and will replace this seam as
     * that class is reconstructed.
     */
    _textWidget = SgFinderGetChild(_baseWidget, SgFINDER_TEXT);

    /* make the frame */
    _iconFinderFrame = XtVaCreateManagedWidget(
        "iconFinderFrame",
        xmFrameWidgetClass,
        _baseWidget,
        NULL);

    /* attach the frame to three sides of the finder
     *
     * RE: libimdApp was built with FAST_QUARKS. the encoded resource
     * pointers -66, -102, -70 and -103 decode through SGI FastStrDefs.h
     * to the four attachment resources below. the values are exactly
     * 1, 1, 1 and 0 in the original constructor.
     */
    args.setArg(XmNtopAttachment, XmATTACH_FORM);
    args.setArg(XmNbottomAttachment, XmATTACH_FORM);
    args.setArg(XmNleftAttachment, XmATTACH_FORM);
    args.setArg(XmNrightAttachment, XmATTACH_NONE);
    args.setValues(_iconFinderFrame);

    /* make the model panel. this argument order is taken directly from the
     * imported constructor symbol in libimdApp.so:
     *
     * ImdModelBasedIconPanel(const char *, Widget, ImdModel *, Arg *, int)
     */
    args.clear();

    /* do not traverse into the icon panel
     *
     * RE: encoded resource pointer -49 is XmNtraversalOn in SGI
     * FastStrDefs.h and is passed with value 0. the original constructor
     * adds one more string-named Boolean resource immediately after this;
     * its spelling is not recovered yet, so it remains omitted here.
     */
    args.setArg(XmNtraversalOn, False);

    _iconFinderPanel = new ImdModelBasedIconPanel(
        "iconFinderPanel",
        _iconFinderFrame,
        0,
        args.getArgList(),
        args.getNumArgs());

    if (_iconFinderPanel && _iconFinderPanel->baseWidget())
        XtManageChild(_iconFinderPanel->baseWidget());

    /*
     * Original constructor sequence, recovered from libimdApp.so:
     *
     *   new VkMenuBar(_menuBarDesc, this, False)
     *   menuBar->show(iconFinder)
     *   attach menuBar top/bottom/right, left NONE
     *   menuBar->findNamedItem("finderSelectionMenu", False)
     *
     * The app-defaults resource for finderSelectionMenu supplies the shipped
     * iconFinderPrevious pixmap, so creating the original ViewKit item also
     * restores the correct resource match instead of inventing an FM button.
     */
    _menuBar = new VkMenuBar(iconFinderMenuBarDesc, (XtPointer)this, False);
    if (_menuBar) {
        _menuBar->show(_baseWidget);
        _finderSelectionMenu =
            _menuBar->findNamedItem("finderSelectionMenu", False);

        Widget menuWidget = _menuBar->baseWidget();
        if (menuWidget) {
            placeIconFinderMenuBar(_baseWidget, menuWidget);
            XtAddEventHandler(_baseWidget,
                              StructureNotifyMask,
                              False,
                              iconFinderConfigureEH,
                              (XtPointer)this);
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

    /* suppress the normal content-change path while setting the field.
     * ImdCompletionField notification is restored with that class.
     */
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
    if (content) {
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
