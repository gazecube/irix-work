/*
 * BackLayout.c++ - first-pass decompilation from IRIX 6.5 fm
 *
 * Binary: /usr/sbin/fm (uploaded specimen)
 * ELF: 32-bit MSB MIPS N32/MIPS-III
 *
 * This file intentionally models observed 32-bit object slots by byte offset
 * while class declarations and source headers are still unavailable. Each
 * body below is reconstructed from the named MIPS function in fm's dynamic
 * symbol table and its exact address range.
 */

#include <stdint.h>
#include <stddef.h>

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

static inline void store8(void *p, unsigned off, unsigned char v)
{
    *((unsigned char *)p + off) = v;
}

static inline void *loadp32(const void *p, unsigned off)
{
    return (void *)(uintptr_t)*((const uint32_t *)((const unsigned char *)p + off));
}

/*
 * IRIX N32/cfront virtual dispatch uses a two-word descriptor in these
 * vtables: an adjustment followed by the function pointer. Until the class
 * declarations are recovered, keep the two renderer calls explicit here.
 */
typedef void (*RendererPrettyShapeFn)(void *, void *, long, long,
                                      void *, unsigned long, void *);
typedef void (*RendererBoundsFn)(void *, void *, long, long,
                                 long *, long *, long *, long *);

static void callRendererPrettyShape(void *renderer, void *view,
                                    long x, long y, void *display,
                                    unsigned long window, void *gc)
{
    uint32_t *vtbl = (uint32_t *)loadp32(renderer, 8);
    intptr_t adjust = (int32_t)vtbl[0x90 / 4];
    RendererPrettyShapeFn fn =
        (RendererPrettyShapeFn)(uintptr_t)vtbl[0x98 / 4];
    fn((unsigned char *)renderer + adjust, view, x, y, display, window, gc);
}

static void callRendererBounds(void *renderer, void *view,
                               long x, long y, long *left, long *top,
                               long *right, long *bottom)
{
    uint32_t *vtbl = (uint32_t *)loadp32(renderer, 8);
    intptr_t adjust = (int32_t)vtbl[0x60 / 4];
    RendererBoundsFn fn = (RendererBoundsFn)(uintptr_t)vtbl[0x68 / 4];
    fn((unsigned char *)renderer + adjust, view, x, y,
       left, top, right, bottom);
}

extern "C" {

/* Imported from libimdIconPanel/libXt/libXext/libX11 in this specimen. */
void *ImdViewPanelGetRenderer__GP10_WidgetRec(void *);
void ImdViewPanelPointToX__GP10_WidgetReclT2RlT4(void *, long, long,
                                                 long &, long &);
void *ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(void *, void *);
void *ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *, void *, void *);
void ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *, void *, void *);
void *XtGetGC(void *, unsigned long, void *);
void XtReleaseGC(void *, void *);
int XShapeCombineRectangles(void *, unsigned long, int, int, int,
                            XRectangle *, int, int, int);
int XShapeCombineMask(void *, unsigned long, int, int, int,
                      unsigned long, int);
int XMapWindow(void *, unsigned long);
int XUnmapWindow(void *, unsigned long);

/* 0x1001f5c4, size 8 */
unsigned char usingShapes__10BackLayoutGv(void *self)
{
    return load8(self, 0x4c);
}

/* Forward declarations for calls recovered below. */
void unshapeView__10BackLayoutGR7ImdView(void *self, void *view);
void shapeView__10BackLayoutGR7ImdView(void *self, void *view);
void unshapeAllViews__10BackLayoutGv(void *self);
void shapeAllViews__10BackLayoutGv(void *self);
void unshapeRectangles__10BackLayoutGP10XRectangleiP7ImdView(
    void *self, XRectangle *rects, int count, void *view);

/*
 * 0x10017860, size 80
 *
 * The original makes these as virtual calls through BackLayout's vtable
 * (slots resolving to unshapeView and shapeView in this specimen). Direct
 * calls are used here until the recovered class/vtable declaration exists.
 */
void viewAreaChanged__10BackLayoutGR7ImdView(void *self, void *view)
{
    unshapeView__10BackLayoutGR7ImdView(self, view);
    shapeView__10BackLayoutGR7ImdView(self, view);
}

/*
 * 0x100161c0, size 456
 *
 * +0x04 in BackLayout is the background ImdViewPanel widget.
 * +0x04/+0x08 in ImdView are the logical x/y coordinates used here.
 * Widget core offsets observed in the generated code:
 *   +0x58 -> Screen *, Screen +0x04 -> Display *
 *   +0x60 -> Window
 *
 * When pretty-shaping is disabled, the renderer supplies a rectangular
 * extent and fm unions that rectangle into the widget's ShapeBounding region.
 * With pretty-shaping enabled, the renderer draws the detailed shape using a
 * temporary Xt GC instead.
 */
void shapeView__10BackLayoutGR7ImdView(void *self, void *view)
{
    if (!load8(self, 0x4c))
        return;

    /* The high bit at ImdView +0x14 gates participation in background shape. */
    if (((*(uint32_t *)((unsigned char *)view + 0x14)) >> 31) == 0)
        return;

    void *panel = loadp32(self, 0x04);
    void *screen = loadp32(panel, 0x58);
    void *display = loadp32(screen, 0x04);
    unsigned long window = (uint32_t)(uintptr_t)loadp32(panel, 0x60);
    void *renderer = ImdViewPanelGetRenderer__GP10_WidgetRec(panel);

    long x = 0;
    long y = 0;
    long logicalX = *(int32_t *)((unsigned char *)view + 0x04);
    long logicalY = *(int32_t *)((unsigned char *)view + 0x08);
    ImdViewPanelPointToX__GP10_WidgetReclT2RlT4(panel, logicalX, logicalY, x, y);

    if (load8(self, 0x4d)) {
        void *gc = XtGetGC(panel, 0, 0);
        callRendererPrettyShape(renderer, view, x, y, display, window, gc);
        XtReleaseGC(panel, gc);
        return;
    }

    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
    callRendererBounds(renderer, view, x, y, &left, &top, &right, &bottom);

    XRectangle rect;
    rect.x = (int16_t)left;
    rect.y = (int16_t)top;
    rect.width = (uint16_t)(right - left);
    rect.height = (uint16_t)(bottom - top);

    /* ShapeBounding, ShapeUnion, YXBanded. */
    XShapeCombineRectangles(display, window, 0, 0, 0,
                            &rect, 1, 1, 3);
}

/*
 * 0x10016c08, size 264
 *
 * Recover the current renderer extent for the view, convert it to one
 * XRectangle, then dispatch BackLayout::unshapeRectangles(rect, 1, &view)
 * through the original virtual slot. Direct call here preserves the body.
 */
void unshapeView__10BackLayoutGR7ImdView(void *self, void *view)
{
    if (!load8(self, 0x4c))
        return;

    void *panel = loadp32(self, 0x04);
    void *renderer = ImdViewPanelGetRenderer__GP10_WidgetRec(panel);

    long x = 0;
    long y = 0;
    long logicalX = *(int32_t *)((unsigned char *)view + 0x04);
    long logicalY = *(int32_t *)((unsigned char *)view + 0x08);
    ImdViewPanelPointToX__GP10_WidgetReclT2RlT4(panel, logicalX, logicalY, x, y);

    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
    callRendererBounds(renderer, view, x, y, &left, &top, &right, &bottom);

    XRectangle rect;
    rect.x = (int16_t)left;
    rect.y = (int16_t)top;
    rect.width = (uint16_t)(right - left);
    rect.height = (uint16_t)(bottom - top);

    unshapeRectangles__10BackLayoutGP10XRectangleiP7ImdView(
        self, &rect, 1, view);
}

/*
 * 0x1001f908, size 300
 *
 * The background ViewPanel is temporarily unmapped. Its bounding shape is
 * first set to an empty region, then each current view is unioned back into
 * it by shapeView(). Finally the iterator is destroyed and the panel remaps.
 */
void shapeAllViews__10BackLayoutGv(void *self)
{
    if (!load8(self, 0x4c))
        return;

    void *panel = loadp32(self, 0x04);
    void *screen = loadp32(panel, 0x58);
    void *display = loadp32(screen, 0x04);
    unsigned long window = (uint32_t)(uintptr_t)loadp32(panel, 0x60);

    XUnmapWindow(display, window);

    /* ShapeBounding + ShapeSet with zero rectangles => empty bounding shape. */
    XShapeCombineRectangles(display, window, 0, 0, 0,
                            0, 0, 0, 3);

    void *iterator = ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(
        panel, 0);
    void *view = ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, iterator, 0);

    while (view) {
        shapeView__10BackLayoutGR7ImdView(self, view);
        view = ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
            panel, iterator, 0);
    }

    ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, iterator, 0);
    XMapWindow(display, window);
}

/*
 * 0x1001fa34, size 84
 *
 * XShapeCombineMask(..., ShapeBounding, 0, 0, None, ShapeSet) restores the
 * window's default rectangular bounding shape.
 */
void unshapeAllViews__10BackLayoutGv(void *self)
{
    void *panel = loadp32(self, 0x04);
    void *screen = loadp32(panel, 0x58);
    void *display = loadp32(screen, 0x04);
    unsigned long window = (uint32_t)(uintptr_t)loadp32(panel, 0x60);

    XShapeCombineMask(display, window, 0, 0, 0, 0, 0);
}

/*
 * 0x100250d8, size 104
 * flag at +0x4c == use-shapes state.
 * vtable +0x14c -> unshapeAllViews
 * vtable +0x158 -> shapeAllViews
 */
void useShapes__10BackLayoutGb(void *self, unsigned char enable)
{
    if (load8(self, 0x4c) == enable)
        return;

    store8(self, 0x4c, enable);
    if (enable)
        shapeAllViews__10BackLayoutGv(self);
    else
        unshapeAllViews__10BackLayoutGv(self);
}

/*
 * 0x100253e4, size 112
 * flag at +0x4d controls the "pretty" shape mode. It only triggers a
 * rebuild while shapes are enabled (+0x4c != 0).
 */
void shapePretty__10BackLayoutGb(void *self, unsigned char enable)
{
    if (load8(self, 0x4d) == enable)
        return;

    if (!load8(self, 0x4c))
        return;

    store8(self, 0x4d, enable);
    unshapeAllViews__10BackLayoutGv(self);
    shapeAllViews__10BackLayoutGv(self);
}

} /* extern "C" */
