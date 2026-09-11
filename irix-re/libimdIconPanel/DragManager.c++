#include "DragManager.h"

#include "ImdIconPanel.h"

#include <X11/Xlib.h>

ImdDragManager::ImdDragManager(ImdIconPanel *panel)
    : _panel(panel),
      _widget(0),
      _dragView(0),
      _pointerOffsetX(0),
      _pointerOffsetY(0),
      _pendingX(0),
      _pendingY(0),
      _dragWindow(None),
      _dragPixmap(None),
      _dragWidth(0),
      _dragHeight(0),
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
    destroyDragWindow();

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
     * machinery. Until the complete XmDragStart/dropProc transfer path is
     * restored, Xt still transports local input events at this boundary.
     */
    XtAddEventHandler(_widget,
                      ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
                      False,
                      inputHandler,
                      this);
}

bool
ImdDragManager::drawDraggedItems()
{
    if (!_widget || !_dragView || !XtIsRealized(_widget))
        return false;

    Display *display = XtDisplay(_widget);
    Window source = XtWindow(_widget);
    if (!display || source == None)
        return false;

    long width = _dragView->portableWidth();
    long height = _dragView->portableHeight();
    if (width <= 0 || height <= 0)
        return false;

    _dragWidth = static_cast<unsigned int>(width);
    _dragHeight = static_cast<unsigned int>(height);

    XWindowAttributes sourceAttributes;
    if (!XGetWindowAttributes(display, source, &sourceAttributes))
        return false;

    _dragPixmap = XCreatePixmap(display,
                                source,
                                _dragWidth,
                                _dragHeight,
                                static_cast<unsigned int>(sourceAttributes.depth));
    if (_dragPixmap == None)
        return false;

    GC gc = XCreateGC(display, _dragPixmap, 0, 0);
    if (!gc) {
        XFreePixmap(display, _dragPixmap);
        _dragPixmap = None;
        return false;
    }

    /*
     * Original drawDraggedItems(Widget,XRectangle&) @ 0x0969577c renders the
     * selected drag items into the dedicated drag representation. This host
     * seam snapshots the already-rendered ImdView rectangle once; importantly,
     * pointer motion no longer redraws or moves the real ViewPanel contents.
     */
    XCopyArea(display,
              source,
              _dragPixmap,
              gc,
              static_cast<int>(_dragView->portableX()),
              static_cast<int>(_dragView->portableY()),
              _dragWidth,
              _dragHeight,
              0,
              0);
    XFreeGC(display, gc);

    return true;
}

bool
ImdDragManager::setupDrag(long x, long y)
{
    if (!_widget || !_dragView || !XtIsRealized(_widget))
        return false;

    destroyDragWindow();

    if (!drawDraggedItems())
        return false;

    Display *display = XtDisplay(_widget);
    Window root = RootWindowOfScreen(XtScreen(_widget));
    Window source = XtWindow(_widget);

    XWindowAttributes sourceAttributes;
    if (!XGetWindowAttributes(display, source, &sourceAttributes)) {
        destroyDragWindow();
        return false;
    }

    Position rootX = 0;
    Position rootY = 0;
    XtTranslateCoords(_widget,
                      static_cast<Position>(x),
                      static_cast<Position>(y),
                      &rootX,
                      &rootY);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.save_under = True;
    attrs.background_pixmap = _dragPixmap;
    attrs.colormap = sourceAttributes.colormap;

    _dragWindow = XCreateWindow(display,
                                root,
                                static_cast<int>(rootX),
                                static_cast<int>(rootY),
                                _dragWidth,
                                _dragHeight,
                                0,
                                sourceAttributes.depth,
                                InputOutput,
                                sourceAttributes.visual,
                                CWOverrideRedirect | CWSaveUnder |
                                CWBackPixmap | CWColormap,
                                &attrs);
    if (_dragWindow == None) {
        destroyDragWindow();
        return false;
    }

    /*
     * Original setupDrag() @ 0x09695984 builds the SGI/Motif drag shell and
     * then shapeDragWindow() applies the item mask. The exact N32 mask path is
     * still pending, so this first host implementation intentionally leaves the
     * snapshot rectangular rather than inventing a shape algorithm.
     */
    XMapRaised(display, _dragWindow);
    XFlush(display);
    return true;
}

void
ImdDragManager::moveDragWindow(long x, long y)
{
    if (!_widget || _dragWindow == None)
        return;

    Position rootX = 0;
    Position rootY = 0;
    XtTranslateCoords(_widget,
                      static_cast<Position>(x),
                      static_cast<Position>(y),
                      &rootX,
                      &rootY);

    XMoveWindow(XtDisplay(_widget),
                _dragWindow,
                static_cast<int>(rootX),
                static_cast<int>(rootY));
    XFlush(XtDisplay(_widget));
}

void
ImdDragManager::destroyDragWindow()
{
    if (!_widget)
        return;

    Display *display = XtDisplay(_widget);
    if (!display)
        return;

    if (_dragWindow != None) {
        XDestroyWindow(display, _dragWindow);
        _dragWindow = None;
    }

    if (_dragPixmap != None) {
        XFreePixmap(display, _dragPixmap);
        _dragPixmap = None;
    }

    _dragWidth = 0;
    _dragHeight = 0;
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
    _pendingX = _dragView->portableX();
    _pendingY = _dragView->portableY();

    /*
     * Binary-proven architecture: setupDrag()/drawDraggedItems() create a
     * separate drag representation. Do not move the real ImdView here.
     */
    if (!setupDrag(_pendingX, _pendingY)) {
        _dragView = 0;
        return;
    }

    _dragging = true;
}

void
ImdDragManager::dragFinished()
{
    destroyDragWindow();

    _dragView = 0;
    _pointerOffsetX = 0;
    _pointerOffsetY = 0;
    _pendingX = 0;
    _pendingY = 0;
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
     * long,XEvent *), which delegates to ImdDragManager::dragStart().
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
     * ImdIconPanel::dragMotion(ImdView *) is the original panel virtual hook.
     * Keep it in the motion path, but move only the dedicated drag window.
     */
    if (!_panel->dragMotion(_dragView))
        return;

    _pendingX = static_cast<long>(event->x) - _pointerOffsetX;
    _pendingY = static_cast<long>(event->y) - _pointerOffsetY;
    moveDragWindow(_pendingX, _pendingY);
}

void
ImdDragManager::buttonRelease(XButtonEvent *event)
{
    if (!event || event->button != Button1)
        return;

    const bool hadDrag = _dragging && _dragView;
    ImdView *dragView = _dragView;
    const long finalX = static_cast<long>(event->x) - _pointerOffsetX;
    const long finalY = static_cast<long>(event->y) - _pointerOffsetY;

    _buttonDown = false;

    /* Remove the transient representation before the real ViewPanel redraw. */
    destroyDragWindow();

    /*
     * Commit the real layout move once at drag completion. This preserves the
     * original separation between drag-window motion and ViewPanel movement
     * while the full Motif dropProc/transferProc path is reconstructed.
     */
    if (hadDrag && dragView)
        ImdViewPanelMoveView(_widget, *dragView, finalX, finalY);

    if (hadDrag && _panel)
        _panel->dragEnd();

    dragFinished();
}
