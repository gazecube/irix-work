#include "ImdIconPanel.h"

#include <Xm/DrawingA.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <cstdio>
#include <cstring>
#include <map>

#if defined(IMDICONPANEL_HAVE_RECONSTRUCTED_SGM)
extern "C" WidgetClass sgVisualDrawingAreaWidgetClass;
#endif

#if defined(IMDICONPANEL_HAVE_DLFCN)
#include <dlfcn.h>
#endif

namespace
{
WidgetClass resolveViewPanelWidgetClass()
{
#if defined(IMDICONPANEL_HAVE_RECONSTRUCTED_SGM)
    /*
     * Our reconstructed libSgm exports the historical IRIX symbol directly.
     * Keeping a real reference here also prevents --as-needed from dropping
     * libSgm from the final executable before runtime symbol probing occurs.
     */
    if (sgVisualDrawingAreaWidgetClass)
        return sgVisualDrawingAreaWidgetClass;
#endif

#if defined(IMDICONPANEL_HAVE_DLFCN)
    /*
     * IRIX 6.5.0's ImdViewPanel class record inherits from the exported
     * sgVisualDrawingAreaWidgetClass data symbol. External compatibility
     * libraries may omit it, so this remains as the fallback probe path.
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

void reportViewPanelSuperclassOnce()
{
    static bool reported = false;
    if (reported)
        return;

    reported = true;
    std::fprintf(
        stderr,
        "ImdViewPanel superclass: %s\n",
        usingSgVisualDrawingArea()
            ? "SgVisualDrawingArea"
            : "XmDrawingArea fallback");
}

void reportSgVisualState(Widget widget)
{
    if (!widget || !usingSgVisualDrawingArea())
        return;

    Visual *visual = 0;
    Colormap colormap = None;
    int depth = 0;
    Boolean installColormap = False;
    Boolean ditherBackground = False;

    XtVaGetValues(
        widget,
        "visual", &visual,
        XtNcolormap, &colormap,
        XtNdepth, &depth,
        "installColormap", &installColormap,
        "ditherBackground", &ditherBackground,
        NULL);

    if (visual)
    {
        std::fprintf(
            stderr,
            "ImdViewPanel visual: id=0x%lx class=%d depth=%d colormap=0x%lx\n",
            static_cast<unsigned long>(XVisualIDFromVisual(visual)),
            visual->c_class,
            depth,
            static_cast<unsigned long>(colormap));
    }
    else
    {
        std::fprintf(
            stderr,
            "ImdViewPanel visual: id=NULL class=-1 depth=%d colormap=0x%lx\n",
            depth,
            static_cast<unsigned long>(colormap));
    }

    std::fprintf(
        stderr,
        "SgVisualDrawingArea resources: installColormap=%s ditherBackground=%s\n",
        installColormap ? "True" : "False",
        ditherBackground ? "True" : "False");
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
    reportViewPanelSuperclassOnce();

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
    {
        gViewPanelState[widget] = ViewPanelState();
        reportSgVisualState(widget);
    }

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
