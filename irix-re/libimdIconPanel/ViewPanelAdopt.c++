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

    /*
     * RE host seam only. The original desktop position database and
     * PositionOrder/BackLayout placement are separate binary-backed layers.
     * This deterministic grid exists only so renderer/FTR reconstruction can
     * be exercised visibly before that database is restored.
     */
    const long column = static_cast<long>(index % 8);
    const long row = static_cast<long>(index / 8);
    view->portableSetPosition(28 + column * 92, 28 + row * 104);

    /*
     * Temporary renderer-facing extent for the host seam. The original
     * ImdView width/height fields are binary-proven at +0x0c/+0x10, but the
     * exact InvertedTee computed size will be restored from its renderer
     * geometry methods. Keep this value explicitly non-ABI/non-original.
     */
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
     * Original ImdViewPanelFindViewByLocation() @ 0x09680470 tests the
     * ImdView geometry fields directly:
     *   visible bit 31 at +0x14
     *   x/y at +0x04/+0x08
     *   width/height at +0x0c/+0x10
     * and returns the matching view.
     *
     * The original collection is y-ordered and uses a binary-search shortcut.
     * The portable side table is small, so preserve the exact rectangle test
     * without claiming the original collection implementation.
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
     * Original exported ImdViewPanelMoveView() @ 0x096a2f3c delegates to the
     * adopted layout's moveView(view,x,y) virtual and, on success, invalidates
     * the ViewPanel/layout state before returning true.
     *
     * The portable layout move virtual is not restored yet. Preserve the
     * externally visible geometry result here and redraw through the same
     * ViewPanel, rather than creating a desktop-specific position path.
     */
    view.portableSetPosition(x, y);

    if (XtIsRealized(widget)) {
        XClearWindow(XtDisplay(widget), XtWindow(widget));
        ImdViewPanelRedraw(widget);
    }
    return true;
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
