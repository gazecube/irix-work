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

static inline unsigned char load8(const void *p, unsigned off)
{
    return *((const unsigned char *)p + off);
}

static inline void store8(void *p, unsigned off, unsigned char v)
{
    *((unsigned char *)p + off) = v;
}

extern "C" {

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
