#ifndef IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H
#define IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H

#include <X11/Intrinsic.h>

class ImdIconPanel;
class ImdView;

/*
 * ImdDragManager
 *
 * Original class/method names are recovered from libimdIconPanel.so.
 * Original N32 allocation size is 124 bytes. This host implementation does
 * not claim that ABI layout.
 *
 * The IRIX implementation transports drags through Motif DnD (XmDragStart,
 * dropProc, dragProc, transferProc, SgDragShell, etc.). Until that transport
 * is reconstructed completely, the host event handler below supplies local
 * same-panel motion while preserving the recovered panel/manager/ViewPanel
 * method boundaries.
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
    void buttonMotion(XMotionEvent *event);
    void buttonRelease(XButtonEvent *event);

    ImdIconPanel *_panel;
    Widget _widget;
    ImdView *_dragView;
    long _pointerOffsetX;
    long _pointerOffsetY;
    bool _buttonDown;
    bool _dragging;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_DRAGMANAGER_H */
