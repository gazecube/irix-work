/*
 * BackLayoutPlacement.c++ - first-pass placement reconstruction from IRIX 6.5 fm
 *
 * Binary: /usr/sbin/fm (uploaded specimen)
 * ELF: 32-bit MSB MIPS N32/MIPS-III
 *
 * Provenance:
 *   findHoles__10BackLayoutGiR11ImdViewListT2
 *     address 0x1002ee50, size 700 bytes
 *
 * This is a body-level reconstruction from the exact MIPS range. Unknown
 * class declarations are still represented by byte offsets. The core scan,
 * overlap test, coordinate generation, and ImdView allocation are directly
 * evidenced; names for several fields remain provisional.
 */

#include <stdint.h>
#include <stddef.h>

static inline int32_t load32(const void *p, unsigned off)
{
    return *(const int32_t *)((const unsigned char *)p + off);
}

static inline float loadf32(const void *p, unsigned off)
{
    return *(const float *)((const unsigned char *)p + off);
}

static inline void store32(void *p, unsigned off, int32_t v)
{
    *(int32_t *)((unsigned char *)p + off) = v;
}

extern "C" {

/* fm local/global symbol evidence. */
extern void *theBgWin;

/* Imports from the IMD libraries. */
void *ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(void *, void *);
void *ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *, void *, void *);
void ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
    void *, void *, void *);
void *__nw__GUi(unsigned int);
void __ct__7ImdViewGv(void *);
void append__11ImdViewListGP7ImdView(void *, void *);

/* Local fm helper, recovered separately at 0x1002ecc0. */
int isOverlapping__10BackLayoutGlN31R11ImdViewList(
    void *, long, long, long, long, void *);

/*
 * 0x1002ee50, size 700
 *
 * Call-site evidence in BackLayout::update() establishes the arguments as:
 *   count     = number of views requiring automatic placement
 *   holesOut  = temporary ImdViewList receiving generated positions
 *   occupied  = ImdViewList containing already-positioned views
 *
 * Each generated hole is represented as a freshly constructed ImdView whose
 * +0x04/+0x08 fields hold x/y. BackLayout::update() later copies those x/y
 * values onto the real views needing placement.
 *
 * Confirmed layout fields used here:
 *   self +0x04 : background ImdViewPanel widget
 *   self +0x20 : horizontal inset/step component
 *   self +0x28 : vertical spacing component
 *   self +0x2c : starting/minimum Y
 *   self +0x30 : bottom inset
 *   self +0x38 : right inset
 *   self +0x40 : right/reference edge
 *   self +0x50 : bottom/reference edge
 *   self +0x54 : logical/X coordinate scale
 *
 * ImdView geometry used by the overlap helper:
 *   +0x04 x, +0x08 y, +0x0c width, +0x10 height
 *
 * The exact cfront control flow has two wrap/scan-state variables. The source
 * below preserves the observed two-pass right-to-left fallback structure,
 * but field semantics and variable names remain provisional until the
 * surrounding BackLayout class is recovered.
 */
void findHoles__10BackLayoutGiR11ImdViewListT2(
    void *self, int count, void *holesOut, void *occupied)
{
    void *panel = *(void **)((unsigned char *)self + 0x04);

    /* fm samples one current view to obtain the standard view height. */
    void *it = ImdViewPanelCreateIterator__GP10_WidgetRecP7ImdView(panel, 0);
    void *sample =
        ImdViewPanelGetNext__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
            panel, it, 0);
    ImdViewPanelDestroyIterator__GP10_WidgetRecP20ImdViewPanelIteratorP7ImdView(
        panel, it, 0);

    /*
     * MIPS sequence:
     *   (sample->height + self[0x28]) * theBgWin[0x28]
     * followed by round-up-to-even.
     */
    int32_t step =
        (int32_t)((load32(sample, 0x10) + load32(self, 0x28)) *
                  loadf32(theBgWin, 0x28));
    if (step & 1)
        ++step;

    int32_t x =
        load32(self, 0x40) - load32(self, 0x38) - load32(self, 0x20);
    int32_t y = load32(self, 0x2c);

    int wraps = 0;
    bool fallbackPass = false;

    for (int i = 0; i < count; ++i) {
        bool found = false;
        bool scanPrimary = true;

        while (!found) {
            if (!fallbackPass && scanPrimary) {
                const int32_t bottomLimit =
                    load32(self, 0x50) - load32(self, 0x30);

                if (bottomLimit < y + step) {
                    x -= 2 * load32(self, 0x20);

                    if (x < load32(self, 0x20)) {
                        ++wraps;
                        const int32_t right =
                            load32(self, 0x40) - load32(self, 0x38);

                        if (wraps >= 2) {
                            x = right;
                            fallbackPass = true;
                            scanPrimary = false;
                        } else {
                            x = right - step;
                        }
                    }

                    y = load32(self, 0x2c);
                }
            }

            const float scale = loadf32(self, 0x54);
            const int32_t logicalY = (int32_t)((float)y / scale);
            const int32_t logicalX = (int32_t)((float)x / scale);
            const int32_t sampleHeight = load32(sample, 0x10);

            /*
             * Exact candidate rectangle passed by fm:
             *   left   = logicalX - 25
             *   top    = logicalY
             *   right  = logicalX + 25
             *   bottom = logicalY + sample->height
             */
            found = !isOverlapping__10BackLayoutGlN31R11ImdViewList(
                self,
                logicalX - 25,
                logicalY,
                logicalX + 25,
                logicalY + sampleHeight,
                occupied);

            if (!found) {
                y += step;
                continue;
            }

            /* A hole is materialized as a 0x20-byte ImdView object. */
            void *hole = __nw__GUi(0x20);
            if (hole)
                __ct__7ImdViewGv(hole);

            store32(hole, 0x04, x);
            store32(hole, 0x08, y);
            append__11ImdViewListGP7ImdView(holesOut, hole);

            fallbackPass = false;
            y += step;
        }
    }
}

} /* extern "C" */
