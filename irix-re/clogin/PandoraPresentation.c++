#include "PandoraPresentation.h"

#include <Xm/DrawnB.h>
#include <Xm/RowColumn.h>
#include <Xm/Xm.h>

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
     * Portable backing only. The original PandoraLayout is an Imd layout
     * object adopted directly by ImdViewPanel; it does not create this Motif
     * RowColumn. Keeping the container here removes that Linux-only detail
     * from PandoraComponent::displayIcon().
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
     * PandoraLayout/ImdLayout geometry algorithm remains an RE target.
     */
    if (_container)
        XtVaSetValues(_container, XmNnumColumns, 4, NULL);
}

UserView::UserView(const std::string &name, long arg0, long arg1)
    : _name(name),
      _home(),
      _uid(0),
      _hasAccountMetadata(false),
      _arg0(arg0),
      _arg1(arg1),
      _widget(NULL)
{
}

void UserView::setAccountMetadata(const std::string &home, uid_t uid)
{
    _home = home;
    _uid = uid;
    _hasAccountMetadata = true;
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
     * by PandoraRenderer/ImdIconRenderer into the ImdViewPanel. A flat
     * DrawnButton avoids presenting the temporary backend as a normal Motif
     * push-button list while retaining selectable per-view widgets on Linux.
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
