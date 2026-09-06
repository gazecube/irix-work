#include "ImdIconPanel.h"

#include <map>
#include <vector>

namespace
{
struct AdoptedViewRecord
{
    ImdView *view;
    ImdIconPanel *panel;

    AdoptedViewRecord(ImdView *v = 0, ImdIconPanel *p = 0)
        : view(v), panel(p)
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
    ImdIconPanel *panel)
{
    if (!widget || !view)
        return;

    /*
     * Original libimdIconPanel.so exports two overloads:
     *
     *   ImdViewPanelAdoptView(Widget, ImdView *, ImdView *)
     *   ImdViewPanelAdoptView(Widget, ImdView *, ImdIconPanel *)
     *
     * The second overload is the panel ownership path recovered for the
     * model-backed view flow. The original widget maintains internal view
     * collections and invalidates layout/redisplay state. Until the complete
     * ImdViewPanel class record/private instance data is reconstructed, retain
     * the exact ownership association in a native side table.
     */
    gAdoptedViews[widget].push_back(AdoptedViewRecord(view, panel));
}
