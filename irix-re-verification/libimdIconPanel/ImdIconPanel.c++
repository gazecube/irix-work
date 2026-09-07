#include "ImdIconPanel.h"
#include "DragManager.h"

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

extern "C" void ImdViewPanelPortableRendererChanged(Widget, ImdRenderer *);
extern "C" void ImdViewPanelPortableResetViews(Widget);

namespace
{
WidgetClass resolveViewPanelWidgetClass()
{
#if defined(IMDICONPANEL_HAVE_RECONSTRUCTED_SGM)
    if (sgVisualDrawingAreaWidgetClass)
        return sgVisualDrawingAreaWidgetClass;
#endif
#if defined(IMDICONPANEL_HAVE_DLFCN)
    void *symbol = dlsym(RTLD_DEFAULT, "sgVisualDrawingAreaWidgetClass");
    if (symbol) {
        WidgetClass *widgetClass = static_cast<WidgetClass *>(symbol);
        if (widgetClass && *widgetClass) return *widgetClass;
    }
    static void *sgmHandle = 0;
    if (!sgmHandle) {
        static const char *const candidates[] = { "libSgm.so", "libSgm.so.1", "libSgm.so.1.1", 0 };
        for (int i = 0; candidates[i] && !sgmHandle; ++i)
            sgmHandle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
    }
    if (sgmHandle) {
        symbol = dlsym(sgmHandle, "sgVisualDrawingAreaWidgetClass");
        if (symbol) {
            WidgetClass *widgetClass = static_cast<WidgetClass *>(symbol);
            if (widgetClass && *widgetClass) return *widgetClass;
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
    ViewPanelState() : renderer(0), layout(0), scale(1.0f), scaleArg0(0), scaleArg1(0), blockDepth(0) {}
};

std::map<Widget, ViewPanelState> gViewPanelState;

bool isPrivateImdResource(const char *name)
{
    if (!name) return false;
    return std::strcmp(name, "horzScrollPolicy") == 0 ||
           std::strcmp(name, "vertScrollPolicy") == 0 ||
           std::strcmp(name, "dropEnabled") == 0 ||
           std::strcmp(name, "dragEnabled") == 0 ||
           std::strcmp(name, "boxSelectEnabled") == 0 ||
           std::strcmp(name, "renameEnabled") == 0;
}

bool usingSgVisualDrawingArea() { return imdViewPanelWidgetClass != xmDrawingAreaWidgetClass; }

void reportViewPanelSuperclassOnce()
{
    static bool reported = false;
    if (reported) return;
    reported = true;
    std::fprintf(stderr, "ImdViewPanel superclass: %s\n", usingSgVisualDrawingArea() ? "SgVisualDrawingArea" : "XmDrawingArea fallback");
}

void reportSgVisualState(Widget widget)
{
    if (!widget || !usingSgVisualDrawingArea()) return;
    Visual *visual = 0;
    Colormap colormap = None;
    int depth = 0;
    Pixel backgroundPixel = 0;
    Pixmap backgroundPixmap = None;
    Boolean installColormap = False;
    Boolean ditherBackground = False;
    XtVaGetValues(widget,
                  "visual", &visual,
                  XtNcolormap, &colormap,
                  XtNdepth, &depth,
                  XtNbackground, &backgroundPixel,
                  XtNbackgroundPixmap, &backgroundPixmap,
                  "installColormap", &installColormap,
                  "ditherBackground", &ditherBackground,
                  NULL);
    if (visual)
        std::fprintf(stderr, "ImdViewPanel visual: id=0x%lx class=%d depth=%d colormap=0x%lx\n", static_cast<unsigned long>(XVisualIDFromVisual(visual)), visual->c_class, depth, static_cast<unsigned long>(colormap));
    else
        std::fprintf(stderr, "ImdViewPanel visual: id=NULL class=-1 depth=%d colormap=0x%lx\n", depth, static_cast<unsigned long>(colormap));
    std::fprintf(stderr, "SgVisualDrawingArea resources: installColormap=%s ditherBackground=%s\n", installColormap ? "True" : "False", ditherBackground ? "True" : "False");
    std::fprintf(stderr, "SgVisualDrawingArea background: pixel=0x%lx pixmap=0x%lx\n", static_cast<unsigned long>(backgroundPixel), static_cast<unsigned long>(backgroundPixmap));
    if (colormap != None) {
        XColor color;
        color.pixel = backgroundPixel;
        XQueryColor(XtDisplay(widget), colormap, &color);
        std::fprintf(stderr, "SgVisualDrawingArea background RGB: %u %u %u\n", static_cast<unsigned int>(color.red), static_cast<unsigned int>(color.green), static_cast<unsigned int>(color.blue));
    }
}

bool isUnsupportedFallbackResource(const char *name)
{
    if (!name || usingSgVisualDrawingArea()) return false;
    return std::strcmp(name, "installColormap") == 0 || std::strcmp(name, "ditherBackground") == 0;
}
}

WidgetClass imdViewPanelWidgetClass = resolveViewPanelWidgetClass();

extern "C" Widget ImdCreateViewPanel(Widget parent, char *name, Arg *args, Cardinal count)
{
    reportViewPanelSuperclassOnce();
    Arg *filtered = 0;
    Cardinal filteredCount = 0;
    if (count != 0) {
        filtered = new Arg[count];
        for (Cardinal i = 0; i < count; ++i) {
            const char *resourceName = args[i].name;
            if (!isPrivateImdResource(resourceName) && !isUnsupportedFallbackResource(resourceName))
                filtered[filteredCount++] = args[i];
        }
    }
    Widget widget = XtCreateWidget(name, imdViewPanelWidgetClass, parent, filtered, filteredCount);
    delete [] filtered;
    if (widget) {
        gViewPanelState[widget] = ViewPanelState();
        reportSgVisualState(widget);
    }
    return widget;
}

extern "C" void ImdViewPanelAdoptRenderer(Widget widget, ImdRenderer *renderer)
{
    gViewPanelState[widget].renderer = renderer;
    if (renderer) renderer->realize(widget);
    ImdViewPanelPortableRendererChanged(widget, renderer);
}

extern "C" void ImdViewPanelAdoptLayout(Widget widget, ImdLayout *layout)
{
    gViewPanelState[widget].layout = layout;
}

extern "C" ImdLayout *ImdViewPanelGetLayout(Widget widget)
{
    std::map<Widget, ViewPanelState>::iterator it = gViewPanelState.find(widget);
    if (it == gViewPanelState.end())
        return 0;
    return it->second.layout;
}

extern "C" void ImdViewPanelSetScale(Widget widget, float scale, long arg0, long arg1)
{
    ViewPanelState &state = gViewPanelState[widget];
    state.scale = scale; state.scaleArg0 = arg0; state.scaleArg1 = arg1;
}
extern "C" void ImdViewPanelBeginBlockChanges(Widget widget) { ++gViewPanelState[widget].blockDepth; }
extern "C" void ImdViewPanelEndBlockChanges(Widget widget)
{
    ViewPanelState &state = gViewPanelState[widget];
    if (state.blockDepth != 0) --state.blockDepth;
}

ImdIconView::ImdIconView() : _portableFlags(0), _portableLabel(), _portableIconResource() {}
ImdIconView::~ImdIconView() {}
void ImdIconView::setFlag(unsigned int flag, bool value) { if (value) _portableFlags |= flag; else _portableFlags &= ~flag; }
bool ImdIconView::getFlag(unsigned int flag) const { return (_portableFlags & flag) == flag; }
ImdString ImdIconView::getLabel() const { return _portableLabel; }
void ImdIconView::setLabel(const ImdString &label) { _portableLabel = label; }

ImdIconPanel::ImdIconPanel(const char *name, Widget parent, Arg *args, int count)
    : _baseWidget(0),
      _dragManager(0)
{
    _baseWidget = ImdCreateViewPanel(parent, const_cast<char *>(name), args, static_cast<Cardinal>(count));

    /*
     * Original ImdIconPanel constructor stores a newly allocated
     * ImdDragManager at original N32 +0x44. The manager itself stores the
     * owning panel at +0x40 and initializes its widget/DnD machinery.
     */
    _dragManager = new ImdDragManager(this);
    if (_baseWidget)
        _dragManager->init(_baseWidget);
}

void ImdIconPanel::adoptView(ImdIconView *view, ImdIconView *parentView)
{
    if (!view) return;
    if (parentView) ImdViewPanelAdoptView(_baseWidget, view, parentView);
    else ImdViewPanelAdoptView(_baseWidget, view, this);
    _views.push_back(view);
    applySelectStrings(*view);
}

void ImdIconPanel::applySelectStrings(ImdIconView &view) { (void)view; }

void
ImdIconPanel::dragStart(int item, long x, long y, XEvent *event)
{
    /* Original base method @ 0x0969eca0 delegates to ImdDragManager. */
    if (_dragManager)
        _dragManager->dragStart(item, x, y, event);
}

bool
ImdIconPanel::dragMotion(ImdView *view)
{
    /*
     * Original base method @ 0x0969eb18 performs DnD target validation. For
     * local same-panel movement, a non-null view is an acceptable target; the
     * full target collection logic remains in the Motif DnD reconstruction.
     */
    return view != 0;
}

void
ImdIconPanel::dragEnd()
{
    /* Original base dragEnd() @ 0x0969eaa0 is an eight-byte no-op. */
}

void ImdIconPanel::portableResetViews(bool destroyViews)
{
    /*
     * Keep the temporary ViewPanel adoption table in the same lifecycle as
     * the panel-owned views. Original ImdViewPanelDeleteAllViews() removes the
     * adopted set before ImdIconPanel releases the view objects.
     */
    if (_baseWidget)
        ImdViewPanelPortableResetViews(_baseWidget);

    if (destroyViews) {
        for (std::vector<ImdView *>::iterator it = _views.begin(); it != _views.end(); ++it) delete *it;
    }
    _views.clear();
}

ImdIconPanel::~ImdIconPanel()
{
    portableResetViews(false);

    delete _dragManager;
    _dragManager = 0;

    if (_baseWidget) {
        gViewPanelState.erase(_baseWidget);
        if (XtIsWidget(_baseWidget)) XtDestroyWidget(_baseWidget);
        _baseWidget = 0;
    }
}
