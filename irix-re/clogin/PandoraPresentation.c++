#include "PandoraPresentation.h"

#include <Xm/BulletinB.h>
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
    : _panelWidget(panelWidget), _container(NULL)
{
    /*
     * Portable backing only. The original PandoraLayout directly constructs
     * ImdLayout(Widget), installs its own vtable, then initializes:
     *
     *   +0x0c = 10   outer inset
     *   +0x10 = 12   inter-cell spacing
     *
     * PandoraLayout::update(bool) at 0x10018028 explicitly positions each
     * view. XmBulletinBoard is therefore a closer portability boundary than
     * XmRowColumn because it lets the recovered algorithm own child x/y.
     */
    _container = XtVaCreateManagedWidget(
        "userViewContainer",
        xmBulletinBoardWidgetClass,
        _panelWidget,
        XmNmarginWidth, 0,
        XmNmarginHeight, 0,
        XmNshadowThickness, 0,
        XmNresizePolicy, XmRESIZE_NONE,
        NULL);
}

void PandoraLayout::addView(UserView *view)
{
    if (view)
        _views.push_back(view);
}

void PandoraLayout::update()
{
    if (!_container || !_panelWidget || _views.empty())
        return;

    const Dimension outerInset = 10;
    const Dimension spacing = 12;

    Dimension panelWidth = 0;
    XtVaGetValues(_panelWidget, XmNwidth, &panelWidth, NULL);
    if (panelWidth == 0)
        XtVaGetValues(_container, XmNwidth, &panelWidth, NULL);
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
     * The original computes panel height from the final occupied row while
     * keeping the same outer inset at the bottom.
     */
    Dimension contentHeight;
    if (column == 0)
        contentHeight = static_cast<Dimension>(cellY - spacing + outerInset);
    else
        contentHeight = static_cast<Dimension>(
            cellY + static_cast<Position>(maxHeight + outerInset));

    XtVaSetValues(
        _container,
        XmNwidth, panelWidth,
        XmNheight, contentHeight,
        NULL);
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
