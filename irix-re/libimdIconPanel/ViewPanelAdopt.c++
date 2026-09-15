#include "ImdIconPanel.h"

#include <Xm/DrawingA.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <algorithm>
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
std::map<Widget, unsigned int> gPortableBlockDepth;
std::map<Widget, bool> gPortableLayoutDirty;
std::map<Widget, bool> gPortableRedrawDirty;

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

    if (r.gridWidth < 25)
        r.gridWidth = 25;

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
    return origin + (quotient + ((remainder * 2 > step) ? 1 : 0)) * step;
}

static long
binaryGridHeight(const ImdView &view, const BinaryGridResources &r)
{
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
    long requiredWidth = r.leftMargin + r.rightMargin;
    long requiredHeight = r.topMargin + r.bottomMargin;

    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        ImdView *view = it->view;
        if (!view || !view->portableVisible())
            continue;

        const long right = view->portableX() +
                           view->portableWidth() +
                           r.rightMargin;
        const long bottom = view->portableY() +
                            view->portableHeight() +
                            r.bottomMargin;
        if (right > requiredWidth)
            requiredWidth = right;
        if (bottom > requiredHeight)
            requiredHeight = bottom;
    }

    Dimension currentWidth = 0;
    Dimension currentHeight = 0;
    Dimension parentWidth = 0;
    Dimension parentHeight = 0;
    XtVaGetValues(widget,
                  XtNwidth, &currentWidth,
                  XtNheight, &currentHeight,
                  NULL);
    if (XtParent(widget))
        XtVaGetValues(XtParent(widget),
                      XtNwidth, &parentWidth,
                      XtNheight, &parentHeight,
                      NULL);

    Dimension targetWidth = requiredWidth > 0
        ? static_cast<Dimension>(requiredWidth)
        : static_cast<Dimension>(1);
    Dimension targetHeight = requiredHeight > 0
        ? static_cast<Dimension>(requiredHeight)
        : static_cast<Dimension>(1);
    if (parentWidth > targetWidth)
        targetWidth = parentWidth;
    if (parentHeight > targetHeight)
        targetHeight = parentHeight;

    if (targetWidth != currentWidth || targetHeight != currentHeight)
        XtVaSetValues(widget,
                      XtNwidth, targetWidth,
                      XtNheight, targetHeight,
                      NULL);
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

static bool
viewIntersectsDamage(const ImdView &view, const XRectangle *damage)
{
    if (!damage)
        return true;

    const long left = view.portableX();
    const long top = view.portableY();
    const long right = left + view.portableWidth();
    const long bottom = top + view.portableHeight();

    const long damageLeft = damage->x;
    const long damageTop = damage->y;
    const long damageRight = damageLeft + damage->width;
    const long damageBottom = damageTop + damage->height;

    return right >= damageLeft &&
           left <= damageRight &&
           bottom >= damageTop &&
           top <= damageBottom;
}

static void redrawPortableRegion(Widget widget, const XRectangle *damage);

static void
exposeHandler(Widget widget, XtPointer, XEvent *event, Boolean *)
{
    if (!event || event->type != Expose)
        return;

    XRectangle damage;
    damage.x = static_cast<short>(event->xexpose.x);
    damage.y = static_cast<short>(event->xexpose.y);
    damage.width = static_cast<unsigned short>(event->xexpose.width);
    damage.height = static_cast<unsigned short>(event->xexpose.height);
    redrawPortableRegion(widget, &damage);
}

static void
drawingAreaExposeCB(Widget widget, XtPointer, XtPointer callData)
{
    XmDrawingAreaCallbackStruct *cb =
        static_cast<XmDrawingAreaCallbackStruct *>(callData);
    if (!cb || !cb->event || cb->event->type != Expose) {
        redrawPortableRegion(widget, 0);
        return;
    }

    XRectangle damage;
    damage.x = static_cast<short>(cb->event->xexpose.x);
    damage.y = static_cast<short>(cb->event->xexpose.y);
    damage.width = static_cast<unsigned short>(cb->event->xexpose.width);
    damage.height = static_cast<unsigned short>(cb->event->xexpose.height);
    redrawPortableRegion(widget, &damage);
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
ImdViewPanelPortableBeginBlockChanges(Widget widget)
{
    if (!widget)
        return;
    ++gPortableBlockDepth[widget];
}

extern "C" bool
ImdViewPanelPortableChangesBlocked(Widget widget)
{
    if (!widget)
        return false;
    return gPortableBlockDepth[widget] != 0;
}

extern "C" void
ImdViewPanelPortableEndBlockChanges(Widget widget)
{
    if (!widget)
        return;

    unsigned int &depth = gPortableBlockDepth[widget];
    if (depth != 0)
        --depth;
    if (depth != 0)
        return;

    if (gPortableLayoutDirty[widget]) {
        gPortableLayoutDirty[widget] = false;
        layoutPortableViews(widget);
    }

    if (gPortableRedrawDirty[widget]) {
        gPortableRedrawDirty[widget] = false;
        if (XtIsRealized(widget)) {
            XClearArea(XtDisplay(widget), XtWindow(widget), 0, 0, 0, 0, False);
            ImdViewPanelRedraw(widget);
        }
    }
}

extern "C" void
ImdViewPanelPortableResetViews(Widget widget)
{
    if (!widget)
        return;
    gAdoptedViews.erase(widget);
    gPortableLayoutDirty[widget] = true;
    gPortableRedrawDirty[widget] = true;
}

extern "C" void
ImdViewPanelPortableScaleChanged(Widget widget)
{
    if (!widget || !XtIsRealized(widget))
        return;

    std::map<Widget, ImdRenderer *>::iterator rendererIt = gRenderers.find(widget);
    if (rendererIt == gRenderers.end() || !rendererIt->second)
        return;

    ImdRenderer *renderer = rendererIt->second;
    AdoptedViewList &views = gAdoptedViews[widget];

    /*
     * Binary anchor: ImdViewPanelSetScale() @ 0x09687afc propagates the new
     * scale to its renderer/layout objects first, then walks extant views and
     * adjusts geometry/placement. It does not perform the host's old
     * paint-everything -> discover-size-change -> relayout -> paint-everything
     * sequence. Stage the same information here without rendering FTI data.
     */
    GC gc = XCreateGC(XtDisplay(widget), XtWindow(widget), 0, 0);
    XFontStruct *font = gc
        ? XQueryFont(XtDisplay(widget), XGContextFromGC(gc))
        : 0;
    const long iconExtent = std::max<long>(1, static_cast<long>(
        100.0 * static_cast<double>(renderer->scale()) * 0.5));

    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        ImdIconView *view = dynamic_cast<ImdIconView *>(it->view);
        if (!view || !view->portableVisible())
            continue;

        const ImdString label = view->getLabel();
        long textWidth = 0;
        long textHeight = 0;
        if (font && !label.empty()) {
            textWidth = XTextWidth(font,
                                   label.c_str(),
                                   static_cast<int>(label.stdString().size()));
            textHeight = font->ascent + font->descent;
        }
        view->portableSetSize(std::max(iconExtent, textWidth),
                              iconExtent + textHeight);
    }

    if (font)
        XFreeFontInfo(0, font, 1);
    if (gc)
        XFreeGC(XtDisplay(widget), gc);

    layoutPortableViews(widget);

    /*
     * Repaint only the clip-window viewport. The original SetScale path walks
     * and moves only participating views; our host renderer already has the
     * binary-backed damage-region culling seam, so feed it the visible region
     * instead of calling ImdViewPanelRedraw(widget) for the full work area.
     */
    Position widgetX = 0;
    Position widgetY = 0;
    Dimension widgetWidth = 0;
    Dimension widgetHeight = 0;
    Dimension parentWidth = 0;
    Dimension parentHeight = 0;
    XtVaGetValues(widget,
                  XtNx, &widgetX,
                  XtNy, &widgetY,
                  XtNwidth, &widgetWidth,
                  XtNheight, &widgetHeight,
                  NULL);
    if (XtParent(widget))
        XtVaGetValues(XtParent(widget),
                      XtNwidth, &parentWidth,
                      XtNheight, &parentHeight,
                      NULL);

    const long visibleX = widgetX < 0 ? -static_cast<long>(widgetX) : 0;
    const long visibleY = widgetY < 0 ? -static_cast<long>(widgetY) : 0;
    const long availableWidth = static_cast<long>(widgetWidth) - visibleX;
    const long availableHeight = static_cast<long>(widgetHeight) - visibleY;
    const long visibleWidth = std::max<long>(0, std::min<long>(
        availableWidth,
        parentWidth ? static_cast<long>(parentWidth) : availableWidth));
    const long visibleHeight = std::max<long>(0, std::min<long>(
        availableHeight,
        parentHeight ? static_cast<long>(parentHeight) : availableHeight));

    if (visibleWidth <= 0 || visibleHeight <= 0)
        return;

    XRectangle damage;
    damage.x = static_cast<short>(visibleX);
    damage.y = static_cast<short>(visibleY);
    damage.width = static_cast<unsigned short>(std::min<long>(visibleWidth, 65535));
    damage.height = static_cast<unsigned short>(std::min<long>(visibleHeight, 65535));

    XClearArea(XtDisplay(widget), XtWindow(widget),
               damage.x, damage.y, damage.width, damage.height, False);
    redrawPortableRegion(widget, &damage);
}

namespace
{
static void
redrawPortableRegion(Widget widget, const XRectangle *damage)
{
    if (!widget || !XtIsRealized(widget))
        return;

    if (gPortableBlockDepth[widget] != 0) {
        gPortableRedrawDirty[widget] = true;
        return;
    }

    std::map<Widget, ImdRenderer *>::iterator rendererIt = gRenderers.find(widget);
    if (rendererIt == gRenderers.end() || !rendererIt->second)
        return;

    ImdRenderer *renderer = rendererIt->second;
    AdoptedViewList &views = gAdoptedViews[widget];

    /*
     * Original ImdViewPanelDrawView() @ 0x0967bf44 computes the transformed
     * view rectangle and gates renderer dispatch through a region-intersection
     * test. ImdViewPanelWalkAllVisibleViews() @ 0x096a3214 is the corresponding
     * visible-view traversal. Preserve that architecture for Expose handling:
     * a scroll-damage strip must not redraw the complete adopted view set.
     *
     * The original library also imports XClearArea/XClearWindow. On the host
     * renderer, redraw of a damaged view must begin from a clean view-sized
     * backing area: otherwise retained scroll pixels can survive beneath a
     * newly replayed FTI command stream and make overlapping polygons appear
     * to swap paint order between Expose events. This clear is deliberately
     * limited to damage-driven redraws; ordinary full redraws already begin
     * from the ViewPanel clear path.
     */
    bool geometryChanged = false;
    for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
        ImdView *view = it->view;
        if (!view || !view->portableVisible() || !viewIntersectsDamage(*view, damage))
            continue;

        if (damage) {
            XClearArea(XtDisplay(widget),
                       XtWindow(widget),
                       static_cast<int>(view->portableX()),
                       static_cast<int>(view->portableY()),
                       static_cast<unsigned int>(view->portableWidth()),
                       static_cast<unsigned int>(view->portableHeight()),
                       False);
        }

        const long oldWidth = view->portableWidth();
        const long oldHeight = view->portableHeight();
        renderer->draw(*view, view->portableX(), view->portableY());
        if (oldWidth != view->portableWidth() ||
            oldHeight != view->portableHeight())
            geometryChanged = true;
    }

    if (geometryChanged) {
        layoutPortableViews(widget);
        XClearArea(XtDisplay(widget), XtWindow(widget), 0, 0, 0, 0, False);
        for (AdoptedViewList::iterator it = views.begin(); it != views.end(); ++it) {
            ImdView *view = it->view;
            if (view && view->portableVisible())
                renderer->draw(*view, view->portableX(), view->portableY());
        }
    }
}
}

void
ImdViewPanelRedraw(Widget widget)
{
    redrawPortableRegion(widget, 0);
}

ImdView *
ImdViewPanelFindViewByLocation(Widget widget, long x, long y)
{
    if (!widget)
        return 0;

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

    ImdLayout *layout = ImdViewPanelGetLayout(widget);
    bool moved = false;

    if (layout)
        moved = layout->moveView(view, x, y);
    else {
        view.portableSetPosition(x, y);
        moved = true;
    }

    if (moved && XtIsRealized(widget)) {
        resizePortablePanelToLayout(widget);
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

    if (gPortableBlockDepth[widget] != 0)
        gPortableLayoutDirty[widget] = true;
    else
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

    if (gPortableBlockDepth[widget] != 0)
        gPortableLayoutDirty[widget] = true;
    else
        layoutPortableViews(widget);
}
