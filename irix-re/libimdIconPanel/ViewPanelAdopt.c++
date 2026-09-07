#include "ImdIconPanel.h"

#include <X11/Xlib.h>
#include <map>
#include <vector>

namespace
{
struct AdoptedViewRecord
{
    ImdView *view;
    ImdView *parentView;
    ImdIconPanel *panel;

    AdoptedViewRecord(ImdView *v = 0,
                      ImdView *parent = 0,
                      ImdIconPanel *p = 0)
        : view(v), parentView(parent), panel(p)
    {
    }
};

typedef std::vector<AdoptedViewRecord> AdoptedViewList;
std::map<Widget, AdoptedViewList> gAdoptedViews;
std::map<Widget, ImdRenderer *> gRenderers;
std::map<Widget, bool> gExposeInstalled;

static void
assignPortablePosition(ImdView *view, std::size_t index)
{
    if (!view)
        return;

    const long column = static_cast<long>(index % 8);
    const long row = static_cast<long>(index / 8);
    view->portableSetPosition(28 + column * 92, 28 + row * 104);

    /* Temporary renderer-facing extent until InvertedTee geometry is restored. */
    view->portableSetSize(84, 84);
    view->portableSetVisible(true);
}

static void
exposeHandler(Widget widget, XtPointer, XEvent *event, Boolean *)
{
    if (!event || event->type != Expose)
        return;
    if (event->xexpose.count != 0)
        return;
    ImdViewPanelRedraw(widget);
}
}

extern "C" void
ImdViewPanelPortableRendererChanged(Widget widget, ImdRenderer *renderer)
{
    if (!widget)
        return;

    gRenderers[widget] = renderer;
    if (!gExposeInstalled[widget]) {
        XtAddEventHandler(widget,
                          ExposureMask,
                          False,
                          exposeHandler,
                          0);
        gExposeInstalled[widget] = true;
    }
}

void
ImdViewPanelRedraw(Widget widget)
{
    if (!widget || !XtIsRealized(widget))
        return;

    std::map<Widget, ImdRenderer *>::iterator rendererIt = gRenderers.find(widget);
    if (rendererIt == gRenderers.end() || !rendererIt->second)
        return;

    ImdRenderer *renderer = rendererIt->second;
    AdoptedViewList &views = gAdoptedViews[widget];
    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        if (it->view && it->view->portableVisible())
            renderer->draw(*it->view, it->view->portableX(), it->view->portableY());
    }
}

ImdView *
ImdViewPanelFindViewByLocation(Widget widget, long x, long y)
{
    if (!widget)
        return 0;

    /*
     * Original ImdViewPanelFindViewByLocation() @ 0x09680470 tests:
     *   visible bit 31 at +0x14
     *   x/y at +0x04/+0x08
     *   width/height at +0x0c/+0x10
     * and returns the matching view.
     */
    AdoptedViewList &views = gAdoptedViews[widget];
    for (AdoptedViewList::reverse_iterator it = views.rbegin();
         it != views.rend(); ++it) {
        ImdView *view = it->view;
        if (!view || !view->portableVisible())
            continue;

        const long left = view->portableX();
        const long top = view->portableY();
        const long right = left + view->portableWidth();
        const long bottom = top + view->portableHeight();
        if (x >= left && x <= right && y >= top && y <= bottom)
            return view;
    }

    return 0;
}

bool
ImdViewPanelMoveView(Widget widget, ImdView &view, long x, long y)
{
    if (!widget)
        return false;

    /*
     * Original ImdViewPanelMoveView() @ 0x096a2f3c delegates to the adopted
     * layout's moveView(view,x,y) virtual. Preserve that recovered boundary.
     */
    ImdLayout *layout = ImdViewPanelGetLayout(widget);
    bool moved = false;

    if (layout)
        moved = layout->moveView(view, x, y);
    else {
        /* Host fallback only for a ViewPanel without an adopted layout. */
        view.portableSetPosition(x, y);
        moved = true;
    }

    if (moved && XtIsRealized(widget)) {
        /* Clear to the ViewPanel background without generating a new Expose. */
        XClearArea(XtDisplay(widget), XtWindow(widget), 0, 0, 0, 0, False);
        ImdViewPanelRedraw(widget);
    }

    return moved;
}

void
ImdViewPanelAdoptView(
    Widget widget,
    ImdView *view,
    ImdView *parentView)
{
    if (!widget || !view || !parentView)
        return;

    AdoptedViewList &views = gAdoptedViews[widget];
    assignPortablePosition(view, views.size());
    views.push_back(AdoptedViewRecord(view, parentView, 0));
}

void
ImdViewPanelAdoptView(
    Widget widget,
    ImdView *view,
    ImdIconPanel *panel)
{
    if (!widget || !view || !panel)
        return;

    AdoptedViewList &views = gAdoptedViews[widget];
    assignPortablePosition(view, views.size());
    views.push_back(AdoptedViewRecord(view, 0, panel));
}
