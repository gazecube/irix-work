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
        if (it->view)
            renderer->draw(*it->view, it->view->portableX(), it->view->portableY());
    }
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
