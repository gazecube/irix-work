#include "ImdIconPanel.h"

#include <Xm/DrawingA.h>
#include <X11/StringDefs.h>
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

/*
 * ImdIconLayout grid resources recovered from the original N32
 * libimdIconPanel.so constructor. The original resource table uses numeric
 * string defaults; retain that Xt conversion path rather than replacing it
 * with host-side magic constants.
 */
struct BinaryGridResources
{
    int gridWidth;
    int staggerOffset;
    int minimumRightIconPadding;
    int baseOfIconPadding;
    int topMargin;
    int bottomMargin;
    int leftMargin;
    int rightMargin;
    int overlapLimit;
};

static XtResource binaryGridResources[] =
{
    { (char *)"gridWidth", (char *)"GridWidth", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, gridWidth), XtRString, (XtPointer)"25" },
    { (char *)"staggerOffset", (char *)"StaggerOffset", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, staggerOffset), XtRString, (XtPointer)"50" },
    { (char *)"minimumRightIconPadding", (char *)"MinimumRightIconPadding",
      XtRInt, sizeof(int), XtOffsetOf(BinaryGridResources, minimumRightIconPadding),
      XtRString, (XtPointer)"20" },
    { (char *)"baseOfIconPadding", (char *)"BaseOfIconPadding", XtRInt,
      sizeof(int), XtOffsetOf(BinaryGridResources, baseOfIconPadding),
      XtRString, (XtPointer)"10" },
    { (char *)"topMargin", (char *)"TopMargin", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, topMargin), XtRString, (XtPointer)"20" },
    { (char *)"bottomMargin", (char *)"BottomMargin", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, bottomMargin), XtRString, (XtPointer)"20" },
    { (char *)"leftMargin", (char *)"LeftMargin", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, leftMargin), XtRString, (XtPointer)"20" },
    { (char *)"rightMargin", (char *)"RightMargin", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, rightMargin), XtRString, (XtPointer)"20" },
    { (char *)"overlapLimit", (char *)"OverlapLimit", XtRInt, sizeof(int),
      XtOffsetOf(BinaryGridResources, overlapLimit), XtRString, (XtPointer)"20" }
};

static BinaryGridResources
getBinaryGridResources(Widget widget)
{
    BinaryGridResources r;
    r.gridWidth = 25;
    r.staggerOffset = 50;
    r.minimumRightIconPadding = 20;
    r.baseOfIconPadding = 10;
    r.topMargin = 20;
    r.bottomMargin = 20;
    r.leftMargin = 20;
    r.rightMargin = 20;
    r.overlapLimit = 20;

    if (widget) {
        XtGetApplicationResources(widget,
                                  (XtPointer)&r,
                                  binaryGridResources,
                                  XtNumber(binaryGridResources),
                                  0,
                                  0);
    }

    /* Original constructor clamps gridWidth to at least 25. */
    if (r.gridWidth < 25)
        r.gridWidth = 25;

    /* Original constructor folds leftMargin into staggerOffset. */
    r.staggerOffset += r.leftMargin;
    return r;
}

static long
halfRoundedUp(long value)
{
    return (value + 1) / 2;
}

static long
nearestGrid(long value, long origin, long step)
{
    if (step <= 0)
        return value;

    long delta = value - origin;
    if (delta <= 0)
        return origin;

    const long quotient = delta / step;
    const long remainder = delta % step;

    /* Original integer path leaves exact half-way cases on the lower point. */
    return origin + (quotient + ((remainder * 2 > step) ? 1 : 0)) * step;
}

static long
binaryGridHeight(const ImdView &view, const BinaryGridResources &r)
{
    /* Original getGridHeight(): view.height + baseOfIconPadding, rounded even. */
    long height = view.portableHeight() + r.baseOfIconPadding;
    if (height & 1)
        ++height;
    return height > 0 ? height : 2;
}

static long
binaryFirstColumn(const ImdView &view,
                  const BinaryGridResources &r,
                  bool staggered)
{
    const long halfWidth = halfRoundedUp(view.portableWidth());
    const long centerAnchor = staggered
        ? r.staggerOffset
        : r.leftMargin + r.gridWidth;

    long x = centerAnchor - halfWidth;
    if (x >= r.leftMargin)
        return x;

    const long origin = staggered ? r.staggerOffset : r.leftMargin;
    long center = nearestGrid(r.leftMargin + halfWidth, origin, r.gridWidth);
    x = center - halfWidth;
    if (x < r.leftMargin)
        x += r.gridWidth;
    return x;
}

static long
binaryNextColumn(const ImdView &view,
                 const ImdView &previous,
                 const BinaryGridResources &r)
{
    const long halfWidth = halfRoundedUp(view.portableWidth());
    const long gridHeight = binaryGridHeight(view, r);
    const long row = (previous.portableY() - r.topMargin) / gridHeight;
    const long origin = (row & 1) ? r.staggerOffset : r.leftMargin;

    const long candidateCenter = previous.portableX() +
                                 previous.portableWidth() +
                                 r.minimumRightIconPadding +
                                 halfWidth;
    const long center = nearestGrid(candidateCenter, origin, r.gridWidth);
    return center - halfWidth;
}

static void
ensurePortableExtent(ImdView *view)
{
    if (!view)
        return;

    /*
     * Bootstrap extent only until ImdInvertedTeeRenderer::draw() performs the
     * binary-backed setSize() calculation from icon and label geometry.
     */
    if (view->portableWidth() <= 0 || view->portableHeight() <= 0)
        view->portableSetSize(84, 84);
    view->portableSetVisible(true);
}

static void
resizePortablePanelToLayout(Widget widget)
{
    if (!widget)
        return;

    AdoptedViewList &views = gAdoptedViews[widget];
    if (views.empty())
        return;

    const BinaryGridResources r = getBinaryGridResources(widget);
    long requiredHeight = r.topMargin + r.bottomMargin;

    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        ImdView *view = it->view;
        if (!view || !view->portableVisible())
            continue;

        const long bottom = view->portableY() +
                            view->portableHeight() +
                            r.bottomMargin;
        if (bottom > requiredHeight)
            requiredHeight = bottom;
    }

    /*
     * Original ImdIconLayout::setPanelSize() @ 0x0968b508 derives the work
     * area's vertical extent from the bottommost view's y + height plus the
     * layout's bottom margin, then resizes the underlying panel. Keep at least
     * the clip/viewport height so an empty remainder does not collapse the
     * work area below its visible parent.
     */
    Dimension currentHeight = 0;
    Dimension parentHeight = 0;
    XtVaGetValues(widget, XtNheight, &currentHeight, NULL);
    if (XtParent(widget))
        XtVaGetValues(XtParent(widget), XtNheight, &parentHeight, NULL);

    Dimension targetHeight = requiredHeight > 0
        ? static_cast<Dimension>(requiredHeight)
        : static_cast<Dimension>(1);
    if (parentHeight > targetHeight)
        targetHeight = parentHeight;

    if (targetHeight != currentHeight)
        XtVaSetValues(widget, XtNheight, targetHeight, NULL);
}

static void
layoutPortableViews(Widget widget)
{
    if (!widget)
        return;

    AdoptedViewList &views = gAdoptedViews[widget];
    if (views.empty())
        return;

    Dimension panelWidth = 0;
    XtVaGetValues(widget, XtNwidth, &panelWidth, NULL);

    const BinaryGridResources r = getBinaryGridResources(widget);
    const long usableRight = panelWidth > (Dimension)r.rightMargin
        ? (long)panelWidth - r.rightMargin
        : r.leftMargin + r.gridWidth;

    ImdView *previous = 0;
    long rowY = r.topMargin;

    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        ImdView *view = it->view;
        if (!view)
            continue;

        ensurePortableExtent(view);

        long x;
        if (!previous) {
            x = binaryFirstColumn(*view, r, false);
            rowY = r.topMargin;
        } else {
            x = binaryNextColumn(*view, *previous, r);

            /*
             * Original gridAlgorithm() wraps when candidate x plus the current
             * view width crosses panelWidth - rightMargin.
             */
            if (x + view->portableWidth() > usableRight) {
                rowY += binaryGridHeight(*view, r);
                const long row = (rowY - r.topMargin) /
                                 binaryGridHeight(*view, r);
                x = binaryFirstColumn(*view, r, (row & 1) != 0);
            }
        }

        view->portableSetPosition(x, rowY);
        previous = view;
    }

    resizePortablePanelToLayout(widget);
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

static void
drawingAreaExposeCB(Widget widget, XtPointer, XtPointer)
{
    /*
     * The portable ViewPanel is backed by SgVisualDrawingArea, which is an
     * XmDrawingArea-family widget. Render from its expose callback so drawing
     * occurs after the widget has repaired its own background. This is a host
     * event-lifecycle seam only; the original ImdViewPanel owns its renderer
     * invalidation internally.
     */
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
        if (XtIsSubclass(widget, xmDrawingAreaWidgetClass)) {
            XtAddCallback(widget,
                          XmNexposeCallback,
                          drawingAreaExposeCB,
                          0);
        } else {
            XtAddEventHandler(widget,
                              ExposureMask,
                              False,
                              exposeHandler,
                              0);
        }
        gExposeInstalled[widget] = true;
    }
}

extern "C" void
ImdViewPanelPortableResetViews(Widget widget)
{
    if (!widget)
        return;

    /*
     * Host-side ownership bookkeeping for the reconstructed ViewPanel.
     * Original ImdViewPanelDeleteAllViews() removes the panel's adopted view
     * set before the owning ImdIconPanel releases the view objects. Keeping a
     * parallel stale list here made fm continue rendering the first directory
     * population after ImdModelBasedIconPanel::changeModel().
     */
    gAdoptedViews.erase(widget);
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

    /*
     * Layout first with the best geometry currently known. The renderer's
     * binary-backed setSize() can refine width/height from icon + label metrics
     * on this pass, so remember whether any extent changed.
     */
    layoutPortableViews(widget);

    bool geometryChanged = false;
    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        if (!it->view || !it->view->portableVisible())
            continue;

        const long oldWidth = it->view->portableWidth();
        const long oldHeight = it->view->portableHeight();
        renderer->draw(*it->view,
                       it->view->portableX(),
                       it->view->portableY());
        if (oldWidth != it->view->portableWidth() ||
            oldHeight != it->view->portableHeight())
            geometryChanged = true;
    }

    if (geometryChanged) {
        /*
         * Re-run the recovered SGI boundary/wrap math with the measured item
         * extents, then repair the window once at the corrected positions.
         */
        layoutPortableViews(widget);
        XClearArea(XtDisplay(widget), XtWindow(widget), 0, 0, 0, 0, False);
        for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
            if (it->view && it->view->portableVisible())
                renderer->draw(*it->view,
                               it->view->portableX(),
                               it->view->portableY());
        }
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
    ensurePortableExtent(view);
    views.push_back(AdoptedViewRecord(view, parentView, 0));
    layoutPortableViews(widget);
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
    ensurePortableExtent(view);
    views.push_back(AdoptedViewRecord(view, 0, panel));
    layoutPortableViews(widget);
}
