/*
 * BackLayoutGrid.c++ - focused recovery notes/body from IRIX 6.5 fm
 *
 * snapViewToGrid__10BackLayoutGR7ImdViewRlT2
 *   address 0x10024ee8, size 496
 *
 * This is kept separate from BackLayout.c++ until the remaining global
 * constants and class field names are resolved.
 */

#include <stdint.h>

static inline uint32_t load32(const void *p, unsigned off)
{ return *(const uint32_t *)((const unsigned char *)p + off); }
static inline float loadf(const void *p, unsigned off)
{ return *(const float *)((const unsigned char *)p + off); }

extern "C" {

/*
 * Exact arithmetic/control flow recovered from MIPS, with two unresolved
 * global float inputs exposed as parameters instead of guessed constants.
 *
 * Confirmed BackLayout fields used here:
 *   +0x20 horizontal step
 *   +0x28 logical offset added to view+0x10
 *   +0x2c minimum Y
 *   +0x34 minimum X
 *   +0x38 right-side inset
 *   +0x40 right edge/reference
 *   +0x54 scale
 *
 * Confirmed ImdView fields used here:
 *   +0x0c width-like extent used in final X centering
 *   +0x10 extent/metric used to derive the vertical grid interval
 */
void snapViewToGrid_core_10024ee8(void *self, void *view,
                                  int32_t *x, int32_t *y,
                                  float globalGridScale,
                                  float yBiasScale)
{
    int32_t grid = (int32_t)(globalGridScale *
        (float)(*(int32_t *)((unsigned char *)view + 0x10) +
                (int32_t)load32(self, 0x28)));

    if (grid & 1)
        ++grid;

    float scale = loadf(self, 0x54);

    int32_t sy = (int32_t)((float)*y * scale);
    *y = sy;

    int32_t min_y = (int32_t)load32(self, 0x2c);
    int32_t half_grid = grid / 2;

    if (sy <= min_y) {
        sy = min_y;
    } else {
        int32_t below = sy - min_y;
        int32_t base = sy - (below % half_grid);
        int32_t upper = base + half_grid;
        sy = ((upper - sy) < (sy - base)) ? upper : base;
    }

    *y = (int32_t)((float)sy / scale);

    int32_t sx = (int32_t)(
        (float)*x * scale +
        ((float)*(int32_t *)((unsigned char *)view + 0x0c) *
         scale * yBiasScale));
    *x = sx;

    int32_t min_x = (int32_t)load32(self, 0x34);
    if (sx < min_x) {
        sx = min_x;
    } else {
        int32_t right = (int32_t)load32(self, 0x40);
        int32_t inset = (int32_t)load32(self, 0x38);
        int32_t step = (int32_t)load32(self, 0x20);
        int32_t avail = right - sx - inset;
        int32_t q = avail / step;
        int32_t r = avail % step;
        if ((r + r) >= step)
            ++q;
        sx = right - (q * step + inset);
    }

    *x = (int32_t)((float)sx / scale -
        (float)(*(int32_t *)((unsigned char *)view + 0x0c) / 2));
}

} /* extern "C" */
