#include "DragManager.h"

#include "ImdIconPanel.h"

#include <X11/Xlib.h>

ImdDragManager::ImdDragManager(ImdIconPanel *panel)
    : _panel(panel),
      _widget(0),
      _dragView(0),
      _pointerOffsetX(0),
      _pointerOffsetY(0),
      _buttonDown(false),
      _dragging(false)
{
    /*
     * Original constructor @ 0x096809b8 stores the owning ImdIconPanel at
     * original N32 +0x40 and initializes the manager state before registering
     * its Motif DnD machinery.
     */
}

ImdDragManager::~ImdDragManager()
{
    if (_widget)
        XtRemoveEventHandler(_widget,
                             ButtonPressMask | ButtonReleaseMask |
                             Button1MotionMask,
                             False,
                             inputHandler,
                             this);
}

void
ImdDragManager::init(Widget widget)
{
    if (!widget || _widget == widget)
        return;

    if (_widget)
        XtRemoveEventHandler(_widget,
                             ButtonPressMask | ButtonReleaseMask |
                             Button1MotionMask,
                             False,
                             inputHandler,
                             this);

    _widget = widget;

    /*
     * Original init() @ 0x09681034 installs the Motif drop-site and drag-shell
     * machinery. The complete XmDragStart/dropProc transfer path is still
     * under reconstruction. For local same-panel desktop movement, use Xt's
     * event transport only at this boundary; all hit testing and movement go
     * through the recovered ViewPanel APIs below.
     */
    XtAddEventHandler(_widget,
                      ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
                      False,
                      inputHandler,
                      this);
}

void
ImdDragManager::dragStart(int item, long x, long y, XEvent *event)
{
    (void)item;
    (void)event;

    if (!_widget)
        return;

    _dragView = ImdViewPanelFindViewByLocation(_widget, x, y);
    if (!_dragView)
        return;

    _pointerOffsetX = x - _dragView->portableX();
    _pointerOffsetY = y - _dragView->portableY();
    _dragging = true;
}

void
ImdDragManager::dragFinished()
{
    _dragView = 0;
    _pointerOffsetX = 0;
    _pointerOffsetY = 0;
    _dragging = false;
}

void
ImdDragManager::inputHandler(Widget,
                             XtPointer clientData,
                             XEvent *event,
                             Boolean *)
{
    ImdDragManager *manager = static_cast<ImdDragManager *>(clientData);
    if (!manager || !event)
        return;

    switch (event->type) {
    case ButtonPress:
        manager->buttonPress(&event->xbutton);
        break;
    case MotionNotify:
        manager->buttonMotion(&event->xmotion);
        break;
    case ButtonRelease:
        manager->buttonRelease(&event->xbutton);
        break;
    default:
        break;
    }
}

void
ImdDragManager::buttonPress(XButtonEvent *event)
{
    if (!event || event->button != Button1 || !_panel)
        return;

    _buttonDown = true;

    /*
     * Preserve the original panel callback boundary. The IRIX ViewPanel's
     * dragStartCallback eventually enters ImdIconPanel::dragStart(int,long,
     * long,XEvent *), which then delegates to ImdDragManager::dragStart().
     */
    _panel->dragStart(1, event->x, event->y,
                      reinterpret_cast<XEvent *>(event));
}

void
ImdDragManager::buttonMotion(XMotionEvent *event)
{
    if (!event || !_buttonDown || !_dragging || !_dragView || !_panel)
        return;

    if ((event->state & Button1Mask) == 0)
        return;

    /*
     * ImdIconPanel::dragMotion(ImdView *) is an original virtual hook used by
     * the DnD path. A false result rejects the motion target.
     */
    if (!_panel->dragMotion(_dragView))
        return;

    const long x = static_cast<long>(event->x) - _pointerOffsetX;
    const long y = static_cast<long>(event->y) - _pointerOffsetY;
    ImdViewPanelMoveView(_widget, *_dragView, x, y);
}

void
ImdDragManager::buttonRelease(XButtonEvent *event)
{
    if (!event || event->button != Button1)
        return;

    const bool hadDrag = _dragging;
    _buttonDown = false;

    if (hadDrag && _panel)
        _panel->dragEnd();

    dragFinished();
}
