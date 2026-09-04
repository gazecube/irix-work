#include "ImdIconPanel.h"

#include <Xm/DrawingA.h>
#include <cstring>
#include <map>

#if defined(IMDICONPANEL_HAVE_DLFCN)
#include <dlfcn.h>
#endif

namespace
{
WidgetClass resolveViewPanelWidgetClass()
{
#if defined(IMDICONPANEL_HAVE_DLFCN)
    /*
     * IRIX 6.5.0's ImdViewPanel class record inherits from the exported
     * sgVisualDrawingAreaWidgetClass data symbol. MaXX installations may ship
     * libSgm without exporting that historical symbol, so resolve it at
     * runtime instead of introducing a mandatory link-time reference.
     *
     * The library handle is intentionally retained for process lifetime: the
     * returned WidgetClass points into libSgm's class record storage.
     */
    void *symbol = dlsym(RTLD_DEFAULT, "sgVisualDrawingAreaWidgetClass");
    if (symbol)
    {
        WidgetClass *widgetClass = static_cast<WidgetClass *>(symbol);
        if (widgetClass && *widgetClass)
            return *widgetClass;
    }

    static void *sgmHandle = 0;
    if (!sgmHandle)
    {
        static const char *const candidates[] =
        {
            "libSgm.so",
            "libSgm.so.1",
            "libSgm.so.1.1",
            0
        };

        for (int i = 0; candidates[i] && !sgmHandle; ++i)
            sgmHandle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
    }

    if (sgmHandle)
    {
        symbol = dlsym(sgmHandle, "sgVisualDrawingAreaWidgetClass");
        if (symbol)
        {
            WidgetClass *widgetClass = static_cast<WidgetClass *>(symbol);
            if (widgetClass && *widgetClass)
                return *widgetClass;
        }
    }
#endif

    return xmDrawingAreaWidgetClass;
}

struct ViewPanelState
{
    ImdRenderer *renderer;
    ImdLayout *layout;
    float scale;
    long scaleArg0;
    long scaleArg1;
    unsigned int blockDepth;

    ViewPanelState()
        : renderer(0), layout(0), scale(1.0f), scaleArg0(0), scaleArg1(0),
          blockDepth(0)
    {
    }
};

std::map<Widget, ViewPanelState> gViewPanelState;

bool isPrivateImdResource(const char *name)
{
    if (!name)
        return false;

    return std::strcmp(name, "horzScrollPolicy") == 0 ||
           std::strcmp(name, "vertScrollPolicy") == 0 ||
           std::strcmp(name, "dropEnabled") == 0 ||
           std::strcmp(name, "dragEnabled") == 0 ||
           std::strcmp(name, "boxSelectEnabled") == 0 ||
           std::strcmp(name, "renameEnabled") == 0;
}

bool usingSgVisualDrawingArea()
{
    return imdViewPanelWidgetClass != xmDrawingAreaWidgetClass;
}

bool isUnsupportedFallbackResource(const char *name)
{
    if (!name || usingSgVisualDrawingArea())
        return false;

    /* These are SgVisualDrawingArea resources, not XmDrawingArea resources. */
    return std::strcmp(name, "installColormap") == 0 ||
           std::strcmp(name, "ditherBackground") == 0;
}
}

/*
 * imdViewPanelClassRec at 0x096caaa0 has a relocation to
 * sgVisualDrawingAreaWidgetClass. Use that class when the runtime provides the
 * historical symbol, otherwise retain XmDrawingArea as the portable fallback.
 */
WidgetClass imdViewPanelWidgetClass = resolveViewPanelWidgetClass();

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
     * Our reconstructed class record does not yet implement ImdViewPanel's
     * private resource table, so those names are filtered. Superclass
     * resources are preserved when SgVisualDrawingArea was resolved. The
     * XmDrawingArea fallback filters the two Sgm-only resources.
     */
    Arg *filtered = 0;
    Cardinal filteredCount = 0;

    if (count != 0)
    {
        filtered = new Arg[count];
        for (Cardinal i = 0; i < count; ++i)
        {
            const char *resourceName = args[i].name;
            if (!isPrivateImdResource(resourceName) &&
                !isUnsupportedFallbackResource(resourceName))
            {
                filtered[filteredCount++] = args[i];
            }
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

extern "C" void ImdViewPanelBeginBlockChanges(Widget widget)
{
    ++gViewPanelState[widget].blockDepth;
}

extern "C" void ImdViewPanelEndBlockChanges(Widget widget)
{
    ViewPanelState &state = gViewPanelState[widget];
    if (state.blockDepth != 0)
        --state.blockDepth;
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

void ImdIconPanel::adoptView(ImdView *view)
{
    if (!view)
        return;

    /*
     * Original method name/ownership direction are recovered from DWARF and
     * clogin displayIcon() call flow. Destruction ownership is deliberately
     * still left with the probe until the original destructor path is traced.
     */
    _views.push_back(view);
}

ImdIconPanel::~ImdIconPanel()
{
    _views.clear();

    if (_baseWidget)
    {
        gViewPanelState.erase(_baseWidget);
        if (XtIsWidget(_baseWidget))
            XtDestroyWidget(_baseWidget);
        _baseWidget = 0;
    }
}
