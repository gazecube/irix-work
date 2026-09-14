#ifndef IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H
#define IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H

#include <X11/Intrinsic.h>

class ImdIconPanel;
class ImdView;
class ImdIconView;

/*
 * ImdDragManager
 *
 * Original class/method names are recovered from libimdIconPanel.so.
 * Original N32 allocation size is 124 bytes. This host implementation does
 * not claim that ABI layout.
 *
 * The original IRIX path owns a dedicated drag representation through
 * setupDrag(), drawDraggedItems(), shapeDragWindow(), dragMotion(), and
 * dragFinished(). The host implementation below mirrors that separation for
 * same-panel movement: the real ImdView remains stationary during pointer
 * motion and is committed once when the drag finishes.
 */
class ImdDragManager
{
public:
    explicit ImdDragManager(ImdIconPanel *panel);
    ~ImdDragManager();

    void init(Widget widget);
    void dragStart(int item, long x, long y, XEvent *event);
    void dragFinished();

private:
    static void inputHandler(Widget, XtPointer, XEvent *, Boolean *);

    void buttonPress(XButtonEvent *event);
    void pointerMotion(XMotionEvent *event);
    void buttonMotion(XMotionEvent *event);
    void buttonRelease(XButtonEvent *event);
    void panelEnter(XCrossingEvent *event);
    void panelLeave(XCrossingEvent *event);
    void setDropTarget(ImdIconView *view);

    /* Binary-named drag phases recovered from the original N32 library. */
    bool setupDrag(long x, long y);
    bool drawDraggedItems();
    void moveDragWindow(long x, long y);
    void destroyDragWindow();

    ImdIconPanel *_panel;
    Widget _widget;
    ImdView *_dragView;
    ImdIconView *_hoverView;
    ImdIconView *_dropTargetView;
    long _pointerOffsetX;
    long _pointerOffsetY;
    long _pendingX;
    long _pendingY;
    Window _dragWindow;
    Pixmap _dragPixmap;
    unsigned int _dragWidth;
    unsigned int _dragHeight;
    bool _buttonDown;
    bool _dragging;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H */
