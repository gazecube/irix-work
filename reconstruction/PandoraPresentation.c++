#include "PandoraPresentation.h"
#include "FtiVectorIcon.h"

#include <Xm/DrawingA.h>
#include <Xm/Label.h>
#include <Xm/Xm.h>

#include <X11/Xlib.h>

#include <pwd.h>

#ifndef PANDORA_USER_TOKEN_FTI_PATH
#define PANDORA_USER_TOKEN_FTI_PATH ""
#endif

namespace
{
FtiVectorIcon &portableUserTokenIcon()
{
    static FtiVectorIcon icon;
    static bool attempted = false;

    if (!attempted)
    {
        attempted = true;
        const char *path = PANDORA_USER_TOKEN_FTI_PATH;
        if (path && path[0])
            icon.load(path);
    }

    return icon;
}
}

PandoraRenderer::PandoraRenderer()
    : _gallery(false), _scale(1.0f)
{
}

void PandoraRenderer::setGallery(bool gallery)
{
    _gallery = gallery;
}

void PandoraRenderer::setScale(float scale)
{
    _scale = scale;
}

PandoraLayout::PandoraLayout(Widget panelWidget)
    : _panelWidget(panelWidget),
      _container(panelWidget),
      _fontProbe(NULL),
      _selectionCallback(NULL),
      _selectionClientData(NULL),
      _commandCallback(NULL),
      _commandClientData(NULL),
      _selectedView(NULL),
      _lastClickTime(0)
{
    /*
     * Binary-proven PandoraLayout constants:
     *
     *   +0x0c = 10   outer inset
     *   +0x10 = 12   inter-cell spacing
     *
     * The original ImdViewPanel owns drawing and selection. The portable probe
     * now follows that architecture: UserView is geometry/state only and the
     * panel is the sole visible drawing surface. One unmanaged XmLabel is kept
     * only as a portable Motif font-list source until getPosFont() is wired.
     *
     * libimdIconPanel.so also preserves the panel translation table. Button 1
     * down invokes Select(), CheckForDoubleClick(), Expand(), MouseClick(), and
     * BoxSelectArm(); Shift+Button1 uses ExtendSelect(). Clogin disables box
     * selection and rename, so this probe bridges ordinary Button1 press only.
     */
    if (_panelWidget)
    {
        _fontProbe = XtVaCreateWidget(
            "userViewFontProbe",
            xmLabelWidgetClass,
            _panelWidget,
            XmNrecomputeSize, True,
            NULL);

        XtAddEventHandler(
            _panelWidget,
            StructureNotifyMask,
            False,
            &PandoraLayout::panelStructureEvent,
            (XtPointer)this);
        XtAddEventHandler(
            _panelWidget,
            ButtonPressMask,
            False,
            &PandoraLayout::panelButtonEvent,
            (XtPointer)this);
        XtAddCallback(
            _panelWidget,
            XmNexposeCallback,
            &PandoraLayout::panelExposeCallback,
            (XtPointer)this);
    }
}

PandoraLayout::~PandoraLayout()
{
    if (_panelWidget)
    {
        XtRemoveEventHandler(
            _panelWidget,
            StructureNotifyMask,
            False,
            &PandoraLayout::panelStructureEvent,
            (XtPointer)this);
        XtRemoveEventHandler(
            _panelWidget,
            ButtonPressMask,
            False,
            &PandoraLayout::panelButtonEvent,
            (XtPointer)this);
        XtRemoveCallback(
            _panelWidget,
            XmNexposeCallback,
            &PandoraLayout::panelExposeCallback,
            (XtPointer)this);
    }
}

void PandoraLayout::setSelectionCallback(
    XtCallbackProc callback,
    XtPointer clientData)
{
    _selectionCallback = callback;
    _selectionClientData = clientData;
}

void PandoraLayout::setCommandCallback(
    XtCallbackProc callback,
    XtPointer clientData)
{
    _commandCallback = callback;
    _commandClientData = clientData;
}

XmFontList PandoraLayout::fontList() const
{
    XmFontList result = NULL;
    if (_fontProbe)
        XtVaGetValues(_fontProbe, XmNfontList, &result, NULL);
    return result;
}

void PandoraLayout::panelStructureEvent(
    Widget,
    XtPointer clientData,
    XEvent *event,
    Boolean *)
{
    PandoraLayout *self = static_cast<PandoraLayout *>(clientData);
    if (!self || !event)
        return;

    if (event->type == ConfigureNotify || event->type == MapNotify)
        self->resize();
}

void PandoraLayout::panelButtonEvent(
    Widget widget,
    XtPointer clientData,
    XEvent *event,
    Boolean *)
{
    PandoraLayout *self = static_cast<PandoraLayout *>(clientData);
    if (!self || !event || event->type != ButtonPress)
        return;
    if (event->xbutton.button != Button1)
        return;

    /*
     * Binary shape:
     *   panel Button1Down -> Select()
     *   Select() -> ImdViewPanelFindIntersectingView(panel, x, y)
     *   FindIntersectingView -> renderer->intersects(view, x, y)
     *
     * The portable bridge keeps the same division: viewAt() delegates to the
     * UserView's renderer-shaped icon/label intersection test and passes the
     * selected view upward. The real Imd selected-view list is still pending.
     */
    UserView *view = self->viewAt(
        static_cast<Position>(event->xbutton.x),
        static_cast<Position>(event->xbutton.y));
    if (!view)
        return;

    const bool selectionChanged = view != self->_selectedView;
    if (selectionChanged)
    {
        self->_selectedView = view;
        if (self->_selectionCallback)
            self->_selectionCallback(
                widget, self->_selectionClientData, (XtPointer)view);
    }

    /*
     * Portable bridge for the binary-proven CheckForDoubleClick()/command
     * channel. The original ImdIconPanel owns this state and emits
     * kCommandCallback separately from kSelectionChangedCallback. Keep the
     * channels separate here until the complete Imd callback/list ABI is
     * reconstructed.
     */
    const Time now = event->xbutton.time;
    const int multiClick = XtGetMultiClickTime(XtDisplay(widget));
    const bool command = !selectionChanged && self->_lastClickTime != 0 &&
        now - self->_lastClickTime <= static_cast<Time>(multiClick);
    self->_lastClickTime = now;

    if (command && self->_commandCallback)
        self->_commandCallback(
            widget, self->_commandClientData, (XtPointer)view);
}

void PandoraLayout::panelExposeCallback(
    Widget,
    XtPointer clientData,
    XtPointer)
{
    PandoraLayout *self = static_cast<PandoraLayout *>(clientData);
    if (self)
        self->draw();
}

void PandoraLayout::addView(UserView *view)
{
    if (!view)
        return;

    view->preparePortableGeometry(fontList());
    _views.push_back(view);
}

void PandoraLayout::resize()
{
    /*
     * PandoraLayout::resize() at 0x10018408 calls
     * ImdViewPanelUpdateLayout(panel). update() is the portable equivalent.
     */
    update();
}

void PandoraLayout::update()
{
    if (!_panelWidget || _views.empty())
        return;

    const Dimension outerInset = 10;
    const Dimension spacing = 12;

    Dimension panelWidth = 0;
    Dimension panelHeight = 0;
    XtVaGetValues(
        _panelWidget,
        XmNwidth, &panelWidth,
        XmNheight, &panelHeight,
        NULL);
    if (panelWidth == 0)
        panelWidth = 1;

    Dimension maxWidth = 0;
    Dimension maxHeight = 0;
    for (std::size_t i = 0; i < _views.size(); ++i)
    {
        UserView *view = _views[i];
        if (!view)
            continue;
        if (view->width() > maxWidth)
            maxWidth = view->width();
        if (view->height() > maxHeight)
            maxHeight = view->height();
    }

    if (maxWidth == 0 || maxHeight == 0)
        return;

    /* Binary-proven PandoraLayout::update(bool) column calculation. */
    long numerator = static_cast<long>(panelWidth)
                   - 2L * static_cast<long>(outerInset)
                   + static_cast<long>(spacing);
    long denominator = static_cast<long>(maxWidth)
                     + static_cast<long>(spacing);
    long columns = denominator > 0 ? numerator / denominator : 1;
    if (columns < 1)
        columns = 1;

    long column = 0;
    Position cellX = static_cast<Position>(outerInset);
    Position cellY = static_cast<Position>(outerInset);

    for (std::size_t i = 0; i < _views.size(); ++i)
    {
        UserView *view = _views[i];
        if (!view)
            continue;

        const Position x = static_cast<Position>(
            cellX + static_cast<Position>((maxWidth - view->width()) / 2));
        view->setPortablePosition(x, cellY);

        ++column;
        if (column == columns)
        {
            column = 0;
            cellX = static_cast<Position>(outerInset);
            cellY = static_cast<Position>(
                cellY + static_cast<Position>(maxHeight + spacing));
        }
        else
        {
            cellX = static_cast<Position>(
                cellX + static_cast<Position>(maxWidth + spacing));
        }
    }

    Dimension contentHeight;
    if (column == 0)
        contentHeight = static_cast<Dimension>(cellY - spacing + outerInset);
    else
        contentHeight = static_cast<Dimension>(
            cellY + static_cast<Position>(maxHeight + outerInset));

    if (contentHeight > panelHeight)
        XtVaSetValues(_panelWidget, XmNheight, contentHeight, NULL);

    if (XtIsRealized(_panelWidget))
        XClearArea(XtDisplay(_panelWidget), XtWindow(_panelWidget), 0, 0, 0, 0, True);
}

void PandoraLayout::draw()
{
    if (!_panelWidget || !XtIsRealized(_panelWidget))
        return;

    XmFontList fonts = fontList();
    for (std::size_t i = 0; i < _views.size(); ++i)
    {
        if (_views[i])
            _views[i]->drawPortableContents(_panelWidget, fonts);
    }
}

UserView *PandoraLayout::viewAt(Position x, Position y) const
{
    XmFontList fonts = fontList();
    for (std::size_t i = 0; i < _views.size(); ++i)
    {
        UserView *view = _views[i];
        if (view && view->intersectsPortable(x, y, fonts))
            return view;
    }
    return NULL;
}

UserView::UserView(
    const std::string &name,
    long uid,
    const std::string &home)
    : _name(name),
      _home(home),
      _uid(static_cast<uid_t>(uid)),
      _x(0),
      _y(0),
      _width(50),
      _height(53)
{
}

UserView::UserView(
    const std::string &name,
    long,
    long)
    : _name(name),
      _home(),
      _uid(0),
      _x(0),
      _y(0),
      _width(50),
      _height(53)
{
    struct passwd *pwd = getpwnam(_name.c_str());
    if (pwd)
    {
        _uid = pwd->pw_uid;
        if (pwd->pw_dir)
            _home = pwd->pw_dir;
    }
}

void UserView::preparePortableGeometry(XmFontList fontList)
{
    XmString label = XmStringCreateLocalized(
        const_cast<char *>(_name.c_str()));
    Dimension labelWidth = 0;
    Dimension labelHeight = 0;
    if (fontList)
        XmStringExtent(fontList, label, &labelWidth, &labelHeight);
    XmStringFree(label);

    const Dimension iconExtent = 50;
    const Dimension invertedTeeMargin = 3;

    /* Binary-proven setSize() formula; XmStringExtent is portable stand-in. */
    _width = labelWidth > iconExtent ? labelWidth : iconExtent;
    _height = static_cast<Dimension>(
        iconExtent + invertedTeeMargin + labelHeight);
}

void UserView::setPortablePosition(Position x, Position y)
{
    _x = x;
    _y = y;
}

bool UserView::intersectsPortable(
    Position x,
    Position y,
    XmFontList fontList) const
{
    /*
     * ImdIconRenderer::intersects() at 0x096af238 calls intersectsIcon() and
     * intersectsLabel() separately. Mirror that structure instead of testing
     * the complete ImdView bounding rectangle.
     */
    const long px = static_cast<long>(x);
    const long py = static_cast<long>(y);
    const long iconExtent = 50;
    const long iconLeft = static_cast<long>(_x)
        + (static_cast<long>(_width) > iconExtent
            ? (static_cast<long>(_width) - iconExtent) / 2
            : 0);
    const long iconTop = static_cast<long>(_y);

    if (px >= iconLeft && px < iconLeft + iconExtent &&
        py >= iconTop && py < iconTop + iconExtent)
        return true;

    XmString label = XmStringCreateLocalized(
        const_cast<char *>(_name.c_str()));
    Dimension labelWidth = 0;
    Dimension labelHeight = 0;
    if (fontList)
        XmStringExtent(fontList, label, &labelWidth, &labelHeight);
    XmStringFree(label);

    const long labelLeft = static_cast<long>(_x)
        + (static_cast<long>(_width) > static_cast<long>(labelWidth)
            ? (static_cast<long>(_width) - static_cast<long>(labelWidth)) / 2
            : 0);
    const long labelTop = static_cast<long>(_y) + iconExtent + 3;

    return px >= labelLeft &&
           px < labelLeft + static_cast<long>(labelWidth) &&
           py >= labelTop &&
           py < labelTop + static_cast<long>(labelHeight);
}

void UserView::drawPortableContents(
    Widget panelWidget,
    XmFontList fontList) const
{
    if (!panelWidget || !XtIsRealized(panelWidget))
        return;

    const Dimension iconExtent = 50;
    const Dimension invertedTeeMargin = 3;

    /*
     * ImdInvertedTeeRenderer::getIconX() at 0x096b5b70 centers the normal
     * 50-pixel icon extent inside the view.
     *
     * Rendering architecture cross-check:
     * ImdIconRenderer::drawIcons() at 0x09679b54 operates on icon objects held
     * by ImdIconView. It configures ImdFtrIcon scale/offset/state and invokes
     * the icon object's virtual draw path. libimdFtrIcon.so provides
     * ImdFtrIconX::drawIcon(), getPixmap(), getMask(), cache and colormap
     * machinery, plus FtrImageIconLib. Therefore FtiVectorIcon below is only
     * a portable visual test backend for the preserved Users.Token.fti asset,
     * not a reconstruction of the original runtime renderer.
     */
    const int localIconX = _width > iconExtent
        ? static_cast<int>((_width - iconExtent) / 2)
        : 0;

    FtiVectorIcon &icon = portableUserTokenIcon();
    if (icon.loaded())
    {
        icon.draw(
            panelWidget,
            static_cast<int>(_x) + localIconX,
            static_cast<int>(_y),
            iconExtent);
    }

    XmString label = XmStringCreateLocalized(
        const_cast<char *>(_name.c_str()));
    Dimension labelWidth = 0;
    Dimension labelHeight = 0;
    if (fontList)
        XmStringExtent(fontList, label, &labelWidth, &labelHeight);

    Pixel foreground = 0;
    XtVaGetValues(panelWidget, XmNforeground, &foreground, NULL);

    Display *display = XtDisplay(panelWidget);
    Window window = XtWindow(panelWidget);
    XGCValues values;
    values.foreground = foreground;
    GC gc = XCreateGC(display, window, GCForeground, &values);

    const Position labelX = static_cast<Position>(
        _x + (_width > labelWidth ? (_width - labelWidth) / 2 : 0));
    const Position labelY = static_cast<Position>(
        _y + iconExtent + invertedTeeMargin);

    if (fontList)
    {
        XmStringDraw(
            display,
            window,
            fontList,
            label,
            gc,
            labelX,
            labelY,
            _width,
            XmALIGNMENT_BEGINNING,
            XmSTRING_DIRECTION_L_TO_R,
            NULL);
    }

    XFreeGC(display, gc);
    XmStringFree(label);
}
