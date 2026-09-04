#include "PandoraPresentation.h"

#include <Xm/DrawnB.h>
#include <Xm/Xm.h>

#include <pwd.h>

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
    : _panelWidget(panelWidget), _container(panelWidget)
{
    /*
     * Binary-proven PandoraLayout directly constructs ImdLayout(Widget),
     * installs its own vtable, then initializes:
     *
     *   +0x0c = 10   outer inset
     *   +0x10 = 12   inter-cell spacing
     *
     * The original layout positions views directly in the Imd panel. The
     * portable probe now does the same against the DrawingArea-backed panel;
     * no intermediate Motif RowColumn/BulletinBoard owns the geometry.
     *
     * PandoraLayout::resize() at 0x10018408 loads the panel widget from the
     * ImdLayout base and calls ImdViewPanelUpdateLayout(panel). Our portable
     * Imd panel does not dispatch the full ViewPanel lifecycle yet, so both
     * ConfigureNotify and the initial MapNotify are bridged to resize(). The
     * MapNotify pass is needed because displayIcon() can run before the panel
     * has its final mapped width.
     */
    if (_panelWidget)
    {
        XtAddEventHandler(
            _panelWidget,
            StructureNotifyMask,
            False,
            &PandoraLayout::panelStructureEvent,
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
    }
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

    /*
     * ConfigureNotify mirrors the recovered PandoraLayout::resize() path.
     * MapNotify is a portable bootstrap bridge for the missing ImdViewPanel
     * show/realize lifecycle: by map time the panel has its final initial
     * width and the UserViews created by displayIcon() already exist.
     */
    if (event->type == ConfigureNotify || event->type == MapNotify)
        self->resize();
}

void PandoraLayout::addView(UserView *view)
{
    if (view)
        _views.push_back(view);
}

void PandoraLayout::resize()
{
    /*
     * Original 0x10018408 is effectively:
     *
     *   ImdViewPanelUpdateLayout(panelWidget);
     *
     * update() is the current portable equivalent of the panel/layout
     * dispatch reached by that call.
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
        Widget widget = _views[i] ? _views[i]->widget() : NULL;
        if (!widget)
            continue;

        Dimension width = 0;
        Dimension height = 0;
        XtVaGetValues(widget, XmNwidth, &width, XmNheight, &height, NULL);
        if (width > maxWidth)
            maxWidth = width;
        if (height > maxHeight)
            maxHeight = height;
    }

    if (maxWidth == 0 || maxHeight == 0)
        return;

    /*
     * Binary-proven column calculation from PandoraLayout::update(bool):
     *
     *   cols = (availableWidth - 2*outerInset + spacing)
     *          / (maxViewWidth + spacing)
     *   if (cols == 0) cols = 1
     */
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
        Widget widget = _views[i] ? _views[i]->widget() : NULL;
        if (!widget)
            continue;

        Dimension width = 0;
        XtVaGetValues(widget, XmNwidth, &width, NULL);

        /* Original centers narrower views inside the maximum-width cell. */
        Position x = static_cast<Position>(
            cellX + static_cast<Position>((maxWidth - width) / 2));

        XtVaSetValues(
            widget,
            XmNx, x,
            XmNy, cellY,
            NULL);

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

    /*
     * Preserve the visible panel height, but grow the work area if the
     * recovered grid requires more vertical space. This is a portable
     * ScrolledWindow integration detail, not an original Pandora method.
     */
    Dimension contentHeight;
    if (column == 0)
        contentHeight = static_cast<Dimension>(cellY - spacing + outerInset);
    else
        contentHeight = static_cast<Dimension>(
            cellY + static_cast<Position>(maxHeight + outerInset));

    if (contentHeight > panelHeight)
        XtVaSetValues(_panelWidget, XmNheight, contentHeight, NULL);
}

UserView::UserView(
    const std::string &name,
    long uid,
    const std::string &home)
    : _name(name),
      _home(home),
      _uid(static_cast<uid_t>(uid)),
      _widget(NULL)
{
    /*
     * Binary-proven constructor shape from 0x100179c4:
     *
     *   ImdIconView::ImdIconView()
     *   install UserView vtable
     *   store uid
     *   construct/copy login ImdString
     *   construct/copy home ImdString
     *
     * The portable C++ base constructor runs automatically here; std::string
     * substitutes for ImdString until libimdUtil's ABI is wired in.
     */
}

UserView::UserView(
    const std::string &name,
    long,
    long)
    : _name(name),
      _home(),
      _uid(0),
      _widget(NULL)
{
    /*
     * Transitional compatibility path for the pre-recovery displayIcon()
     * call site. The fresh clogin disassembly proves the real constructor is
     * (login, uid, home), so resolve those two values from passwd rather than
     * retaining the obsolete pair of anonymous long values.
     */
    struct passwd *pwd = getpwnam(_name.c_str());
    if (pwd)
    {
        _uid = pwd->pw_uid;
        if (pwd->pw_dir)
            _home = pwd->pw_dir;
    }
}

void UserView::createPortableWidget(
    Widget parent,
    XtCallbackProc activateCallback,
    XtPointer clientData)
{
    if (!parent || _widget)
        return;

    XmString label = XmStringCreateLocalized(
        const_cast<char *>(_name.c_str()));

    /*
     * Portable rendering backend only. Original UserView instances are drawn
     * through PandoraRenderer / ImdInvertedTeeRenderer in the ImdViewPanel.
     * A flat DrawnButton remains only as the temporary Linux selection surface.
     * The 128x96 portable cell size remains provisional until
     * ImdInvertedTeeRenderer::setSize(ImdView &) is reconstructed.
     */
    _widget = XtVaCreateManagedWidget(
        _name.c_str(),
        xmDrawnButtonWidgetClass,
        parent,
        XmNlabelString, label,
        XmNuserData, (XtPointer)this,
        XmNwidth, 128,
        XmNheight, 96,
        XmNrecomputeSize, False,
        XmNshadowThickness, 0,
        XmNhighlightThickness, 0,
        XmNtraversalOn, False,
        XmNpushButtonEnabled, True,
        NULL);

    XmStringFree(label);

    if (activateCallback)
        XtAddCallback(
            _widget,
            XmNactivateCallback,
            activateCallback,
            clientData);
}
