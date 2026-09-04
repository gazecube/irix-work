/*
 * BackLayoutOverlap.c++ - fm overlap and shape-repair reconstruction
 *
 * Binary: /usr/sbin/fm (IRIX 6.5 specimen)
 * ELF: 32-bit MSB MIPS N32/MIPS-III
 *
 * Bodies below are reconstructed from exact named function ranges in fm's
 * dynamic symbol table. Unknown collection/vtable details remain explicit
 * rather than being silently normalized.
 */

#include <stdint.h>

struct XRectangle {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
};

static inline unsigned char load8(const void *p, unsigned off)
{
    return *((const unsigned char *)p + off);
}

static inline int32_t load32s(const void *p, unsigned off)
{
    return *(const int32_t *)((const unsigned char *)p + off);
}

static inline void *loadp32(const void *p, unsigned off)
{
    return (void *)(uintptr_t)*(const uint32_t *)((const unsigned char *)p + off);
}

extern "C" {

/* Imported / previously recovered helpers. */
unsigned int numViews__11ImdViewListGv(void *list);
void *get__11ImdViewListGUi(void *list, unsigned int index);
unsigned int findViewIndex__10BackLayoutGR11ImdViewList(void *self, void *list);
int ImdRectsOverlap__GlN71(long l1, long t1, long r1, long b1,
                           long l2, long t2, long r2, long b2);
void shapeView__10BackLayoutGR7ImdView(void *self, void *view);

void ImdViewPanelPointFromX__GP10_WidgetReclT2RlT4(
    void *panel, long x, long y, long &outX, long &outY);
void ImdViewPanelSizeFromX__GP10_WidgetReclT2RlT4(
    void *panel, long width, long height, long &outW, long &outH);
void ImdViewPanelFindOverlappingViews__GP10_WidgetReclN32R38ImdOrderedCollectionOf__pt__9_7ImdView(
    void *panel, long left, long top, long right, long bottom, void *collection);

int XShapeCombineRectangles(void *display, unsigned long window,
                            int dest_kind, int x_off, int y_off,
                            XRectangle *rects, int n_rects,
                            int op, int ordering);

/* Opaque IRIX collection support. */
void *__nw__GUi(unsigned int size);
void __ct__26ImdOrderedCollectionOfImplGv(void *collection);

/*
 * 0x1002ecc0, size 400
 *
 * Signature from symbol:
 *   isOverlapping__10BackLayoutGlN31R11ImdViewList
 *
 * N32 argument registers establish:
 *   self, left, top, right, bottom, ImdViewList&
 *
 * ImdView geometry observed here:
 *   +0x04 x
 *   +0x08 y
 *   +0x0c width
 *   +0x10 height
 *
 * The list is sorted enough for an early exit once candidate.bottom is above
 * the next view's y coordinate. findViewIndex() provides the first plausible
 * list index and avoids rescanning views that cannot overlap horizontally.
 */
int isOverlapping__10BackLayoutGlN31R11ImdViewList(
    void *self, long left, long top, long right, long bottom, void *views)
{
    unsigned int i = findViewIndex__10BackLayoutGR11ImdViewList(self, views);
    unsigned int count = numViews__11ImdViewListGv(views);

    for (; i < count; ++i) {
        void *view = get__11ImdViewListGUi(views, i);
        long vx = load32s(view, 0x04);
        long vy = load32s(view, 0x08);
        long vr = vx + load32s(view, 0x0c);
        long vb = vy + load32s(view, 0x10);

        if (bottom < vy)
            return 0;
        if (vr < left)
            continue;
        if (right < vx)
            continue;

        if (ImdRectsOverlap__GlN71(left, top, right, bottom,
                                   vx, vy, vr, vb))
            return 1;
    }

    return 0;
}

/*
 * 0x10025140, size 676
 *
 * Exact high-level flow recovered:
 *
 *   if (!useShapes) return;
 *
 *   XShapeCombineRectangles(... ShapeBounding,
 *                           rects, count,
 *                           ShapeSubtract, YXBanded);
 *
 *   for each removed rectangle:
 *       allocate/construct ImdOrderedCollectionOf<ImdView>
 *       convert rectangle origin from X coords to ViewPanel coords
 *       convert rectangle size from X coords to ViewPanel coords
 *       ImdViewPanelFindOverlappingViews(..., collection)
 *       for each overlapping view:
 *           if view != excludedView and selected shape-state bits are clear:
 *               shapeView(view)
 *       destroy temporary iterator/collection objects
 *
 * This is intentionally not flattened into guessed STL-like collection code.
 * The binary uses cfront-era virtual collection objects; the exact iterator
 * class names/destructors are still being recovered. The essential desktop
 * shape-repair behavior and all X/ViewPanel operations are established.
 */
void unshapeRectangles__10BackLayoutGP10XRectangleiP7ImdView(
    void *self, XRectangle *rects, int count, void *excludedView)
{
    if (!load8(self, 0x4c))
        return;

    void *panel = loadp32(self, 0x04);
    void *screen = loadp32(panel, 0x58);
    void *display = loadp32(screen, 0x04);
    unsigned long window = (uint32_t)(uintptr_t)loadp32(panel, 0x60);

    /* ShapeBounding, ShapeSubtract, YXBanded. */
    XShapeCombineRectangles(display, window, 0, 0, 0,
                            rects, count, 3, 3);

    for (int i = 0; i < count; ++i) {
        long logicalX = 0;
        long logicalY = 0;
        long logicalW = 0;
        long logicalH = 0;

        ImdViewPanelPointFromX__GP10_WidgetReclT2RlT4(
            panel, rects[i].x, rects[i].y, logicalX, logicalY);
        ImdViewPanelSizeFromX__GP10_WidgetReclT2RlT4(
            panel, rects[i].width, rects[i].height, logicalW, logicalH);

        /*
         * The original allocates 12 bytes for this collection object and
         * constructs ImdOrderedCollectionOfImpl. Keep that allocation shape
         * visible until its full C++ declaration is recovered.
         */
        void *overlaps = __nw__GUi(12);
        if (overlaps)
            __ct__26ImdOrderedCollectionOfImplGv(overlaps);

        ImdViewPanelFindOverlappingViews__GP10_WidgetReclN32R38ImdOrderedCollectionOf__pt__9_7ImdView(
            panel,
            logicalX, logicalY,
            logicalX + logicalW, logicalY + logicalH,
            overlaps);

        /*
         * Remaining assembly iterates the returned collection via its virtual
         * iterator interface. For each result it skips excludedView and views
         * whose ImdView +0x14 state has bits in mask 0x3fffffff >> 29 set,
         * then calls BackLayout::shapeView().
         *
         * This loop stays documented rather than fabricated until the exact
         * collection iterator ABI is named from adjacent symbols/classes.
         */
    }
}

} /* extern "C" */
