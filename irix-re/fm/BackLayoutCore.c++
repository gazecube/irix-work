/*
 * BackLayoutCore.c++ - IRIX 6.5 fm BackLayout core recovery
 *
 * Binary: /usr/sbin/fm (uploaded specimen)
 * ELF: 32-bit MSB MIPS N32/MIPS-III
 *
 * This file records exact control flow and field observations from the named
 * MIPS functions while the original class declarations are still unavailable.
 * Unknown virtual calls are deliberately left as opaque helpers instead of
 * being assigned guessed source-level names.
 */

#include <stdint.h>
#include <stddef.h>

static inline uint8_t load8(const void *p, unsigned off)
{
    return *((const uint8_t *)p + off);
}

static inline void store8(void *p, unsigned off, uint8_t v)
{
    *((uint8_t *)p + off) = v;
}

static inline int32_t load32(const void *p, unsigned off)
{
    return *(const int32_t *)((const uint8_t *)p + off);
}

static inline void store32(void *p, unsigned off, int32_t v)
{
    *(int32_t *)((uint8_t *)p + off) = v;
}

static inline void *loadp32(const void *p, unsigned off)
{
    return (void *)(uintptr_t)*(const uint32_t *)((const uint8_t *)p + off);
}

extern "C" {

/* Imported functions resolved from the MIPS GOT. */
void __ct__13ImdIconLayoutGP10_WidgetRec(void *self, void *panel);
void addView__13ImdIconLayoutGR7ImdView(void *self, void *view);
void removeView__13ImdIconLayoutGR7ImdView(void *self, void *view);
void moveView__13ImdIconLayoutGR7ImdViewlT2(void *self, void *view,
                                            long x, long y);
void *ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(void *panel,
                                                           void *start);
void ImdViewPanelResetIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *panel, void *iterator, void *start);
void *ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *panel, void *iterator, void *start);
void ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *panel, void *iterator, void *start);
void ImdViewPanelUpdateLayout__GP10_WidgetRec(void *panel);
void ImdViewPanelSizeFromX__GP10_WidgetReclT2RlT4(void *panel,
                                                  long w, long h,
                                                  long &, long &);
void ImdViewPanelSetSize__GP10_WidgetReclT2(void *panel, long w, long h);
void XtGetApplicationResources(void *widget, void *base, void *resources,
                               unsigned num_resources, void *args,
                               unsigned num_args);

void shapeView__10BackLayoutGR7ImdView(void *self, void *view);
void unshapeView__10BackLayoutGR7ImdView(void *self, void *view);

/*
 * Opaque virtual calls whose exact source-level names are not assigned yet.
 * The offsets are preserved because they are directly observed in fm.
 */
typedef void (*Virtual1Fn)(void *, int);

static void call_vtbl_060_068(void *self, int arg)
{
    uint32_t *vtbl = (uint32_t *)loadp32(self, 0x08);
    intptr_t adjust = (int32_t)vtbl[0x60 / 4];
    Virtual1Fn fn = (Virtual1Fn)(uintptr_t)vtbl[0x68 / 4];
    fn((uint8_t *)self + adjust, arg);
}

/*
 * 0x1001f5cc, size 448
 * __ct__10BackLayoutGP10_WidgetRec
 *
 * Confirmed initialization performed after ImdIconLayout(panel):
 *   +0x08 = BackLayout vtable
 *   +0x48 = heap object of size 12, constructed as an ImdRealCollection
 *   +0x4c = 0        useShapes
 *   +0x4d = 1        shapePretty
 *   +0x58 = 0
 *   +0x59 = 0
 *   +0x45 = 0
 *   +0x50 = panel->core.height (16-bit field at panel +0x22)
 *   +0x24 is rounded up to even if necessary
 *   +0x20 is increased by (+0x24 / 2)
 *   +0x54 = floating default recovered from .rodata
 *
 * The tail calls XtGetApplicationResources() with the BackLayout resource
 * table. The individual resource names/defaults are being correlated against
 * the FileManager app-defaults before assigning field names to +0x20..+0x44.
 */
void recover_BackLayout_ctor_observed_fields(void *self, void *panel,
                                             void *collection)
{
    /* This helper is a source-level record of the observed field writes; the
       original constructor also invokes the base ctor and resource loader. */
    (void)panel;

    *(uint32_t *)((uint8_t *)self + 0x48) = (uint32_t)(uintptr_t)collection;
    store8(self, 0x4c, 0);
    store8(self, 0x4d, 1);
    store8(self, 0x58, 0);
    store8(self, 0x59, 0);
    store8(self, 0x45, 0);

    uint32_t step = *(uint32_t *)((uint8_t *)self + 0x24);
    if (step & 1)
        ++step;
    *(uint32_t *)((uint8_t *)self + 0x24) = step;
    *(int32_t *)((uint8_t *)self + 0x20) += (int32_t)(step / 2);
}

/*
 * 0x1001ec7c, size 180
 * addView__10BackLayoutGR7ImdView
 *
 * Exact high-level flow:
 *   1. call ImdIconLayout::addView(view)
 *   2. inspect ImdView +0x14 bits 30..31
 *   3. if either is set, shapeView(view)
 *      otherwise append the view to the collection at BackLayout +0x48
 *   4. if BackLayout +0x59 was set, mark the global FmApp byte at +0x2c,
 *      then clear +0x59
 */
void addView__10BackLayoutGR7ImdView(void *self, void *view)
{
    addView__13ImdIconLayoutGR7ImdView(self, view);

    uint32_t flags = *(uint32_t *)((uint8_t *)view + 0x14);
    if ((flags >> 30) & 0x3) {
        shapeView__10BackLayoutGR7ImdView(self, view);
    } else {
        /*
         * The original invokes the collection's virtual append-like slot:
         * object = *(self + 0x48), adjustment @ vtbl+0x24,
         * function @ vtbl+0x2c, argument = view.
         * Kept opaque until ImdRealCollection's class surface is mapped.
         */
        void *collection = loadp32(self, 0x48);
        uint32_t *vtbl = (uint32_t *)loadp32(collection, 0x00);
        intptr_t adjust = (int32_t)vtbl[0x24 / 4];
        typedef void (*AppendLikeFn)(void *, void *);
        AppendLikeFn fn = (AppendLikeFn)(uintptr_t)vtbl[0x2c / 4];
        fn((uint8_t *)collection + adjust, view);
    }

    /* Global-dirty side effect deliberately not modeled here until the
       owning global object is named with confidence. */
    if (load8(self, 0x59))
        store8(self, 0x59, 0);
}

/*
 * 0x10025638, size 136
 * moveView__10BackLayoutGR7ImdViewlT2
 *
 * This function is extremely clean in the binary:
 *   unshapeView(view)
 *   result = ImdIconLayout::moveView(view, x, y)
 *   shapeView(view)
 *   return low byte of result
 */
unsigned char moveView__10BackLayoutGR7ImdViewlT2(void *self, void *view,
                                                   long x, long y)
{
    unshapeView__10BackLayoutGR7ImdView(self, view);
    unsigned long result =
        (unsigned long)(uintptr_t)moveView__13ImdIconLayoutGR7ImdViewlT2;

    /* Keep the real base call explicit; its ABI return is used as a boolean. */
    typedef unsigned long (*MoveFn)(void *, void *, long, long);
    MoveFn fn = (MoveFn)(uintptr_t)moveView__13ImdIconLayoutGR7ImdViewlT2;
    result = fn(self, view, x, y);

    shapeView__10BackLayoutGR7ImdView(self, view);
    return (unsigned char)(result & 0xff);
}

/*
 * 0x100254fc, size 316
 * doLayout__10BackLayoutGv
 *
 * Iterator choreography is exact:
 *   iterator = CreateIterator(panel, 0)
 *   ResetIterator(panel, iterator, 0)
 *   view = GetNext(panel, iterator, 0)
 *
 * If no first view exists, the iterator is destroyed and the function exits.
 * Otherwise the iterator is reset again and all returned views have bit 30 of
 * ImdView +0x14 cleared (mask 0xbfffffff). The method then invokes virtual
 * slot +0x60/+0x68 with argument 1.
 *
 * If the initial GetNext returned null, fm calls DestroyIterator immediately.
 * The exact distinction between the two iterator paths is preserved here.
 */
void doLayout__10BackLayoutGv(void *self)
{
    void *panel = loadp32(self, 0x04);
    void *iterator = ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(
        panel, 0);

    ImdViewPanelResetIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, iterator, 0);

    void *view = ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, iterator, 0);

    if (!view) {
        ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
            panel, iterator, 0);
        return;
    }

    ImdViewPanelResetIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, iterator, 0);

    while ((view = ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
                panel, iterator, 0)) != 0) {
        uint32_t flags = *(uint32_t *)((uint8_t *)view + 0x14);
        flags &= 0xbfffffffU; /* clear bit 30 */
        *(uint32_t *)((uint8_t *)view + 0x14) = flags;
    }

    call_vtbl_060_068(self, 1);
}

} /* extern "C" */
