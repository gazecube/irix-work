#include "PandoraPresentation.h"
#include "FtiVectorIcon.h"
#include "Icon/UserPictureResolver.h"
#include "Base/PwdNameList.h"
#include "Filesystem/DirectoryCheck.h"

#include <Xm/DrawingA.h>
#include <Xm/Label.h>
#include <Xm/Xm.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>

#include <pwd.h>
#include <unistd.h>

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

std::string resolvedUserPicturePath(
    const std::string &name,
    const std::string &home)
{
    struct passwd *pwd = getpwnam(name.c_str());
    if (!pwd)
        return std::string();

    const char *configPath = "/var/sysadm/config/clogin.conf";
    const bool haveIrixConfig = access(configPath, R_OK) == 0;
    PwdNameList config(haveIrixConfig ? configPath : NULL);

    UserPicturePolicy policy;
    policy.howDisplay = config.displayFaces() ? 0 : 1;
    policy.whereIcons = config.lookForPicturesLocally() ? 0 : 1;

    const int directoryState = static_cast<int>(
        clogin::checkDirectoryPortable(home));
    const UserPictureResult result = resolveUserPicture(
        *pwd,
        config,
        policy,
        directoryState);

    return result.found ? result.path : std::string();
}

bool drawPixmapFile(
    Widget widget,
    const std::string &path,
    int x,
    int y,
    unsigned int extent)
{
    if (!widget || !XtIsRealized(widget) || path.empty())
        return false;

    Display *display = XtDisplay(widget);
    Window window = XtWindow(widget);
    if (!display || !window)
        return false;

    Pixmap pixmap = None;
    Pixmap mask = None;

    if (XpmReadFileToPixmap(
            display,
            window,
            const_cast<char *>(path.c_str()),
            &pixmap,
            &mask,
            NULL) == XpmSuccess && pixmap != None)
    {
        Window root;
        int sourceX;
        int sourceY;
        unsigned int width;
        unsigned int height;
        unsigned int border;
        unsigned int depth;

        if (XGetGeometry(
                display,
                pixmap,
                &root,
                &sourceX,
                &sourceY,
                &width,
                &height,
                &border,
                &depth))
        {
            const unsigned int copyWidth = width < extent ? width : extent;
            const unsigned int copyHeight = height < extent ? height : extent;
            const int drawX = x + static_cast<int>((extent - copyWidth) / 2);
            const int drawY = y + static_cast<int>((extent - copyHeight) / 2);

            GC gc = XCreateGC(display, window, 0, NULL);
            if (mask != None)
            {
                XSetClipMask(display, gc, mask);
                XSetClipOrigin(display, gc, drawX, drawY);
            }
            XCopyArea(
                display,
                pixmap,
                window,
                gc,
                0,
                0,
                copyWidth,
                copyHeight,
                drawX,
                drawY);
            XFreeGC(display, gc);
            XFreePixmap(display, pixmap);
            if (mask != None)
                XFreePixmap(display, mask);
            return true;
        }

        XFreePixmap(display, pixmap);
        if (mask != None)
            XFreePixmap(display, mask);
    }

    unsigned int bitmapWidth = 0;
    unsigned int bitmapHeight = 0;
    int hotX = 0;
    int hotY = 0;
    if (XReadBitmapFile(
            display,
            window,
            path.c_str(),
            &bitmapWidth,
            &bitmapHeight,
            &pixmap,
            &hotX,
            &hotY) == BitmapSuccess && pixmap != None)
    {
        const unsigned int copyWidth = bitmapWidth < extent ? bitmapWidth : extent;
        const unsigned int copyHeight = bitmapHeight < extent ? bitmapHeight : extent;
        const int drawX = x + static_cast<int>((extent - copyWidth) / 2);
        const int drawY = y + static_cast<int>((extent - copyHeight) / 2);

        Pixel foreground = 0;
        Pixel background = 0;
        XtVaGetValues(
            widget,
            XmNforeground, &foreground,
            XmNbackground, &background,
            NULL);

        XGCValues values;
        values.foreground = foreground;
        values.background = background;
        GC gc = XCreateGC(
            display,
            window,
            GCForeground | GCBackground,
            &values);
        XCopyPlane(
            display,
            pixmap,
            window,
            gc,
            0,
            0,
            copyWidth,
            copyHeight,
            drawX,
            drawY,
            1);
        XFreeGC(display, gc);
        XFreePixmap(display, pixmap);
        return true;
    }

    return false;
}

bool drawResolvedUserPicture(
    Widget widget,
    const std::string &name,
    const std::string &home,
    int x,
    int y,
    unsigned int extent)
{
    const std::string path = resolvedUserPicturePath(name, home);
    if (path.empty())
        return false;

    /*
     * Portable rendering bridge only. The original UserView uses the Imd/Ftr
     * image/icon machinery after findUserPicture() returns an open descriptor.
     * Preserve the binary-proven lookup decision above; then try the portable
     * decoders currently available in this cleanroom tree.
     */
    FtiVectorIcon vectorIcon;
    if (vectorIcon.load(path) && vectorIcon.loaded())
    {
        vectorIcon.draw(widget, x, y, extent);
        return true;
    }

    return drawPixmapFile(widget, path, x, y, extent);
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
     * machinery, plus FtrImageIconLib. The decoder calls here are therefore a
     * portable visual bridge after the binary-backed findUserPicture() lookup,
     * not a reconstruction of the original image-rendering ABI.
     */
    const int localIconX = _width > iconExtent
        ? static_cast<int>((_width - iconExtent) / 2)
        : 0;
    const int iconX = static_cast<int>(_x) + localIconX;
    const int iconY = static_cast<int>(_y);

    bool pictureDrawn = drawResolvedUserPicture(
        panelWidget,
        _name,
        _home,
        iconX,
        iconY,
        iconExtent);

    if (!pictureDrawn)
    {
        FtiVectorIcon &icon = portableUserTokenIcon();
        if (icon.loaded())
            icon.draw(panelWidget, iconX, iconY, iconExtent);
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
