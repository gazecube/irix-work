#ifndef IRIX_RE_FM_FMICONSURFACE_H
#define IRIX_RE_FM_FMICONSURFACE_H

#include "ImdIconPanel.h"

#include <X11/Intrinsic.h>
#include <stddef.h>
#include <string>
#include <vector>

struct FmIconSurfaceEntry
{
    std::string name;
    bool directory;
};

typedef void (*FmIconSurfaceActivateProc)(size_t index, void *closure);

class FmIconSurface
{
public:
    FmIconSurface(ImdIconPanel *panel, Widget widget);
    ~FmIconSurface();

    void setEntries(const std::vector<FmIconSurfaceEntry> &entries);
    bool selectedIndex(size_t *index) const;
    void selectAll();
    void redraw();

    void setActivateProc(FmIconSurfaceActivateProc proc, void *closure);

private:
    class View;

    static void eventHandler(Widget, XtPointer, XEvent *, Boolean *);
    static void exposeCB(Widget, XtPointer, XtPointer);

    void handleEvent(XEvent *event);
    void draw();
    int indexAtY(int y) const;

    ImdIconPanel *_panel;
    Widget _widget;
    std::vector<FmIconSurfaceEntry> _entries;
    std::vector<bool> _selected;
    FmIconSurfaceActivateProc _activateProc;
    void *_activateClosure;
    Time _lastClickTime;
    int _lastClickIndex;
};

#endif /* IRIX_RE_FM_FMICONSURFACE_H */
