#include "DragManager.h"

#include "ImdIconPanel.h"

#include <X11/StringDefs.h>
#include <X11/Xlib.h>

namespace
{
static void
setPortableDragSourceState(ImdIconPanel *panel,
                           Widget widget,
                           ImdView *fallbackView,
                           bool active)
{
    if (!panel || !widget)
        return;

    const std::vector<ImdIconView *> &selected = panel->portableSelectedViews();
    bool changedAny = false;

    for (std::vector<ImdIconView *>::const_iterator it = selected.begin();
         it != selected.end(); ++it) {
        ImdIconView *view = *it;
        if (!view)
            continue;

        /*
         * Binary-proven IRIX drag state:
         *   ImdDragManager::dragStart()
         *     view->setFlag(2, true)
         *     ImdViewPanelDrawView(...)
         * and drop/finish performs the inverse with false.
         *
         * The original enum spelling for state value 2 has not been recovered,
         * so keep the numeric value rather than inventing a name.
         */
        view->setFlag(2u, active);

        /*
         * Host renderer seam only. The original ImdFtrIcon renderer consumes
         * icon state 2 directly. Our portable FTR renderer does not yet do so,
         * therefore suppress normal Expose redraw participation while the drag
         * is active. Remove this once ImdFtrIcon::setIconState() semantics are
         * reconstructed in the host renderer.
         */
        view->portableSetVisible(!active);

        if (active && XtIsRealized(widget)) {
            XClearArea(XtDisplay(widget), XtWindow(widget),
                       static_cast<int>(view->portableX()),
                       static_cast<int>(view->portableY()),
                       static_cast<unsigned int>(view->portableWidth()),
                       static_cast<unsigned int>(view->portableHeight()),
                       False);
        }
        changedAny = true;
    }

    /*
     * Normal IRIX dragging operates on the selected-view collection. Keep a
     * defensive host fallback for input-order cases where the drag callback
     * reaches us before the selection callback has populated that collection.
     */
    if (!changedAny && fallbackView) {
        ImdIconView *view = dynamic_cast<ImdIconView *>(fallbackView);
        if (view) {
            view->setFlag(2u, active);
            view->portableSetVisible(!active);
            if (active && XtIsRealized(widget)) {
                XClearArea(XtDisplay(widget), XtWindow(widget),
                           static_cast<int>(view->portableX()),
                           static_cast<int>(view->portableY()),
                           static_cast<unsigned int>(view->portableWidth()),
                           static_cast<unsigned int>(view->portableHeight()),
                           False);
            }
            changedAny = true;
        }
    }

    if (!active && changedAny)
        ImdViewPanelRedraw(widget);
}
}

ImdDragManager::ImdDragManager(ImdIconPanel *panel)
    : _panel(panel),
      _widget(0),
      _dragView(0),
      _hoverView(0),
      _dropTargetView(0),
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
    if (_dropTargetView)
        _dropTargetView->setFlag(0x20u, false);
    if (_hoverView)
        _hoverView->setFlag(0x04u, false);

    if (_dragging)
        setPortableDragSourceState(_panel, _widget, _dragView, false);

    destroyDragWindow();

    if (_widget)
        XtRemoveEventHandler(_widget,
                             ButtonPressMask | ButtonReleaseMask |
                             PointerMotionMask | EnterWindowMask |
                             LeaveWindowMask,
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
                             PointerMotionMask | EnterWindowMask |
                             LeaveWindowMask,
                             False,
                             inputHandler,
                             this);

    _widget = widget;
    _hoverView = 0;
    _dropTargetView = 0;

    /*
     * Original ViewPanel routes pointer entry/exit to ImdIconPanel::enter(),
     * leave(), enterPanel(), and leavePanel(), while Motif DnD callbacks own
     * drop-target transitions. Until that complete callback-object layer is
     * restored, one Xt handler transports those same binary-proven states.
     */
    XtAddEventHandler(_widget,
                      ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | EnterWindowMask | LeaveWindowMask,
                      False,
                      inputHandler,
                      this);
}

void
ImdDragManager::purgeDeleted(ImdView *view)
{
    if (!view)
        return;

    /*
     * Binary anchor: purgeDeleted__14ImdDragManagerSGP10_WidgetRecPvT2
     * @ 0x09680b9c examines the manager's two retained view pointers at
     * original N32 +0x28 and +0x3c. If the referenced view carries the
     * deleted-state bits in ImdView +0x14, the corresponding manager slot is
     * written to zero.
     *
     * The host transport does not claim the original object layout, but its
     * equivalent retained non-owning view references are _hoverView and
     * _dropTargetView. Purge them while the view is still alive, before the
     * panel releases the object. This preserves the SGI lifetime rule and
     * prevents a later MotionNotify from passing a freed view to leave().
     */
    if (_hoverView == view)
        _hoverView = 0;
    if (_dropTargetView == view)
        _dropTargetView = 0;
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

    Pixel background = WhitePixelOfScreen(XtScreen(_widget));
    XtVaGetValues(_widget, XtNbackground, &background, NULL);
    XSetForeground(display, gc, background);
    XFillRectangle(display, _dragPixmap, gc,
                   0, 0, _dragWidth, _dragHeight);
    XFreeGC(display, gc);

    /*
     * Original drawDraggedItems(Widget,XRectangle&) @ 0x0969577c walks the
     * selected views and renders them through the drag renderer. It does not
     * screenshot a rectangular area of the live ViewPanel.
     */
    ImdInvertedTeeRenderer dragRenderer;
    dragRenderer.setScale(ImdViewPanelGetScale(_widget));
    dragRenderer.realize(_widget);
    if (!dragRenderer.portableDrawToDrawable(*_dragView,
                                             _dragPixmap,
                                             0,
                                             0)) {
        XFreePixmap(display, _dragPixmap);
        _dragPixmap = None;
        _dragWidth = 0;
        _dragHeight = 0;
        return false;
    }

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
     * still pending, so this host implementation intentionally leaves the
     * renderer-composed representation rectangular for now.
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
ImdDragManager::setDropTarget(ImdIconView *view)
{
    if (_dropTargetView == view)
        return;

    /*
     * Binary-proven DnD target state: dropSiteEnter()/dragMotion() set icon
     * state 0x20 on the accepted target, while dropSiteLeave()/dropProc() clear
     * it from the previous target. State 0x20 is part of FTR's 0x2e special
     * border mask.
     */
    if (_dropTargetView)
        _dropTargetView->setFlag(0x20u, false);
    _dropTargetView = view;
    if (_dropTargetView)
        _dropTargetView->setFlag(0x20u, true);

    if (_widget)
        ImdViewPanelRedraw(_widget);
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

    setPortableDragSourceState(_panel, _widget, _dragView, true);
    _dragging = true;
}

void
ImdDragManager::dragFinished()
{
    setDropTarget(0);

    if (_dragging)
        setPortableDragSourceState(_panel, _widget, _dragView, false);

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
        manager->pointerMotion(&event->xmotion);
        manager->buttonMotion(&event->xmotion);
        break;
    case ButtonRelease:
        manager->buttonRelease(&event->xbutton);
        break;
    case EnterNotify:
        manager->panelEnter(&event->xcrossing);
        break;
    case LeaveNotify:
        manager->panelLeave(&event->xcrossing);
        break;
    default:
        break;
    }
}

void
ImdDragManager::panelEnter(XCrossingEvent *event)
{
    (void)event;
    if (_panel)
        _panel->enterPanel();
}

void
ImdDragManager::panelLeave(XCrossingEvent *event)
{
    (void)event;
    if (_hoverView && _panel)
        _panel->leave(_hoverView);
    _hoverView = 0;
    setDropTarget(0);
    if (_panel)
        _panel->leavePanel();
}

void
ImdDragManager::pointerMotion(XMotionEvent *event)
{
    if (!event || !_widget || !_panel || _dragging)
        return;

    ImdIconView *hit = dynamic_cast<ImdIconView *>(
        ImdViewPanelFindViewByLocation(_widget, event->x, event->y));
    if (hit == _hoverView)
        return;

    if (_hoverView)
        _panel->leave(_hoverView);
    _hoverView = hit;
    if (_hoverView)
        _panel->enter(_hoverView);
}

void
ImdDragManager::buttonPress(XButtonEvent *event)
{
    if (!event || event->button != Button1 || !_panel || !_widget)
        return;

    _buttonDown = true;

    /*
     * Binary-backed ViewPanel translations:
     *
     *   <Btn1Down>: ... Select() ... MouseClick() ...
     *   <Btn1Motion>: ... DragStart(Move)
     *
     * MouseClick() stores the press coordinates, and DragStart() reuses those
     * coordinates when motion begins.
     */
    _pendingX = static_cast<long>(event->x);
    _pendingY = static_cast<long>(event->y);

    ImdView *hit = ImdViewPanelFindViewByLocation(_widget,
                                                  _pendingX,
                                                  _pendingY);
    ImdIconView *iconView = dynamic_cast<ImdIconView *>(hit);
    if (iconView)
        _panel->select(iconView);
    else
        _panel->deselect();
}

void
ImdDragManager::buttonMotion(XMotionEvent *event)
{
    if (!event || !_buttonDown || !_panel)
        return;

    if ((event->state & Button1Mask) == 0)
        return;

    /*
     * IRIX does not begin a Button1 drag on ButtonPress. The ViewPanel invokes
     * DragStart(Move) from its <Btn1Motion> translation and seeds it with the
     * coordinates previously captured by MouseClick().
     */
    if (!_dragging) {
        const long pressX = _pendingX;
        const long pressY = _pendingY;
        _panel->dragStart(1,
                          pressX,
                          pressY,
                          reinterpret_cast<XEvent *>(event));
        if (!_dragging || !_dragView)
            return;
    }

    /* Track the binary-proven state-0x20 target independently from the source. */
    ImdIconView *target = dynamic_cast<ImdIconView *>(
        ImdViewPanelFindViewByLocation(_widget, event->x, event->y));
    setDropTarget(target);

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
    setDropTarget(0);

    /* Remove the transient representation before the real ViewPanel redraw. */
    destroyDragWindow();

    /*
     * Commit the real layout move once at drag completion. Keep the source
     * suppressed while the layout changes; dragFinished() then clears the
     * binary-proven state-2 flag and redraws the source at its final position.
     */
    if (hadDrag && dragView)
        ImdViewPanelMoveView(_widget, *dragView, finalX, finalY);

    if (hadDrag && _panel)
        _panel->dragEnd();

    dragFinished();
}
