#include "ImdIconPanel.h"

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
}

void
ImdViewPanelAdoptView(
    Widget widget,
    ImdView *view,
    ImdView *parentView)
{
    if (!widget || !view || !parentView)
        return;

    /*
     * Original overload used for child-view ownership:
     *   ImdViewPanelAdoptView(Widget, ImdView *, ImdView *)
     *
     * The private ImdViewPanel instance collections/layout invalidation remain
     * unrecovered. Preserve the exact parent relationship in the host side
     * table without attributing this storage mechanism to IRIX.
     */
    gAdoptedViews[widget].push_back(AdoptedViewRecord(view, parentView, 0));
}

void
ImdViewPanelAdoptView(
    Widget widget,
    ImdView *view,
    ImdIconPanel *panel)
{
    if (!widget || !view || !panel)
        return;

    /*
     * Original overload used for root/panel ownership:
     *   ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *)
     */
    gAdoptedViews[widget].push_back(AdoptedViewRecord(view, 0, panel));
}
