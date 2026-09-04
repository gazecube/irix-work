#include "PandoraPresentation.h"

#include <Xm/DrawnB.h>
#include <Xm/RowColumn.h>
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
     * ImdLayout(Widget), then installs its own vtable and initializes two
     * class fields to 10 and 12. The Motif RowColumn remains a Linux-only
     * placement backend until the original layout algorithm is reconstructed.
     */
    _container = XtVaCreateManagedWidget(
        "userViewContainer",
        xmRowColumnWidgetClass,
        _panelWidget,
        XmNorientation, XmHORIZONTAL,
        XmNpacking, XmPACK_COLUMN,
        XmNnumColumns, 4,
        XmNspacing, 3,
        XmNmarginWidth, 3,
        XmNmarginHeight, 3,
        NULL);
}

void PandoraLayout::addView(UserView *view)
{
    if (view)
        _views.push_back(view);
}

void PandoraLayout::update()
{
    /*
     * Xt's RowColumn performs the portable placement for now. The original
     * PandoraLayout::update(bool) at 0x10018028 remains the geometry target.
     */
    if (_container)
        XtVaSetValues(_container, XmNnumColumns, 4, NULL);
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