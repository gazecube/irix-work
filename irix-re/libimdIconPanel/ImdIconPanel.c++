#include "ImdIconPanel.h"

#include <Xm/DrawingA.h>
#include <cstring>
#include <map>

/*
 * Reconstructed Linux backing detail.
 *
 * The original IRIX library exports a distinct imdViewPanelWidgetClass.
 * Until the private widget class record is recovered, use Motif's DrawingArea
 * class as the backing Xt class while preserving the original public symbol
 * and construction API. Do not treat this alias as binary-proven.
 */
WidgetClass imdViewPanelWidgetClass = xmDrawingAreaWidgetClass;

namespace
{
struct ViewPanelState
{
    ImdRenderer *renderer;
    ImdLayout *layout;
    float scale;
    long scaleArg0;
    long scaleArg1;

    ViewPanelState()
        : renderer(0), layout(0), scale(1.0f), scaleArg0(0), scaleArg1(0)
    {
    }
};

std::map<Widget, ViewPanelState> gViewPanelState;

bool isImdOnlyResource(const char *name)
{
    if (!name)
        return false;

    return std::strcmp(name, "installColormap") == 0 ||
           std::strcmp(name, "ditherBackground") == 0 ||
           std::strcmp(name, "horzScrollPolicy") == 0 ||
           std::strcmp(name, "vertScrollPolicy") == 0 ||
           std::strcmp(name, "dropEnabled") == 0 ||
           std::strcmp(name, "dragEnabled") == 0 ||
           std::strcmp(name, "boxSelectEnabled") == 0 ||
           std::strcmp(name, "renameEnabled") == 0;
}
}

extern "C" Widget ImdCreateViewPanel(
    Widget parent,
    char *name,
    Arg *args,
    Cardinal count)
{
    /*
     * Original IRIX behavior is XtCreateWidget(name,
     * imdViewPanelWidgetClass, parent, args, count).
     *
     * The temporary DrawingArea backing class does not know the private Imd
     * resources, so filter only those recovered Imd-specific names. Other Xt
     * resources, including any visual/depth/colormap args, are preserved.
     */
    Arg *filtered = 0;
    Cardinal filteredCount = 0;

    if (count != 0)
    {
        filtered = new Arg[count];
        for (Cardinal i = 0; i < count; ++i)
        {
            if (!isImdOnlyResource(args[i].name))
                filtered[filteredCount++] = args[i];
        }
    }

    Widget widget = XtCreateWidget(
        name,
        imdViewPanelWidgetClass,
        parent,
        filtered,
        filteredCount);

    delete [] filtered;

    if (widget)
        gViewPanelState[widget] = ViewPanelState();

    return widget;
}

extern "C" void ImdViewPanelAdoptRenderer(Widget widget, ImdRenderer *renderer)
{
    gViewPanelState[widget].renderer = renderer;
}

extern "C" void ImdViewPanelAdoptLayout(Widget widget, ImdLayout *layout)
{
    gViewPanelState[widget].layout = layout;
}

extern "C" void ImdViewPanelSetScale(
    Widget widget,
    float scale,
    long arg0,
    long arg1)
{
    ViewPanelState &state = gViewPanelState[widget];
    state.scale = scale;
    state.scaleArg0 = arg0;
    state.scaleArg1 = arg1;
}

ImdIconPanel::ImdIconPanel(
    const char *name,
    Widget parent,
    Arg *args,
    int count)
    : _baseWidget(0)
{
    _baseWidget = ImdCreateViewPanel(
        parent,
        const_cast<char *>(name),
        args,
        static_cast<Cardinal>(count));
}

ImdIconPanel::~ImdIconPanel()
{
    if (_baseWidget)
    {
        gViewPanelState.erase(_baseWidget);
        if (XtIsWidget(_baseWidget))
            XtDestroyWidget(_baseWidget);
        _baseWidget = 0;
    }
}
