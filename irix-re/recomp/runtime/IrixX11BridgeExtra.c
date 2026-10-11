#include "IrixGuest.h"
#include "IrixX11BridgeExtraAccess.h"
#include "IrixX11WindowBridgeAccess.h"

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#ifdef IRIX_HAVE_XEXT
#include <X11/extensions/shape.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_MAX_DISPLAYS 16u
#define IRIX_X11_DISPLAY_BASE 0x72400000u
#define IRIX_X11_DISPLAY_STRIDE 0x00001000u
#define IRIX_X11_SCREEN_BASE 0x72440000u
#define IRIX_X11_SCREEN_STRIDE 0x00001000u
#define IRIX_X11_VISUAL_BASE 0x72480000u
#define IRIX_X11_VISUAL_STRIDE 0x00001000u
#define IRIX_X11_DISPLAY_SHADOW_SIZE 0x00100000u
#define IRIX_X11_MAX_GCS 1024u
#define IRIX_X11_GC_BASE 0x72500000u
#define IRIX_X11_GC_STRIDE 128u
#define IRIX_X11_GC_SHADOW_SIZE (IRIX_X11_MAX_GCS * IRIX_X11_GC_STRIDE)
#define IRIX_X11_TMP_BASE 0x72600000u
#define IRIX_X11_TMP_SIZE 0x00100000u
#define IRIX_X11_MAX_FONTS 256u

typedef struct {
    IrixGuestRuntime *rt;
    Display *displays[IRIX_X11_MAX_DISPLAYS];
    IrixGuestAddr display_free_funcs[IRIX_X11_MAX_DISPLAYS];
    size_t display_count;
    GC gcs[IRIX_X11_MAX_GCS];
    size_t gc_count;
    XFontStruct *fonts[IRIX_X11_MAX_FONTS];
    IrixGuestAddr font_guest[IRIX_X11_MAX_FONTS];
    size_t font_count;
    IrixGuestAddr tmp_cur;
} IrixX11ExtraState;

static IrixX11ExtraState g_x11;

static IrixGuestAddr gc_to_guest(GC gc);

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || g_x11.tmp_cur + n < g_x11.tmp_cur ||
        g_x11.tmp_cur + n > IRIX_X11_TMP_BASE + IRIX_X11_TMP_SIZE)
        return 0;
    a = g_x11.tmp_cur;
    g_x11.tmp_cur += n;
    return a;
}

static IrixGuestAddr copy_host_string(const char *s)
{
    IrixGuestAddr a;
    size_t n;
    void *p;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    a = tmp_alloc((uint32_t)n);
    if (!a) return 0;
    p = irix_guest_ptr(g_x11.rt, a, n);
    if (!p) return 0;
    memcpy(p, s, n);
    return a;
}

static IrixGuestAddr display_guest_addr(size_t i)
{
    return IRIX_X11_DISPLAY_BASE + (IrixGuestAddr)(i * IRIX_X11_DISPLAY_STRIDE);
}

static Display *display_from_guest(IrixGuestAddr addr)
{
    uint32_t off, i;
    if (addr < IRIX_X11_DISPLAY_BASE || addr >= IRIX_X11_SCREEN_BASE) return 0;
    off = addr - IRIX_X11_DISPLAY_BASE;
    if (off % IRIX_X11_DISPLAY_STRIDE) return 0;
    i = off / IRIX_X11_DISPLAY_STRIDE;
    return i < g_x11.display_count ? g_x11.displays[i] : 0;
}

static void write_visual_shadow(IrixGuestRuntime *rt, IrixGuestAddr a, Visual *v)
{
    if (!a || !v) return;
    wr32(rt, a + 0, 0);
    wr32(rt, a + 4, (uint32_t)v->visualid);
    wr32(rt, a + 8, (uint32_t)v->class);
    wr32(rt, a + 12, (uint32_t)v->red_mask);
    wr32(rt, a + 16, (uint32_t)v->green_mask);
    wr32(rt, a + 20, (uint32_t)v->blue_mask);
    wr32(rt, a + 24, (uint32_t)v->bits_per_rgb);
    wr32(rt, a + 28, (uint32_t)v->map_entries);
}

static void write_xgcvalues(IrixGuestRuntime *rt, IrixGuestAddr a, const XGCValues *v)
{
    if (!a || !v) return;
    wr32(rt, a + 0u, (uint32_t)v->function);
    wr32(rt, a + 4u, (uint32_t)v->plane_mask);
    wr32(rt, a + 8u, (uint32_t)v->foreground);
    wr32(rt, a + 12u, (uint32_t)v->background);
    wr32(rt, a + 16u, (uint32_t)v->line_width);
    wr32(rt, a + 20u, (uint32_t)v->line_style);
    wr32(rt, a + 24u, (uint32_t)v->cap_style);
    wr32(rt, a + 28u, (uint32_t)v->join_style);
    wr32(rt, a + 32u, (uint32_t)v->fill_style);
    wr32(rt, a + 36u, (uint32_t)v->fill_rule);
    wr32(rt, a + 40u, (uint32_t)v->arc_mode);
    wr32(rt, a + 44u, (uint32_t)v->tile);
    wr32(rt, a + 48u, (uint32_t)v->stipple);
    wr32(rt, a + 52u, (uint32_t)v->ts_x_origin);
    wr32(rt, a + 56u, (uint32_t)v->ts_y_origin);
    wr32(rt, a + 60u, (uint32_t)v->font);
    wr32(rt, a + 64u, (uint32_t)v->subwindow_mode);
    wr32(rt, a + 68u, (uint32_t)v->graphics_exposures);
    wr32(rt, a + 72u, (uint32_t)v->clip_x_origin);
    wr32(rt, a + 76u, (uint32_t)v->clip_y_origin);
    wr32(rt, a + 80u, (uint32_t)v->clip_mask);
    wr32(rt, a + 84u, (uint32_t)v->dash_offset);
    irix_guest_write8(rt, a + 88u, (uint8_t)v->dashes);
    irix_guest_write8(rt, a + 89u, 0);
    irix_guest_write8(rt, a + 90u, 0);
    irix_guest_write8(rt, a + 91u, 0);
}

static int read_xgcvalues(IrixGuestRuntime *rt, IrixGuestAddr a, XGCValues *v)
{
    if (!a || !v || !irix_guest_ptr(rt, a, 92u)) return 0;
    memset(v, 0, sizeof(*v));
    v->function = (int32_t)irix_guest_read32be(rt, a + 0u);
    v->plane_mask = (unsigned long)irix_guest_read32be(rt, a + 4u);
    v->foreground = (unsigned long)irix_guest_read32be(rt, a + 8u);
    v->background = (unsigned long)irix_guest_read32be(rt, a + 12u);
    v->line_width = (int32_t)irix_guest_read32be(rt, a + 16u);
    v->line_style = (int32_t)irix_guest_read32be(rt, a + 20u);
    v->cap_style = (int32_t)irix_guest_read32be(rt, a + 24u);
    v->join_style = (int32_t)irix_guest_read32be(rt, a + 28u);
    v->fill_style = (int32_t)irix_guest_read32be(rt, a + 32u);
    v->fill_rule = (int32_t)irix_guest_read32be(rt, a + 36u);
    v->arc_mode = (int32_t)irix_guest_read32be(rt, a + 40u);
    v->tile = (Pixmap)irix_guest_read32be(rt, a + 44u);
    v->stipple = (Pixmap)irix_guest_read32be(rt, a + 48u);
    v->ts_x_origin = (int32_t)irix_guest_read32be(rt, a + 52u);
    v->ts_y_origin = (int32_t)irix_guest_read32be(rt, a + 56u);
    v->font = (Font)irix_guest_read32be(rt, a + 60u);
    v->subwindow_mode = (int32_t)irix_guest_read32be(rt, a + 64u);
    v->graphics_exposures = (Bool)(int32_t)irix_guest_read32be(rt, a + 68u);
    v->clip_x_origin = (int32_t)irix_guest_read32be(rt, a + 72u);
    v->clip_y_origin = (int32_t)irix_guest_read32be(rt, a + 76u);
    v->clip_mask = (Pixmap)irix_guest_read32be(rt, a + 80u);
    v->dash_offset = (int32_t)irix_guest_read32be(rt, a + 84u);
    v->dashes = (char)irix_guest_read8(rt, a + 88u);
    return !rt->trapped;
}

static void sync_gc_shadow(size_t i)
{
    GC gc;
    IrixGuestAddr a;
    if (i >= g_x11.gc_count) return;
    gc = g_x11.gcs[i];
    if (!gc) return;
    a = IRIX_X11_GC_BASE + (IrixGuestAddr)(i * IRIX_X11_GC_STRIDE);
    wr32(g_x11.rt, a + 0u, 0u);
    wr32(g_x11.rt, a + 4u, (uint32_t)gc->gid);

    /*
     * Xlib's private GC contains host pointers (rects/dashes).  Never expose
     * those addresses through the 32-bit IRIX GC shadow: truncating a host
     * pointer creates a plausible-looking but unmapped guest pointer, and SGI
     * Xlib/Motif code is allowed to inspect the private GC prefix.
     *
     * We do not currently mirror host-side rectangle/dash backing stores into
     * guest memory, so advertise them as absent.  The public GC values below
     * remain synchronized through their scalar/XID representation.
     */
    wr32(g_x11.rt, a + 8u, 0u);
    wr32(g_x11.rt, a + 12u, 0u);
    wr32(g_x11.rt, a + 16u, (uint32_t)gc->dirty);
    write_xgcvalues(g_x11.rt, a + 20u, &gc->values);
}

static void sync_gc_by_ptr(GC gc)
{
    size_t i;
    if (!gc) return;
    for (i = 0; i < g_x11.gc_count; ++i)
        if (g_x11.gcs[i] == gc) {
            sync_gc_shadow(i);
            return;
        }
}

static void write_display_shadow(size_t i, Display *dpy)
{
    IrixGuestRuntime *rt = g_x11.rt;
    IrixGuestAddr d = display_guest_addr(i);
    IrixGuestAddr screens = IRIX_X11_SCREEN_BASE + (IrixGuestAddr)(i * IRIX_X11_SCREEN_STRIDE);
    IrixGuestAddr visuals = IRIX_X11_VISUAL_BASE + (IrixGuestAddr)(i * IRIX_X11_VISUAL_STRIDE);
    IrixGuestAddr vendor = copy_host_string(ServerVendor(dpy));
    IrixGuestAddr name = copy_host_string(DisplayString(dpy));
    IrixGuestAddr formats = 0;
    int ns = ScreenCount(dpy);
    int min_keycode = 0, max_keycode = 0;
    int nformats = 0;
    int s;
    XPixmapFormatValues *host_formats = XListPixmapFormats(dpy, &nformats);
    IrixGuestAddr free_funcs = g_x11.display_free_funcs[i];

    /*
     * Match the public IRIX Xlib.h _XDisplay prefix exactly. Old Xlib clients
     * are allowed to reach these fields through macros even though Display is
     * nominally opaque. In the IRIX/N32 layout motion_buffer/private16 live at
     * +144/+148 and min_keycode/max_keycode at +152/+156.
     */
    XDisplayKeycodes(dpy, &min_keycode, &max_keycode);

    /*
     * Xlib's public Xlib.h calls this slot private1, but Xlibint.h names it
     * free_funcs. Translated Xlib writes callback addresses into this table
     * during lazy initialization (for example modifiermap at +4). It must
     * therefore be real writable guest storage, not NULL and not a host
     * pointer.
     */
    if (!free_funcs) {
        free_funcs = tmp_alloc(64u);
        if (free_funcs) {
            void *p = irix_guest_ptr(rt, free_funcs, 64u);
            if (p) {
                memset(p, 0, 64u);
                g_x11.display_free_funcs[i] = free_funcs;
            } else {
                free_funcs = 0;
            }
        }
    }

    if (host_formats && nformats > 0) {
        uint64_t bytes = (uint64_t)(unsigned)nformats * 16u;
        if (bytes <= UINT32_MAX) {
            formats = tmp_alloc((uint32_t)bytes);
            if (formats) {
                int fi;
                for (fi = 0; fi < nformats; ++fi) {
                    IrixGuestAddr gf = formats + (IrixGuestAddr)fi * 16u;
                    wr32(rt, gf + 0u, 0u);
                    wr32(rt, gf + 4u, (uint32_t)host_formats[fi].depth);
                    wr32(rt, gf + 8u, (uint32_t)host_formats[fi].bits_per_pixel);
                    wr32(rt, gf + 12u, (uint32_t)host_formats[fi].scanline_pad);
                }
            }
        }
    }

    wr32(rt, d + 0, 0);
    wr32(rt, d + 4, free_funcs);
    wr32(rt, d + 8, (uint32_t)ConnectionNumber(dpy));
    wr32(rt, d + 12, 0);
    wr32(rt, d + 16, (uint32_t)ProtocolVersion(dpy));
    wr32(rt, d + 20, (uint32_t)ProtocolRevision(dpy));
    wr32(rt, d + 24, vendor);
    /* +28..+44 are XID/private allocator state. Do not fabricate host
       pointers; translated clients must not execute host resource_alloc. */
    wr32(rt, d + 28, 0u);
    wr32(rt, d + 32, 0u);
    wr32(rt, d + 36, 0u);
    wr32(rt, d + 40, 0u);
    wr32(rt, d + 44, 0u);
    wr32(rt, d + 48, (uint32_t)ImageByteOrder(dpy));
    wr32(rt, d + 52, (uint32_t)BitmapUnit(dpy));
    wr32(rt, d + 56, (uint32_t)BitmapPad(dpy));
    wr32(rt, d + 60, (uint32_t)BitmapBitOrder(dpy));
    wr32(rt, d + 64, (uint32_t)nformats);
    wr32(rt, d + 68, formats);
    wr32(rt, d + 72, 0u);
    wr32(rt, d + 76, (uint32_t)VendorRelease(dpy));
    wr32(rt, d + 80, 0u);
    wr32(rt, d + 84, 0u);
    wr32(rt, d + 88, (uint32_t)QLength(dpy));
    wr32(rt, d + 92, (uint32_t)LastKnownRequestProcessed(dpy));
    wr32(rt, d + 96, (uint32_t)(NextRequest(dpy) - 1));
    wr32(rt, d + 100, 0u);
    wr32(rt, d + 104, 0u);
    wr32(rt, d + 108, 0u);
    wr32(rt, d + 112, 0u);
    wr32(rt, d + 116, (uint32_t)XMaxRequestSize(dpy));
    wr32(rt, d + 120, 0u);
    wr32(rt, d + 124, 0u);
    wr32(rt, d + 128, name);
    wr32(rt, d + 132, (uint32_t)DefaultScreen(dpy));
    wr32(rt, d + 136, (uint32_t)ns);
    wr32(rt, d + 140, screens);
    wr32(rt, d + 144, (uint32_t)XDisplayMotionBufferSize(dpy));
    wr32(rt, d + 148, 0u);
    wr32(rt, d + 152, (uint32_t)min_keycode);
    wr32(rt, d + 156, (uint32_t)max_keycode);

    /*
     * +160 keysyms, +164 modifiermap, +168 keysyms_per_keycode and +172
     * xdefaults are guest Xlib-owned mutable state. Do not clear them while
     * refreshing the host-backed public shadow: _XKeyInitialize/InitModMap
     * populate these lazily and expect the values to survive bridge calls.
     */

    if (host_formats) XFree(host_formats);

    

    if (ns > 16) ns = 16;
    for (s = 0; s < ns; ++s) {
        Screen *hs = ScreenOfDisplay(dpy, s);
        Visual *hv = DefaultVisual(dpy, s);
        IrixGuestAddr gs = screens + (IrixGuestAddr)(s * 80u);
        IrixGuestAddr fallback_gv = visuals + (IrixGuestAddr)(s * 32u);
        IrixGuestAddr gv = 0u;
        IrixGuestAddr gdepths = 0u;
        int di;

        /*
         * Screen.depths is public Xlib ABI, not an opaque implementation
         * detail.  Motif/4Dwm walks this list when resolving visualType.
         * Advertising ndepths while leaving depths == NULL produced a
         * structurally impossible Screen and eventually a NULL visual
         * candidate.  Mirror the complete N32 Depth/Visual hierarchy.
         *
         * N32 Depth is { int depth; int nvisuals; Visual *visuals; } = 12
         * bytes and N32 Visual is 32 bytes.
         */
        if (hs->ndepths > 0 && hs->depths)
            gdepths = tmp_alloc((uint32_t)hs->ndepths * 12u);

        if (gdepths) {
            for (di = 0; di < hs->ndepths; ++di) {
                Depth *hd = &hs->depths[di];
                IrixGuestAddr gd = gdepths + (IrixGuestAddr)di * 12u;
                IrixGuestAddr gvis = 0u;
                int vi;

                if (hd->nvisuals > 0 && hd->visuals)
                    gvis = tmp_alloc((uint32_t)hd->nvisuals * 32u);

                wr32(rt, gd + 0u, (uint32_t)hd->depth);
                wr32(rt, gd + 4u, gvis ? (uint32_t)hd->nvisuals : 0u);
                wr32(rt, gd + 8u, gvis);

                if (!gvis)
                    continue;

                for (vi = 0; vi < hd->nvisuals; ++vi) {
                    Visual *v = &hd->visuals[vi];
                    IrixGuestAddr gvi = gvis + (IrixGuestAddr)vi * 32u;
                    write_visual_shadow(rt, gvi, v);
                    if (hv && !gv &&
                        (v == hv || v->visualid == hv->visualid))
                        gv = gvi;
                }
            }
        }

        /*
         * Keep a valid default visual even if the complete list could not be
         * allocated.  Normally gv points into Screen.depths so pointer
         * identity agrees with visual enumeration.
         */
        if (!gv) {
            gv = fallback_gv;
            write_visual_shadow(rt, gv, hv);
        }

        wr32(rt, gs + 0, 0);
        wr32(rt, gs + 4, d);
        wr32(rt, gs + 8, (uint32_t)hs->root);
        wr32(rt, gs + 12, (uint32_t)hs->width);
        wr32(rt, gs + 16, (uint32_t)hs->height);
        wr32(rt, gs + 20, (uint32_t)hs->mwidth);
        wr32(rt, gs + 24, (uint32_t)hs->mheight);
        wr32(rt, gs + 28, gdepths ? (uint32_t)hs->ndepths : 0u);
        wr32(rt, gs + 32, gdepths);
        wr32(rt, gs + 36, (uint32_t)hs->root_depth);
        wr32(rt, gs + 40, gv);
        wr32(rt, gs + 44, gc_to_guest(hs->default_gc));
        wr32(rt, gs + 48, (uint32_t)hs->cmap);
        wr32(rt, gs + 52, (uint32_t)hs->white_pixel);
        wr32(rt, gs + 56, (uint32_t)hs->black_pixel);
        wr32(rt, gs + 60, (uint32_t)hs->max_maps);
        wr32(rt, gs + 64, (uint32_t)hs->min_maps);
        wr32(rt, gs + 68, (uint32_t)hs->backing_store);
        wr32(rt, gs + 72, (uint32_t)hs->save_unders);
        wr32(rt, gs + 76, (uint32_t)hs->root_input_mask);

        
    }
}

static IrixGuestAddr display_to_guest(Display *dpy)
{
    size_t i;
    if (!dpy) return 0;
    for (i = 0; i < g_x11.display_count; ++i) {
        if (g_x11.displays[i] == dpy)
            return display_guest_addr(i);
    }
    if (g_x11.display_count >= IRIX_X11_MAX_DISPLAYS) return 0;
    i = g_x11.display_count++;
    g_x11.displays[i] = dpy;
    write_display_shadow(i, dpy);
    return display_guest_addr(i);
}

static GC gc_from_guest(IrixGuestAddr addr)
{
    uint32_t off, i;
    if (addr < IRIX_X11_GC_BASE || addr >= IRIX_X11_GC_BASE + IRIX_X11_GC_SHADOW_SIZE) return 0;
    off = addr - IRIX_X11_GC_BASE;
    if (off % IRIX_X11_GC_STRIDE) return 0;
    i = off / IRIX_X11_GC_STRIDE;
    return i < g_x11.gc_count ? g_x11.gcs[i] : 0;
}

static IrixGuestAddr gc_to_guest(GC gc)
{
    size_t i;
    size_t free_slot = IRIX_X11_MAX_GCS;

    if (!gc) return 0;

    for (i = 0; i < g_x11.gc_count; ++i) {
        if (g_x11.gcs[i] == gc) {
            sync_gc_shadow(i);
            return IRIX_X11_GC_BASE +
                   (IrixGuestAddr)(i * IRIX_X11_GC_STRIDE);
        }
        if (!g_x11.gcs[i] && free_slot == IRIX_X11_MAX_GCS)
            free_slot = i;
    }

    /*
     * XFreeGC clears g_x11.gcs[i], so recycle that guest shadow slot before
     * extending the high-water mark.  The old code only ever incremented
     * gc_count and therefore exhausted all 1024 handles during repeated Motif
     * redisplay even though many host GCs had already been freed.
     */
    if (free_slot != IRIX_X11_MAX_GCS) {
        i = free_slot;
    } else {
        if (g_x11.gc_count >= IRIX_X11_MAX_GCS)
            return 0;
        i = g_x11.gc_count++;
    }

    g_x11.gcs[i] = gc;
    sync_gc_shadow(i);
    return IRIX_X11_GC_BASE +
           (IrixGuestAddr)(i * IRIX_X11_GC_STRIDE);
}

static void write_char_struct(IrixGuestRuntime *rt, IrixGuestAddr a, const XCharStruct *c)
{
    if (!a || !c) return;
    irix_guest_write16be(rt, a + 0u, (uint16_t)c->lbearing);
    irix_guest_write16be(rt, a + 2u, (uint16_t)c->rbearing);
    irix_guest_write16be(rt, a + 4u, (uint16_t)c->width);
    irix_guest_write16be(rt, a + 6u, (uint16_t)c->ascent);
    irix_guest_write16be(rt, a + 8u, (uint16_t)c->descent);
    irix_guest_write16be(rt, a + 10u, c->attributes);
}

IrixGuestAddr irix_x11_primary_create_gc(IrixGuestRuntime *rt,
                                         IrixGuestAddr display_addr,
                                         uint32_t drawable,
                                         uint32_t mask,
                                         IrixGuestAddr values_addr)
{
    Display *d = display_from_guest(display_addr);
    XGCValues values;
    XGCValues *values_ptr = NULL;
    GC gc;

    if (!d || !drawable)
        return 0;
    if (mask) {
        if (!read_xgcvalues(rt, values_addr, &values))
            return 0;
        values_ptr = &values;
    }
    gc = XCreateGC(d, (Drawable)drawable, (unsigned long)mask, values_ptr);
    return gc_to_guest(gc);
}

int irix_x11_primary_free_gc(IrixGuestRuntime *rt,
                             IrixGuestAddr display_addr,
                             IrixGuestAddr guest_gc)
{
    Display *d = display_from_guest(display_addr);
    GC gc = gc_from_guest(guest_gc);
    uint32_t off, i;
    int rc;

    if (!d || !gc)
        return 0;
    rc = XFreeGC(d, gc);
    if (guest_gc >= IRIX_X11_GC_BASE &&
        guest_gc < IRIX_X11_GC_BASE + IRIX_X11_GC_SHADOW_SIZE) {
        off = guest_gc - IRIX_X11_GC_BASE;
        if ((off % IRIX_X11_GC_STRIDE) == 0u) {
            i = off / IRIX_X11_GC_STRIDE;
            if (i < g_x11.gc_count) {
                g_x11.gcs[i] = 0;
                if (irix_guest_ptr(rt, guest_gc, 112u))
                    memset(irix_guest_ptr(rt, guest_gc, 112u), 0, 112u);
            }
        }
    }
    return rc;
}

static IrixGuestAddr font_to_guest(IrixGuestRuntime *rt, XFontStruct *f)
{
    IrixGuestAddr out, props = 0, per = 0;
    size_t i;
    uint64_t rows, cols, nchar = 0;

    if (!f || g_x11.font_count >= IRIX_X11_MAX_FONTS) return 0;
    out = tmp_alloc(80u);
    if (!out) return 0;

    if (f->n_properties > 0 && f->properties) {
        uint64_t nbytes = (uint64_t)(unsigned)f->n_properties * 8u;
        if (nbytes > UINT32_MAX) return 0;
        props = tmp_alloc((uint32_t)nbytes);
        if (!props) return 0;
        for (i = 0; i < (size_t)f->n_properties; ++i) {
            wr32(rt, props + (IrixGuestAddr)i * 8u + 0u, (uint32_t)f->properties[i].name);
            wr32(rt, props + (IrixGuestAddr)i * 8u + 4u, (uint32_t)f->properties[i].card32);
        }
    }

    if (f->per_char) {
        cols = (uint64_t)f->max_char_or_byte2 - (uint64_t)f->min_char_or_byte2 + 1u;
        rows = (uint64_t)f->max_byte1 - (uint64_t)f->min_byte1 + 1u;
        if (f->min_byte1 == 0 && f->max_byte1 == 0) rows = 1u;
        nchar = rows * cols;
        if (nchar && nchar <= UINT32_MAX / 12u) {
            per = tmp_alloc((uint32_t)(nchar * 12u));
            if (!per) return 0;
            for (i = 0; i < (size_t)nchar; ++i)
                write_char_struct(rt, per + (IrixGuestAddr)i * 12u, &f->per_char[i]);
        }
    }

    wr32(rt, out + 0u, 0u);
    wr32(rt, out + 4u, (uint32_t)f->fid);
    wr32(rt, out + 8u, f->direction);
    wr32(rt, out + 12u, f->min_char_or_byte2);
    wr32(rt, out + 16u, f->max_char_or_byte2);
    wr32(rt, out + 20u, f->min_byte1);
    wr32(rt, out + 24u, f->max_byte1);
    wr32(rt, out + 28u, (uint32_t)f->all_chars_exist);
    wr32(rt, out + 32u, f->default_char);
    wr32(rt, out + 36u, (uint32_t)f->n_properties);
    wr32(rt, out + 40u, props);
    write_char_struct(rt, out + 44u, &f->min_bounds);
    write_char_struct(rt, out + 56u, &f->max_bounds);
    wr32(rt, out + 68u, per);
    wr32(rt, out + 72u, (uint32_t)f->ascent);
    wr32(rt, out + 76u, (uint32_t)f->descent);

    g_x11.fonts[g_x11.font_count] = f;
    g_x11.font_guest[g_x11.font_count] = out;
    ++g_x11.font_count;
    return out;
}

static void bridge_XLoadQueryFont(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    XFontStruct *f = 0;

    if (d && name && *name)
        f = XLoadQueryFont(d, name);

    /*
     * Deadline bring-up: 4Dwm can reach its font loader with an empty or
     * unavailable SGI font resource while the rest of the X resource stack is
     * still being reconstructed.  Fall back to the core X "fixed" font so the
     * WM can finish startup instead of exiting.  Keep the requested name in
     * the log so the real resource can be restored later.
     */
    if (d && !f) {
        fprintf(stderr,
                "IRIX XLoadQueryFont fallback: requested=%s%s%s -> fixed\n",
                name ? "\"" : "",
                name ? name : "<invalid>",
                name ? "\"" : "");
        f = XLoadQueryFont(d, "fixed");
    }

    cpu->gpr[2] = font_to_guest(rt, f);
}

/*
 * SGI-private Xlib preload hint.  IRIX applications pass a variadic list of
 * atom-name strings here before opening/initializing their display.  The host
 * Xlib has no matching API, and actual atom creation/lookup is already handled
 * by the normal XInternAtom/XInternAtoms bridges, so the preload hint can be
 * ignored without exposing host pointers or inventing guest atom IDs.
 */
static void bridge__XSGIAddToPreloadAtomList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
}

static int irix_host_xerror_continue(Display *dpy, XErrorEvent *ev)
{
    char msg[128];
    msg[0] = 0;
    if (dpy)
        XGetErrorText(dpy, ev->error_code, msg, (int)sizeof(msg));
    fprintf(stderr,
            "IRIX host X error ignored: code=%u request=%u minor=%u resource=%08lx serial=%lu%s%s\n",
            (unsigned)ev->error_code,
            (unsigned)ev->request_code,
            (unsigned)ev->minor_code,
            ev->resourceid,
            ev->serial,
            msg[0] ? " text=" : "",
            msg[0] ? msg : "");
    return 0;
}

static void bridge_XOpenDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    Display *dpy;
    IrixGuestAddr guest_display;

    

    dpy = XOpenDisplay(name);
    if (dpy)
        XSetErrorHandler(irix_host_xerror_continue);
    guest_display = display_to_guest(dpy);
    
    cpu->gpr[2] = guest_display;
}

static void bridge_XCloseDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCloseDisplay(d) : 0;
}

static void bridge_XDefaultScreen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultScreen(d) : 0;
}

static void bridge_XRootWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)RootWindow(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultRootWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultRootWindow(d) : 0;
}

static void bridge_XDisplayWidth(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DisplayWidth(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDisplayHeight(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DisplayHeight(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultDepth(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultDepth(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XBlackPixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)BlackPixel(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XWhitePixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)WhitePixel(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultColormap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultColormap(d, (int)cpu->gpr[5]) : 0;
}

/*
 * SGI OpenDis.c has an internal XKB display-initialization helper which
 * allocates/fills Display->xkb_info and then fetches the initial XkbDescRec.
 * Our guest Display is intentionally only a public/marshalable shadow; the
 * real host Display returned by XOpenDisplay already owns valid host XKB
 * private state.  Never run that SGI helper against the shadow.
 */
static void bridge__IrixXkbDisplayInit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? 1u : 0u;
}

static void bridge_XkbKeysymToModifiers(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    KeySym ks = (KeySym)(uint32_t)cpu->gpr[5];
    (void)rt;

    /*
     * SGI XkbKeysymToModifiers immediately dereferences Display->xkb_info.
     * The guest Display is only a public ABI shadow, so keep this lookup on
     * the real host Display where Xlib owns the private XKB state.
     */
    cpu->gpr[2] = d ? (uint32_t)XkbKeysymToModifiers(d, ks) : 0u;
}

static void bridge_XkbGetUpdatedMap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    unsigned int which = (unsigned int)cpu->gpr[5];
    XkbDescPtr desc = 0;

    /*
     * XkbGetUpdatedMap mutates an XkbDescRec containing several host pointers,
     * so a guest copy cannot safely be handed to host Xlib.  xwsh reaches this
     * entry with its own guest-side descriptor.  Let host Xlib fetch/update a
     * host descriptor, then keep the guest-side object untouched here; callers
     * that need XKB map contents are bridged separately.
     */
    (void)rt;
    if (d)
        desc = XkbGetMap(d, which, XkbUseCoreKbd);
    cpu->gpr[2] = desc ? Success : BadAlloc;
    if (desc)
        XkbFreeKeyboard(desc, XkbAllComponentsMask, True);
}

static void bridge_XkbSelectEventDetails(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Bool ok = False;
    (void)rt;

    if (d)
        ok = XkbSelectEventDetails(d,
            (unsigned int)cpu->gpr[5],
            (unsigned int)cpu->gpr[6],
            (unsigned long)(uint32_t)cpu->gpr[7],
            (unsigned long)(uint32_t)cpu->gpr[8]);

    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XkbQueryExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr opcode_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr event_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr error_addr = (uint32_t)cpu->gpr[7];
    IrixGuestAddr major_addr = (uint32_t)cpu->gpr[8];
    IrixGuestAddr minor_addr = (uint32_t)cpu->gpr[9];
    int opcode = 0, event = 0, error = 0, major = 0, minor = 0;
    Bool ok = False;

    if (d)
        ok = XkbQueryExtension(d, &opcode, &event, &error, &major, &minor);

    if (opcode_addr) wr32(rt, opcode_addr, (uint32_t)opcode);
    if (event_addr) wr32(rt, event_addr, (uint32_t)event);
    if (error_addr) wr32(rt, error_addr, (uint32_t)error);
    if (major_addr) wr32(rt, major_addr, (uint32_t)major);
    if (minor_addr) wr32(rt, minor_addr, (uint32_t)minor);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XkbUseExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr major_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr minor_addr = (uint32_t)cpu->gpr[6];
    int major = major_addr ? (int32_t)irix_guest_read32be(rt, major_addr) : 0;
    int minor = minor_addr ? (int32_t)irix_guest_read32be(rt, minor_addr) : 0;
    Bool ok = False;

    if (!rt->trapped && d)
        ok = XkbUseExtension(d, &major, &minor);

    if (!rt->trapped && major_addr)
        wr32(rt, major_addr, (uint32_t)major);
    if (!rt->trapped && minor_addr)
        wr32(rt, minor_addr, (uint32_t)minor);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static IrixGuestAddr copy_xextcodes(IrixGuestRuntime *rt, const XExtCodes *codes)
{
    IrixGuestAddr out;
    if (!codes) return 0;
    out = tmp_alloc(16u);
    if (!out) return 0;
    wr32(rt, out + 0u, (uint32_t)codes->extension);
    wr32(rt, out + 4u, (uint32_t)codes->major_opcode);
    wr32(rt, out + 8u, (uint32_t)codes->first_event);
    wr32(rt, out + 12u, (uint32_t)codes->first_error);
    return out;
}

static void bridge_XInitExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    XExtCodes *codes = (d && name) ? XInitExtension(d, name) : 0;
    cpu->gpr[2] = copy_xextcodes(rt, codes);
}

static void bridge_XAddExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    XExtCodes *codes = d ? XAddExtension(d) : 0;
    cpu->gpr[2] = copy_xextcodes(rt, codes);
}

static void bridge_XQueryExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    int major_opcode = 0, first_event = 0, first_error = 0;
    Bool ok = False;

    if (d && name)
        ok = XQueryExtension(d, name, &major_opcode, &first_event, &first_error);

    if ((uint32_t)cpu->gpr[6])
        wr32(rt, (uint32_t)cpu->gpr[6], (uint32_t)major_opcode);
    if ((uint32_t)cpu->gpr[7])
        wr32(rt, (uint32_t)cpu->gpr[7], (uint32_t)first_event);
    if ((uint32_t)cpu->gpr[8])
        wr32(rt, (uint32_t)cpu->gpr[8], (uint32_t)first_error);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XInternAtom(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (d && name) ? (uint32_t)XInternAtom(d, name, (Bool)cpu->gpr[6]) : 0;
}

static void bridge_XGetAtomName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    char *s = d ? XGetAtomName(d, (Atom)(uint32_t)cpu->gpr[5]) : 0;
    IrixGuestAddr out = 0;
    if (s) {
        size_t n = strlen(s) + 1u;
        out = tmp_alloc((uint32_t)n);
        if (out) {
            void *p = irix_guest_ptr(rt, out, n);
            if (p) memcpy(p, s, n); else out = 0;
        }
        XFree(s);
    }
    cpu->gpr[2] = out;
}

#define SIMPLE_DISPLAY0(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt; \
    cpu->gpr[2] = d ? (uint32_t)(call) : 0; }

static void bridge_XFlush(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_display = (uint32_t)cpu->gpr[4];
    Display *d = display_from_guest(guest_display);
    cpu->gpr[2] = d ? (uint32_t)XFlush(d)
                     : (uint32_t)irix_x11_window_flush_guest_display(rt,
                                                                      guest_display);
}

static void bridge_XSync(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_display = (uint32_t)cpu->gpr[4];
    Display *d = display_from_guest(guest_display);
    cpu->gpr[2] = d ? (uint32_t)XSync(d, (Bool)cpu->gpr[5])
                     : (uint32_t)irix_x11_window_sync_guest_display(rt, cpu,
                                                                     guest_display);
}

SIMPLE_DISPLAY0(bridge_XPending, XPending(d))
#define IRIX_XEVENT_SIZE 96u


/*
 * IRIX N32 XEvent uses 32-bit long/pointer fields.  Marshal the common
 * 24-byte header and the Key/Button/Motion payloads explicitly.
 */
static int write_xevent(IrixGuestRuntime *rt, IrixGuestAddr a, const XEvent *e)
{
    if (!a || !e || !irix_guest_ptr(rt, a, IRIX_XEVENT_SIZE)) return 0;
    memset(irix_guest_ptr(rt, a, IRIX_XEVENT_SIZE), 0, IRIX_XEVENT_SIZE);
    wr32(rt, a + 0u, (uint32_t)e->xany.type);
    wr32(rt, a + 4u, (uint32_t)e->xany.serial);
    wr32(rt, a + 8u, (uint32_t)e->xany.send_event);
    wr32(rt, a + 12u, display_to_guest(e->xany.display));
    wr32(rt, a + 16u, (uint32_t)e->xany.window);

    switch (e->type) {
    case KeyPress:
    case KeyRelease:
        wr32(rt, a + 20u, (uint32_t)e->xkey.root);
        wr32(rt, a + 24u, (uint32_t)e->xkey.subwindow);
        wr32(rt, a + 28u, (uint32_t)e->xkey.time);
        wr32(rt, a + 32u, (uint32_t)e->xkey.x);
        wr32(rt, a + 36u, (uint32_t)e->xkey.y);
        wr32(rt, a + 40u, (uint32_t)e->xkey.x_root);
        wr32(rt, a + 44u, (uint32_t)e->xkey.y_root);
        wr32(rt, a + 48u, (uint32_t)e->xkey.state);
        wr32(rt, a + 52u, (uint32_t)e->xkey.keycode);
        wr32(rt, a + 56u, (uint32_t)e->xkey.same_screen);
        break;
    case ButtonPress:
    case ButtonRelease:
        wr32(rt, a + 20u, (uint32_t)e->xbutton.root);
        wr32(rt, a + 24u, (uint32_t)e->xbutton.subwindow);
        wr32(rt, a + 28u, (uint32_t)e->xbutton.time);
        wr32(rt, a + 32u, (uint32_t)e->xbutton.x);
        wr32(rt, a + 36u, (uint32_t)e->xbutton.y);
        wr32(rt, a + 40u, (uint32_t)e->xbutton.x_root);
        wr32(rt, a + 44u, (uint32_t)e->xbutton.y_root);
        wr32(rt, a + 48u, (uint32_t)e->xbutton.state);
        wr32(rt, a + 52u, (uint32_t)e->xbutton.button);
        wr32(rt, a + 56u, (uint32_t)e->xbutton.same_screen);
        break;
    case MotionNotify:
        wr32(rt, a + 20u, (uint32_t)e->xmotion.root);
        wr32(rt, a + 24u, (uint32_t)e->xmotion.subwindow);
        wr32(rt, a + 28u, (uint32_t)e->xmotion.time);
        wr32(rt, a + 32u, (uint32_t)e->xmotion.x);
        wr32(rt, a + 36u, (uint32_t)e->xmotion.y);
        wr32(rt, a + 40u, (uint32_t)e->xmotion.x_root);
        wr32(rt, a + 44u, (uint32_t)e->xmotion.y_root);
        wr32(rt, a + 48u, (uint32_t)e->xmotion.state);
        irix_guest_write8(rt, a + 52u, (uint8_t)e->xmotion.is_hint);
        wr32(rt, a + 56u, (uint32_t)e->xmotion.same_screen);
        break;
    case EnterNotify:
    case LeaveNotify:
        wr32(rt, a + 20u, (uint32_t)e->xcrossing.root);
        wr32(rt, a + 24u, (uint32_t)e->xcrossing.subwindow);
        wr32(rt, a + 28u, (uint32_t)e->xcrossing.time);
        wr32(rt, a + 32u, (uint32_t)e->xcrossing.x);
        wr32(rt, a + 36u, (uint32_t)e->xcrossing.y);
        wr32(rt, a + 40u, (uint32_t)e->xcrossing.x_root);
        wr32(rt, a + 44u, (uint32_t)e->xcrossing.y_root);
        wr32(rt, a + 48u, (uint32_t)e->xcrossing.mode);
        wr32(rt, a + 52u, (uint32_t)e->xcrossing.detail);
        wr32(rt, a + 56u, (uint32_t)e->xcrossing.same_screen);
        wr32(rt, a + 60u, (uint32_t)e->xcrossing.focus);
        wr32(rt, a + 64u, (uint32_t)e->xcrossing.state);
        break;
    case FocusIn:
    case FocusOut:
        wr32(rt, a + 20u, (uint32_t)e->xfocus.mode);
        wr32(rt, a + 24u, (uint32_t)e->xfocus.detail);
        break;
    case Expose:
        wr32(rt, a + 20u, (uint32_t)e->xexpose.x);
        wr32(rt, a + 24u, (uint32_t)e->xexpose.y);
        wr32(rt, a + 28u, (uint32_t)e->xexpose.width);
        wr32(rt, a + 32u, (uint32_t)e->xexpose.height);
        wr32(rt, a + 36u, (uint32_t)e->xexpose.count);
        break;
    case CreateNotify:
        wr32(rt, a + 20u, (uint32_t)e->xcreatewindow.window);
        wr32(rt, a + 24u, (uint32_t)e->xcreatewindow.x);
        wr32(rt, a + 28u, (uint32_t)e->xcreatewindow.y);
        wr32(rt, a + 32u, (uint32_t)e->xcreatewindow.width);
        wr32(rt, a + 36u, (uint32_t)e->xcreatewindow.height);
        wr32(rt, a + 40u, (uint32_t)e->xcreatewindow.border_width);
        wr32(rt, a + 44u, (uint32_t)e->xcreatewindow.override_redirect);
        break;
    case DestroyNotify:
        wr32(rt, a + 20u, (uint32_t)e->xdestroywindow.window);
        break;
    case UnmapNotify:
        wr32(rt, a + 20u, (uint32_t)e->xunmap.window);
        wr32(rt, a + 24u, (uint32_t)e->xunmap.from_configure);
        break;
    case MapNotify:
        wr32(rt, a + 20u, (uint32_t)e->xmap.window);
        wr32(rt, a + 24u, (uint32_t)e->xmap.override_redirect);
        break;
    case MapRequest:
        /*
         * XMapRequestEvent aliases XAnyEvent.window with parent at +16.
         * The client which must be managed is the following N32 word.
         */
        wr32(rt, a + 20u, (uint32_t)e->xmaprequest.window);
        break;
    case ReparentNotify:
        wr32(rt, a + 20u, (uint32_t)e->xreparent.window);
        wr32(rt, a + 24u, (uint32_t)e->xreparent.parent);
        wr32(rt, a + 28u, (uint32_t)e->xreparent.x);
        wr32(rt, a + 32u, (uint32_t)e->xreparent.y);
        wr32(rt, a + 36u, (uint32_t)e->xreparent.override_redirect);
        break;
    case ConfigureNotify:
        /*
         * XConfigureEvent differs from XAnyEvent at +16: this slot is
         * 'event', followed by 'window'.  The common-header write above
         * already stores xany.window at +16, which aliases xconfigure.event.
         * Continue with window at +20; do not write event a second time or
         * every remaining field is shifted by four bytes.
         */
        wr32(rt, a + 20u, (uint32_t)e->xconfigure.window);
        wr32(rt, a + 24u, (uint32_t)e->xconfigure.x);
        wr32(rt, a + 28u, (uint32_t)e->xconfigure.y);
        wr32(rt, a + 32u, (uint32_t)e->xconfigure.width);
        wr32(rt, a + 36u, (uint32_t)e->xconfigure.height);
        wr32(rt, a + 40u, (uint32_t)e->xconfigure.border_width);
        wr32(rt, a + 44u, (uint32_t)e->xconfigure.above);
        wr32(rt, a + 48u, (uint32_t)e->xconfigure.override_redirect);
        break;
    case GravityNotify:
        wr32(rt, a + 20u, (uint32_t)e->xgravity.window);
        wr32(rt, a + 24u, (uint32_t)e->xgravity.x);
        wr32(rt, a + 28u, (uint32_t)e->xgravity.y);
        break;
    case ResizeRequest:
        wr32(rt, a + 20u, (uint32_t)e->xresizerequest.width);
        wr32(rt, a + 24u, (uint32_t)e->xresizerequest.height);
        break;
    case ConfigureRequest:
        wr32(rt, a + 20u, (uint32_t)e->xconfigurerequest.window);
        wr32(rt, a + 24u, (uint32_t)e->xconfigurerequest.x);
        wr32(rt, a + 28u, (uint32_t)e->xconfigurerequest.y);
        wr32(rt, a + 32u, (uint32_t)e->xconfigurerequest.width);
        wr32(rt, a + 36u, (uint32_t)e->xconfigurerequest.height);
        wr32(rt, a + 40u, (uint32_t)e->xconfigurerequest.border_width);
        wr32(rt, a + 44u, (uint32_t)e->xconfigurerequest.above);
        wr32(rt, a + 48u, (uint32_t)e->xconfigurerequest.detail);
        wr32(rt, a + 52u, (uint32_t)e->xconfigurerequest.value_mask);
        break;
    case CirculateNotify:
        wr32(rt, a + 20u, (uint32_t)e->xcirculate.window);
        wr32(rt, a + 24u, (uint32_t)e->xcirculate.place);
        break;
    case CirculateRequest:
        wr32(rt, a + 20u, (uint32_t)e->xcirculaterequest.window);
        wr32(rt, a + 24u, (uint32_t)e->xcirculaterequest.place);
        break;
    case PropertyNotify:
        wr32(rt, a + 20u, (uint32_t)e->xproperty.atom);
        wr32(rt, a + 24u, (uint32_t)e->xproperty.time);
        wr32(rt, a + 28u, (uint32_t)e->xproperty.state);
        break;
    case ColormapNotify:
        wr32(rt, a + 20u, (uint32_t)e->xcolormap.colormap);
        wr32(rt, a + 24u, (uint32_t)e->xcolormap.new);
        wr32(rt, a + 28u, (uint32_t)e->xcolormap.state);
        break;
    case VisibilityNotify:
        wr32(rt, a + 20u, (uint32_t)e->xvisibility.state);
        break;
    case ClientMessage:
        wr32(rt, a + 20u, (uint32_t)e->xclient.message_type);
        wr32(rt, a + 24u, (uint32_t)e->xclient.format);
        if (e->xclient.format == 8) {
            unsigned i;
            for (i = 0; i < 20u; ++i)
                irix_guest_write8(rt, a + 28u + i,
                                  (uint8_t)e->xclient.data.b[i]);
        } else if (e->xclient.format == 16) {
            unsigned i;
            for (i = 0; i < 10u; ++i)
                irix_guest_write16be(rt, a + 28u + i * 2u,
                                     (uint16_t)e->xclient.data.s[i]);
        } else if (e->xclient.format == 32) {
            unsigned i;
            for (i = 0; i < 5u; ++i)
                wr32(rt, a + 28u + i * 4u,
                     (uint32_t)e->xclient.data.l[i]);
        }
        break;
    default:
        break;
    }
    return !rt->trapped;
}

static int read_xkeyevent(IrixGuestRuntime *rt, IrixGuestAddr a, XKeyEvent *e)
{
    if (!a || !e || !irix_guest_ptr(rt, a, 60u)) return 0;
    memset(e, 0, sizeof(*e));
    e->type = (int32_t)irix_guest_read32be(rt, a + 0u);
    e->serial = (unsigned long)irix_guest_read32be(rt, a + 4u);
    e->send_event = (Bool)(int32_t)irix_guest_read32be(rt, a + 8u);
    e->display = display_from_guest(irix_guest_read32be(rt, a + 12u));
    e->window = (Window)irix_guest_read32be(rt, a + 16u);
    e->root = (Window)irix_guest_read32be(rt, a + 20u);
    e->subwindow = (Window)irix_guest_read32be(rt, a + 24u);
    e->time = (Time)irix_guest_read32be(rt, a + 28u);
    e->x = (int32_t)irix_guest_read32be(rt, a + 32u);
    e->y = (int32_t)irix_guest_read32be(rt, a + 36u);
    e->x_root = (int32_t)irix_guest_read32be(rt, a + 40u);
    e->y_root = (int32_t)irix_guest_read32be(rt, a + 44u);
    e->state = (unsigned int)irix_guest_read32be(rt, a + 48u);
    e->keycode = (unsigned int)irix_guest_read32be(rt, a + 52u);
    e->same_screen = (Bool)(int32_t)irix_guest_read32be(rt, a + 56u);
    return !rt->trapped && e->display;
}

static int read_xevent(IrixGuestRuntime *rt, IrixGuestAddr a, XEvent *e)
{
    int type;
    if (!a || !e || !irix_guest_ptr(rt, a, IRIX_XEVENT_SIZE)) return 0;
    memset(e, 0, sizeof(*e));
    type = (int32_t)irix_guest_read32be(rt, a + 0u);
    e->xany.type = type;
    e->xany.serial = (unsigned long)irix_guest_read32be(rt, a + 4u);
    e->xany.send_event = (Bool)(int32_t)irix_guest_read32be(rt, a + 8u);
    e->xany.display = display_from_guest(irix_guest_read32be(rt, a + 12u));
    e->xany.window = (Window)irix_guest_read32be(rt, a + 16u);
    if (!e->xany.display) return 0;
    switch (type) {
    case KeyPress:
    case KeyRelease:
        return read_xkeyevent(rt, a, &e->xkey);
    case FocusIn:
    case FocusOut:
        e->xfocus.mode = (int32_t)irix_guest_read32be(rt, a + 20u);
        e->xfocus.detail = (int32_t)irix_guest_read32be(rt, a + 24u);
        break;
    case MapRequest:
        e->xmaprequest.window =
            (Window)irix_guest_read32be(rt, a + 20u);
        break;
    case ConfigureRequest:
        e->xconfigurerequest.window =
            (Window)irix_guest_read32be(rt, a + 20u);
        e->xconfigurerequest.x =
            (int32_t)irix_guest_read32be(rt, a + 24u);
        e->xconfigurerequest.y =
            (int32_t)irix_guest_read32be(rt, a + 28u);
        e->xconfigurerequest.width =
            (int32_t)irix_guest_read32be(rt, a + 32u);
        e->xconfigurerequest.height =
            (int32_t)irix_guest_read32be(rt, a + 36u);
        e->xconfigurerequest.border_width =
            (int32_t)irix_guest_read32be(rt, a + 40u);
        e->xconfigurerequest.above =
            (Window)irix_guest_read32be(rt, a + 44u);
        e->xconfigurerequest.detail =
            (int32_t)irix_guest_read32be(rt, a + 48u);
        e->xconfigurerequest.value_mask =
            (unsigned long)irix_guest_read32be(rt, a + 52u);
        break;
    case PropertyNotify:
        e->xproperty.atom =
            (Atom)irix_guest_read32be(rt, a + 20u);
        e->xproperty.time =
            (Time)irix_guest_read32be(rt, a + 24u);
        e->xproperty.state =
            (int32_t)irix_guest_read32be(rt, a + 28u);
        break;
    case ClientMessage: {
        unsigned i;
        e->xclient.message_type =
            (Atom)irix_guest_read32be(rt, a + 20u);
        e->xclient.format =
            (int32_t)irix_guest_read32be(rt, a + 24u);
        if (e->xclient.format == 8) {
            for (i = 0; i < 20u; ++i)
                e->xclient.data.b[i] =
                    (char)irix_guest_read8(rt, a + 28u + i);
        } else if (e->xclient.format == 16) {
            for (i = 0; i < 10u; ++i)
                e->xclient.data.s[i] =
                    (short)irix_guest_read16be(rt, a + 28u + i * 2u);
        } else if (e->xclient.format == 32) {
            for (i = 0; i < 5u; ++i)
                e->xclient.data.l[i] =
                    (long)(int32_t)irix_guest_read32be(
                        rt, a + 28u + i * 4u);
        }
        break;
    }
    default:
        break;
    }
    return !rt->trapped;
}

static void bridge_XWindowEvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    long mask = (long)(int32_t)cpu->gpr[6];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    XEvent e;
    if (!d || !out) { cpu->gpr[2] = 0; return; }
    XWindowEvent(d, w, mask, &e);
    cpu->gpr[2] = write_xevent(rt, out, &e) ? 0u : 0u;
}

static void bridge_XCheckWindowEvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    long mask = (long)(int32_t)cpu->gpr[6];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    XEvent e;
    Bool ok = False;
    if (d && out) ok = XCheckWindowEvent(d, w, mask, &e);
    if (ok && !write_xevent(rt, out, &e)) ok = False;
    cpu->gpr[2] = (uint32_t)ok;
}

static void bridge_XFilterEvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XEvent e;
    if (!read_xevent(rt, (uint32_t)cpu->gpr[4], &e)) {
        cpu->gpr[2] = False;
        return;
    }
    cpu->gpr[2] = (uint32_t)XFilterEvent(&e, (Window)(uint32_t)cpu->gpr[5]);
}

static void bridge_XNextEvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    XEvent e;

    if (!d || !out) {
        cpu->gpr[2] = 0;
        return;
    }

    /*
     * Deliver the complete host event stream to SGI Xt/Motif.
     *
     * Older bring-up code dropped EnterNotify/LeaveNotify here because those
     * events reached an unresolved Motif BaseClass inheritance sentinel.
     * The BaseClass inheritance path is now repaired structurally, and Motif
     * menu traversal depends on crossing events for its normal popup/grab
     * state machine.  Suppressing them after that fix breaks real menu
     * semantics, so pass them through unchanged.
     */
    if (XNextEvent(d, &e) != 0) {
        cpu->gpr[2] = 0;
        return;
    }

    if (!write_xevent(rt, out, &e)) {
        cpu->gpr[2] = 0;
        return;
    }

    
    
    
    cpu->gpr[2] = 0;
}

static void bridge_XLookupKeysym(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XKeyEvent e;
    if (!read_xkeyevent(rt, (uint32_t)cpu->gpr[4], &e)) {
        cpu->gpr[2] = 0;
        return;
    }
    cpu->gpr[2] = (uint32_t)XLookupKeysym(&e, (int)(int32_t)cpu->gpr[5]);
}

static void bridge_XLookupString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XKeyEvent e;
    IrixGuestAddr buf_addr = (uint32_t)cpu->gpr[5];
    int bytes = (int)(int32_t)cpu->gpr[6];
    IrixGuestAddr keysym_addr = (uint32_t)cpu->gpr[7];
    char *buf = bytes > 0 ? (char *)irix_guest_ptr(rt, buf_addr, (size_t)bytes) : NULL;
    KeySym ks = NoSymbol;
    int rc;

    if (!read_xkeyevent(rt, (uint32_t)cpu->gpr[4], &e) ||
        (bytes > 0 && !buf)) {
        cpu->gpr[2] = 0;
        return;
    }
    rc = XLookupString(&e, buf, bytes, keysym_addr ? &ks : NULL, NULL);
    if (keysym_addr) wr32(rt, keysym_addr, (uint32_t)ks);
    
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XGrabKeyboard(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XGrabKeyboard(
        d, (Window)(uint32_t)cpu->gpr[5], (Bool)cpu->gpr[6],
        (int)cpu->gpr[7], (int)cpu->gpr[8], (Time)(uint32_t)cpu->gpr[9])
        : (uint32_t)GrabNotViewable;
}

static void bridge_XUngrabKeyboard(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    if (d) XUngrabKeyboard(d, (Time)(uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = 0;
}

static void bridge_XGrabPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    /*
     * N32 has eight integer argument registers (a0..a7). XGrabPointer has
     * nine arguments, so Time is the first stack argument in the ninth
     * 64-bit argument slot at sp+64. The old bridge incorrectly read t4.
     */
    IrixGuestAddr sp = (uint32_t)cpu->gpr[29];
    uint32_t time = 0;
    uint8_t *slot = (uint8_t *)irix_guest_ptr(rt, sp + 64u, 8u);

    if (slot)
        time = ((uint32_t)slot[4] << 24) | ((uint32_t)slot[5] << 16) |
               ((uint32_t)slot[6] << 8) | (uint32_t)slot[7];

    {
        int result = d ? XGrabPointer(
        d, (Window)(uint32_t)cpu->gpr[5], (Bool)cpu->gpr[6],
        (unsigned int)cpu->gpr[7], (int)cpu->gpr[8], (int)cpu->gpr[9],
        (Window)(uint32_t)cpu->gpr[10], (Cursor)(uint32_t)cpu->gpr[11],
        (Time)time) : GrabNotViewable;
        
        cpu->gpr[2] = (uint32_t)result;
    }
}

static void bridge_XUngrabPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    if (d) XUngrabPointer(d, (Time)(uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = 0;
}

SIMPLE_DISPLAY0(bridge_XEventsQueued, XEventsQueued(d, (int)cpu->gpr[5]))
SIMPLE_DISPLAY0(bridge_XBell, XBell(d, (int)cpu->gpr[5]))

static void bridge_XCreateSimpleWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    unsigned long background_pixel =
        (unsigned long)irix_guest_read64be(rt, sp + 64u);
    if (rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }
    cpu->gpr[2] = d ? (uint32_t)XCreateSimpleWindow(d, (Window)(uint32_t)cpu->gpr[5],
        (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9],
        (unsigned)cpu->gpr[10], (unsigned long)cpu->gpr[11],
        background_pixel) : 0;
}

#define WINDOW_CALL1(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); \
    (void)rt; \
    cpu->gpr[2] = d ? (uint32_t)(call) : 0; }

WINDOW_CALL1(bridge_XDestroyWindow, XDestroyWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapWindow, XMapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapRaised, XMapRaised(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XUnmapWindow, XUnmapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XRaiseWindow, XRaiseWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XLowerWindow, XLowerWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XClearWindow, XClearWindow(d, (Window)(uint32_t)cpu->gpr[5]))
static void bridge_XMoveWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    int x = (int32_t)cpu->gpr[6], y = (int32_t)cpu->gpr[7];
    (void)rt;
    
    cpu->gpr[2] = d ? (uint32_t)XMoveWindow(d, w, x, y) : 0;
}

static void bridge_XResizeWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    unsigned width = (unsigned)cpu->gpr[6], height = (unsigned)cpu->gpr[7];
    IrixGuestAddr caller = (uint32_t)cpu->gpr[31];
    const IrixGuestFunctionEntry *ce = irix_guest_find_function(rt, caller);
    
    cpu->gpr[2] = d ? (uint32_t)XResizeWindow(d, w, width, height) : 0;
}

static void bridge_XMoveResizeWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    int x = (int32_t)cpu->gpr[6], y = (int32_t)cpu->gpr[7];
    unsigned width = (unsigned)cpu->gpr[8], height = (unsigned)cpu->gpr[9];
    (void)rt;
    
    cpu->gpr[2] = d ? (uint32_t)XMoveResizeWindow(d, w, x, y, width, height) : 0;
}

static void bridge_XConfigureWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    unsigned int mask = (unsigned int)cpu->gpr[6];
    IrixGuestAddr a = (uint32_t)cpu->gpr[7];
    XWindowChanges v;

    if (!d || !a || !irix_guest_ptr(rt, a, 28u)) {
        cpu->gpr[2] = 0;
        return;
    }

    memset(&v, 0, sizeof(v));
    
    v.x = (int32_t)irix_guest_read32be(rt, a + 0u);
    v.y = (int32_t)irix_guest_read32be(rt, a + 4u);
    v.width = (int32_t)irix_guest_read32be(rt, a + 8u);
    v.height = (int32_t)irix_guest_read32be(rt, a + 12u);
    v.border_width = (int32_t)irix_guest_read32be(rt, a + 16u);
    v.sibling = (Window)irix_guest_read32be(rt, a + 20u);
    v.stack_mode = (int32_t)irix_guest_read32be(rt, a + 24u);
    cpu->gpr[2] = (uint32_t)XConfigureWindow(d, w, mask, &v);
}

static void bridge_XSelectInput(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    long mask = (long)(int32_t)cpu->gpr[6];
    int rc;

    if (!d) {
        cpu->gpr[2] = 0;
        return;
    }

    rc = XSelectInput(d, w, mask);
    cpu->gpr[2] = (uint32_t)rc;
}
WINDOW_CALL1(bridge_XSetWindowBackground, XSetWindowBackground(d, (Window)(uint32_t)cpu->gpr[5], (unsigned long)cpu->gpr[6]))
WINDOW_CALL1(bridge_XSetWindowBorder, XSetWindowBorder(d, (Window)(uint32_t)cpu->gpr[5], (unsigned long)cpu->gpr[6]))

static void bridge_XClearArea(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XClearArea(d, (Window)(uint32_t)cpu->gpr[5],
        (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9], (Bool)cpu->gpr[10]) : 0;
}

static void bridge_XStoreName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    cpu->gpr[2] = (d && s) ? (uint32_t)XStoreName(d, (Window)(uint32_t)cpu->gpr[5], s) : 0;
}

static void bridge_XSetIconName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    cpu->gpr[2] = (d && s) ? (uint32_t)XSetIconName(d, (Window)(uint32_t)cpu->gpr[5], s) : 0;
}

static void bridge_XCreatePixmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCreatePixmap(d, (Drawable)(uint32_t)cpu->gpr[5],
        (unsigned)cpu->gpr[6], (unsigned)cpu->gpr[7], (unsigned)cpu->gpr[8]) : 0;
}

WINDOW_CALL1(bridge_XFreePixmap, XFreePixmap(d, (Pixmap)(uint32_t)cpu->gpr[5]))

static void bridge_XCreateGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    unsigned long mask = (unsigned long)(uint32_t)cpu->gpr[6];
    IrixGuestAddr values_addr = (uint32_t)cpu->gpr[7];
    XGCValues values;
    XGCValues *values_ptr = 0;
    GC gc;

    if (!d) { cpu->gpr[2] = 0; return; }
    if (mask) {
        if (!read_xgcvalues(rt, values_addr, &values)) {
            cpu->gpr[2] = 0;
            return;
        }
        values_ptr = &values;
    }
    gc = XCreateGC(d, (Drawable)(uint32_t)cpu->gpr[5], mask, values_ptr);
    cpu->gpr[2] = gc_to_guest(gc);
}

static void bridge_XChangeGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]);
    unsigned long mask = (unsigned long)(uint32_t)cpu->gpr[6];
    IrixGuestAddr values_addr = (uint32_t)cpu->gpr[7];
    XGCValues values;
    int rc = 0;

    if (d && gc && read_xgcvalues(rt, values_addr, &values)) {
        rc = XChangeGC(d, gc, mask, &values);
        sync_gc_by_ptr(gc);
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XGetGCValues(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]);
    unsigned long mask = (unsigned long)(uint32_t)cpu->gpr[6];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    XGCValues values;
    Status ok = 0;
    memset(&values, 0, sizeof(values));
    if (d && gc) ok = XGetGCValues(d, gc, mask, &values);
    if (ok && out) write_xgcvalues(rt, out, &values);
    cpu->gpr[2] = (uint32_t)ok;
}

static void bridge_XCopyGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC src = gc_from_guest((uint32_t)cpu->gpr[5]);
    unsigned long mask = (unsigned long)(uint32_t)cpu->gpr[6];
    GC dst = gc_from_guest((uint32_t)cpu->gpr[7]);
    int rc = (d && src && dst) ? XCopyGC(d, src, mask, dst) : 0;
    if (rc && dst) sync_gc_by_ptr(dst);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XFreeGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr guest = (uint32_t)cpu->gpr[5];
    GC gc = gc_from_guest(guest);
    uint32_t off, i;
    int rc = 0;
    if (d && gc) rc = XFreeGC(d, gc);
    if (gc && guest >= IRIX_X11_GC_BASE) {
        off = guest - IRIX_X11_GC_BASE;
        if ((off % IRIX_X11_GC_STRIDE) == 0u) {
            i = off / IRIX_X11_GC_STRIDE;
            if (i < g_x11.gc_count) {
                g_x11.gcs[i] = 0;
                if (irix_guest_ptr(rt, guest, 112u))
                    memset(irix_guest_ptr(rt, guest, 112u), 0, 112u);
            }
        }
    }
    cpu->gpr[2] = (uint32_t)rc;
}

#define GC_CALL(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); \
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]); \
    int rc = (d && gc) ? (call) : 0; \
    (void)rt; if (d && gc) sync_gc_by_ptr(gc); \
    cpu->gpr[2] = (uint32_t)rc; }

GC_CALL(bridge_XSetForeground, XSetForeground(d, gc, (unsigned long)cpu->gpr[6]))
GC_CALL(bridge_XSetBackground, XSetBackground(d, gc, (unsigned long)cpu->gpr[6]))
GC_CALL(bridge_XSetFunction, XSetFunction(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetPlaneMask, XSetPlaneMask(d, gc, (unsigned long)cpu->gpr[6]))
GC_CALL(bridge_XSetFillStyle, XSetFillStyle(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetFillRule, XSetFillRule(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetArcMode, XSetArcMode(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetClipMask, XSetClipMask(d, gc, (Pixmap)(uint32_t)cpu->gpr[6]))
GC_CALL(bridge_XSetClipOrigin, XSetClipOrigin(d, gc, (int)cpu->gpr[6], (int)cpu->gpr[7]))
GC_CALL(bridge_XSetFont, XSetFont(d, gc, (Font)(uint32_t)cpu->gpr[6]))
GC_CALL(bridge_XSetSubwindowMode, XSetSubwindowMode(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetGraphicsExposures, XSetGraphicsExposures(d, gc, (Bool)cpu->gpr[6]))

static void bridge_XSetLineAttributes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]);
    int rc = (d && gc) ? XSetLineAttributes(d, gc, (unsigned)cpu->gpr[6],
        (int)cpu->gpr[7], (int)cpu->gpr[8], (int)cpu->gpr[9]) : 0;
    (void)rt;
    if (d && gc) sync_gc_by_ptr(gc);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XDrawLine(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawLine(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(int)cpu->gpr[9],(int)cpu->gpr[10]):0;
}

static void bridge_XDrawRectangle(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawRectangle(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10]):0;
}

static void bridge_XFillRectangle(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XFillRectangle(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10]):0;
}

static void bridge_XCopyArea(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[7]);
    IrixGuestAddr sp=(IrixGuestAddr)(uint32_t)cpu->gpr[29];
    /* The N32 caller places the final two arguments at SP and SP+8. */
    int dest_x=(int32_t)irix_guest_read64be(rt,sp);
    int dest_y=(int32_t)irix_guest_read64be(rt,sp+8u);
    if(rt->trapped){cpu->gpr[2]=0;return;}
    cpu->gpr[2]=(d&&gc)?(uint32_t)XCopyArea(d,(Drawable)(uint32_t)cpu->gpr[5],(Drawable)(uint32_t)cpu->gpr[6],gc,
        (int)cpu->gpr[8],(int)cpu->gpr[9],(unsigned)cpu->gpr[10],(unsigned)cpu->gpr[11],dest_x,dest_y):0;
}

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(
        rt, sp + (IrixGuestAddr)(index * 8u));
}

static void bridge_XGetGeometry(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); Window root=0; int x=0,y=0; unsigned w=0,h=0,b=0,depth=0; Status s=0;
    if (d) s=XGetGeometry(d,(Drawable)(uint32_t)cpu->gpr[5],&root,&x,&y,&w,&h,&b,&depth);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)root); wr32(rt,(uint32_t)cpu->gpr[7],(uint32_t)x);
    wr32(rt,(uint32_t)cpu->gpr[8],(uint32_t)y); wr32(rt,(uint32_t)cpu->gpr[9],w);
    wr32(rt,(uint32_t)cpu->gpr[10],h); wr32(rt,(uint32_t)cpu->gpr[11],b);
    wr32(rt,n32_stack_arg32(rt,cpu,0u),depth);
    cpu->gpr[2]=(uint32_t)s;
}

static void bridge_XTranslateCoordinates(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); int x=0,y=0; Window child=0; Bool s=False;
    if(d) s=XTranslateCoordinates(d,(Window)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6],
        (int)cpu->gpr[7],(int)cpu->gpr[8],&x,&y,&child);
    wr32(rt,(uint32_t)cpu->gpr[9],(uint32_t)x); wr32(rt,(uint32_t)cpu->gpr[10],(uint32_t)y); wr32(rt,(uint32_t)cpu->gpr[11],(uint32_t)child);
    cpu->gpr[2]=(uint32_t)s;
}

static void bridge_XGetInputFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); Window w=0; int revert=0;
    if(d) XGetInputFocus(d,&w,&revert); wr32(rt,(uint32_t)cpu->gpr[5],(uint32_t)w); wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)revert); cpu->gpr[2]=0;
}

static void bridge_XSetInputFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XSetInputFocus(d,(Window)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6],(Time)(uint32_t)cpu->gpr[7]):0;
}

static Cursor g_irix_root_pointer_cursor;
static Display *g_irix_root_pointer_display;

static void bridge_XCreateFontCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    unsigned shape = (unsigned)cpu->gpr[5];
    Cursor cursor = None;
    Font font = None;
    XColor fg, bg;
    const char *source = "cursor-font";

    /* SGI XCreateFontCursor creates a cursor-font glyph, which can later be
       recolored with XRecolorCursor.  Modern host Xlib may instead return an
       Xcursor theme pixmap (for example "left_ptr"), whose colors are baked
       in and cannot be recolored.  Use the core cursor font to preserve the
       IRIX behavior. */
    if (d && shape < 65535u && (font = XLoadFont(d, "cursor")) != None) {
        memset(&fg, 0, sizeof(fg));
        memset(&bg, 0, sizeof(bg));
        fg.pixel = BlackPixel(d, DefaultScreen(d));
        fg.flags = DoRed | DoGreen | DoBlue;
        bg.pixel = WhitePixel(d, DefaultScreen(d));
        bg.red = bg.green = bg.blue = 65535u;
        bg.flags = DoRed | DoGreen | DoBlue;
        cursor = XCreateGlyphCursor(d, font, font, (unsigned short)shape,
                                    (unsigned short)(shape + 1u), &fg, &bg);
        XUnloadFont(d, font);
    }
    if (d && !cursor) {
        cursor = XCreateFontCursor(d, shape);
    }
    (void)rt;
    
    /* 4Dwm's pointerColor resource creates/recolors its normal pointer but
       the SGI startup path never defines that cursor on the root window.
       Keep this opt-in so ordinary X clients retain Xlib semantics.  Defer
       definition until XRecolorCursor so the active root cursor has final RGB. */
    if (d && cursor && shape == 2u &&
        getenv("IRIX_DEFINE_ROOT_POINTER_CURSOR")) {
        g_irix_root_pointer_cursor = cursor;
        g_irix_root_pointer_display = d;
    }
    cpu->gpr[2] = (uint32_t)cursor;
}

static void bridge_XRecolorCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr fg_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr bg_addr = (uint32_t)cpu->gpr[7];
    XColor fg, bg;
    if (!d || !fg_addr || !bg_addr ||
        !irix_guest_ptr(rt, fg_addr, 12u) || !irix_guest_ptr(rt, bg_addr, 12u)) {
        cpu->gpr[2] = 0;
        return;
    }
    memset(&fg, 0, sizeof(fg)); memset(&bg, 0, sizeof(bg));
    fg.pixel = (unsigned long)irix_guest_read32be(rt, fg_addr + 0u);
    fg.red = irix_guest_read16be(rt, fg_addr + 4u); fg.green = irix_guest_read16be(rt, fg_addr + 6u); fg.blue = irix_guest_read16be(rt, fg_addr + 8u);
    fg.flags = (char)irix_guest_read8(rt, fg_addr + 10u); fg.pad = (char)irix_guest_read8(rt, fg_addr + 11u);
    bg.pixel = (unsigned long)irix_guest_read32be(rt, bg_addr + 0u);
    bg.red = irix_guest_read16be(rt, bg_addr + 4u); bg.green = irix_guest_read16be(rt, bg_addr + 6u); bg.blue = irix_guest_read16be(rt, bg_addr + 8u);
    bg.flags = (char)irix_guest_read8(rt, bg_addr + 10u); bg.pad = (char)irix_guest_read8(rt, bg_addr + 11u);
    if (rt->trapped) { cpu->gpr[2] = 0; return; }
    {
        Cursor cursor = (Cursor)(uint32_t)cpu->gpr[5];
        int rc = XRecolorCursor(d, cursor, &fg, &bg);
        cpu->gpr[2] = (uint32_t)rc;
        if (d == g_irix_root_pointer_display &&
            cursor == g_irix_root_pointer_cursor &&
            getenv("IRIX_DEFINE_ROOT_POINTER_CURSOR")) {
            Window root = DefaultRootWindow(d);
            XDefineCursor(d, root, cursor);
            XSync(d, False);
        }
    }
}

WINDOW_CALL1(bridge_XFreeCursor, XFreeCursor(d,(Cursor)(uint32_t)cpu->gpr[5]))

static void bridge_XDefineCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    Cursor cursor = (Cursor)(uint32_t)cpu->gpr[6];
    int rc = d ? XDefineCursor(d, w, cursor) : 0;
    (void)rt;
    
    if (d) XFlush(d);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XUndefineCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    int rc = d ? XUndefineCursor(d, w) : 0;
    (void)rt;
    
    if (d) XFlush(d);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XSetSelectionOwner(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XSetSelectionOwner(d,(Atom)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6],(Time)(uint32_t)cpu->gpr[7]):0;
}

static void bridge_XGetSelectionOwner(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XGetSelectionOwner(d,(Atom)(uint32_t)cpu->gpr[5]):0;
}

static void bridge_XDeleteProperty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XDeleteProperty(d,(Window)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6]):0;
}

static void bridge_XChangeProperty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]);
    int format=(int)cpu->gpr[8]; int nelem=(int)cpu->gpr[11]; IrixGuestAddr data_addr=(uint32_t)cpu->gpr[10];
    const unsigned char *data=0; unsigned long *longs=0; int i;
    if(format==8) data=(const unsigned char *)irix_guest_ptr(rt,data_addr,(size_t)(nelem>0?nelem:0));
    else if(format==16) data=(const unsigned char *)irix_guest_ptr(rt,data_addr,(size_t)(nelem>0?nelem*2:0));
    else if(format==32 && nelem>0) {
        longs=(unsigned long *)calloc((size_t)nelem,sizeof(*longs));
        if(longs) for(i=0;i<nelem;++i) longs[i]=irix_guest_read32be(rt,data_addr+(IrixGuestAddr)i*4u);
        data=(const unsigned char *)longs;
    }
    cpu->gpr[2]=(d && (nelem==0 || data))?(uint32_t)XChangeProperty(d,(Window)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6],
        (Atom)(uint32_t)cpu->gpr[7],format,(int)cpu->gpr[9],data,nelem):0;
    free(longs);
}

#ifdef IRIX_HAVE_XEXT
static uint32_t shape_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                            unsigned int index)
{
    IrixGuestAddr sp;
    if (index < 8u) return (uint32_t)cpu->gpr[4u + index];
    sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(
        rt, sp + (IrixGuestAddr)((index - 8u) * 8u));
}

/*
 * X Double Buffer extension: Status XdbeQueryExtension(dpy, &major, &minor).
 * None of the DBE request builders are bridged, and the guest libXext
 * implementation would walk the Display shadow, which has no extension
 * table.  Report the extension as unavailable; clients fall back to
 * single-buffered drawing.
 */
static void bridge_XdbeQueryExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
}

static void bridge_XShapeQueryExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    IrixGuestAddr event_out = shape_arg32(rt, cpu, 1u);
    IrixGuestAddr error_out = shape_arg32(rt, cpu, 2u);
    int event_base = 0, error_base = 0;
    Bool rc = d ? XShapeQueryExtension(d, &event_base, &error_base) : False;
    wr32(rt, event_out, (uint32_t)event_base);
    wr32(rt, error_out, (uint32_t)error_base);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XShapeSelectInput(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    Window window = (Window)shape_arg32(rt, cpu, 1u);
    unsigned long mask = (unsigned long)shape_arg32(rt, cpu, 2u);
    if (d) XShapeSelectInput(d, window, mask);
    cpu->gpr[2] = 0;
}

static void bridge_XShapeQueryExtents(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    Window window = (Window)shape_arg32(rt, cpu, 1u);
    IrixGuestAddr bounding_shaped_out = shape_arg32(rt, cpu, 2u);
    IrixGuestAddr x_bounding_out = shape_arg32(rt, cpu, 3u);
    IrixGuestAddr y_bounding_out = shape_arg32(rt, cpu, 4u);
    IrixGuestAddr w_bounding_out = shape_arg32(rt, cpu, 5u);
    IrixGuestAddr h_bounding_out = shape_arg32(rt, cpu, 6u);
    IrixGuestAddr clip_shaped_out = shape_arg32(rt, cpu, 7u);
    IrixGuestAddr x_clip_out = shape_arg32(rt, cpu, 8u);
    IrixGuestAddr y_clip_out = shape_arg32(rt, cpu, 9u);
    IrixGuestAddr w_clip_out = shape_arg32(rt, cpu, 10u);
    IrixGuestAddr h_clip_out = shape_arg32(rt, cpu, 11u);
    Bool bounding_shaped = False, clip_shaped = False;
    int x_bounding = 0, y_bounding = 0, x_clip = 0, y_clip = 0;
    unsigned int w_bounding = 0, h_bounding = 0, w_clip = 0, h_clip = 0;
    Status rc = 0;

    if (d && !rt->trapped)
        rc = XShapeQueryExtents(d, window, &bounding_shaped,
            &x_bounding, &y_bounding, &w_bounding, &h_bounding,
            &clip_shaped, &x_clip, &y_clip, &w_clip, &h_clip);
    if (rc) {
        wr32(rt, bounding_shaped_out, (uint32_t)bounding_shaped);
        wr32(rt, x_bounding_out, (uint32_t)x_bounding);
        wr32(rt, y_bounding_out, (uint32_t)y_bounding);
        wr32(rt, w_bounding_out, w_bounding);
        wr32(rt, h_bounding_out, h_bounding);
        wr32(rt, clip_shaped_out, (uint32_t)clip_shaped);
        wr32(rt, x_clip_out, (uint32_t)x_clip);
        wr32(rt, y_clip_out, (uint32_t)y_clip);
        wr32(rt, w_clip_out, w_clip);
        wr32(rt, h_clip_out, h_clip);
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XShapeCombineShape(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    if (d && !rt->trapped)
        XShapeCombineShape(d,
            (Window)shape_arg32(rt, cpu, 1u),
            (int32_t)shape_arg32(rt, cpu, 2u),
            (int32_t)shape_arg32(rt, cpu, 3u),
            (int32_t)shape_arg32(rt, cpu, 4u),
            (Window)shape_arg32(rt, cpu, 5u),
            (int32_t)shape_arg32(rt, cpu, 6u),
            (int32_t)shape_arg32(rt, cpu, 7u));
    cpu->gpr[2] = 0;
}

static void bridge_XShapeCombineRectangles(IrixGuestRuntime *rt,
                                           IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    IrixGuestAddr rect_addr = shape_arg32(rt, cpu, 5u);
    int nrects = (int32_t)shape_arg32(rt, cpu, 6u);
    XRectangle *rects = NULL;
    int i;

    if (d && !rt->trapped && nrects > 0) {
        if (nrects > 65536 ||
            !irix_guest_ptr(rt, rect_addr, (size_t)nrects * 8u)) {
            cpu->gpr[2] = 0;
            return;
        }
        rects = (XRectangle *)calloc((size_t)nrects, sizeof(*rects));
        if (!rects) {
            cpu->gpr[2] = 0;
            return;
        }
        for (i = 0; i < nrects; ++i) {
            IrixGuestAddr r = rect_addr + (IrixGuestAddr)i * 8u;
            rects[i].x = (short)(int16_t)irix_guest_read16be(rt, r);
            rects[i].y = (short)(int16_t)irix_guest_read16be(rt, r + 2u);
            rects[i].width = irix_guest_read16be(rt, r + 4u);
            rects[i].height = irix_guest_read16be(rt, r + 6u);
        }
        if (!rt->trapped)
            XShapeCombineRectangles(d, (Window)shape_arg32(rt, cpu, 1u),
                (int)shape_arg32(rt, cpu, 2u),
                (int32_t)shape_arg32(rt, cpu, 3u),
                (int32_t)shape_arg32(rt, cpu, 4u), rects, nrects,
                (int)shape_arg32(rt, cpu, 7u),
                (int)shape_arg32(rt, cpu, 8u));
        free(rects);
    }
    cpu->gpr[2] = 0;
}

static void bridge_XShapeCombineMask(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(shape_arg32(rt, cpu, 0u));
    if (d && !rt->trapped)
        XShapeCombineMask(d,
            (Window)shape_arg32(rt, cpu, 1u),
            (int32_t)shape_arg32(rt, cpu, 2u),
            (int32_t)shape_arg32(rt, cpu, 3u),
            (int32_t)shape_arg32(rt, cpu, 4u),
            (Pixmap)shape_arg32(rt, cpu, 5u),
            (int32_t)shape_arg32(rt, cpu, 6u));
    cpu->gpr[2] = 0;
}
#endif

void irix_x11_extra_init(IrixGuestRuntime *rt)
{
    memset(&g_x11,0,sizeof(g_x11));
    g_x11.rt=rt;
    (void)irix_guest_map(rt,IRIX_X11_DISPLAY_BASE,IRIX_X11_DISPLAY_SHADOW_SIZE,3u);
    (void)irix_guest_map(rt,IRIX_X11_GC_BASE,IRIX_X11_GC_SHADOW_SIZE,3u);
    if(irix_guest_map(rt,IRIX_X11_TMP_BASE,IRIX_X11_TMP_SIZE,3u)==0 || irix_guest_ptr(rt,IRIX_X11_TMP_BASE,1))
        g_x11.tmp_cur=IRIX_X11_TMP_BASE;
}

IrixGuestFunction irix_x11_bulk_lookup(const char *name);
IrixGuestFunction irix_x11_window_bridge_lookup(const char *name);

IrixGuestFunction irix_x11_extra_lookup(const char *name)
{
    if(!name) return 0;
#define MAP(n) if(!strcmp(name,#n)) return bridge_##n
    MAP(_XSGIAddToPreloadAtomList);
    MAP(XOpenDisplay); MAP(XCloseDisplay); MAP(XDefaultScreen); MAP(XRootWindow); MAP(XDefaultRootWindow);
    if(!strcmp(name,"XAllocNamedColor")) return irix_x11_bulk_lookup(name);
    if(!strcmp(name,"XQueryColors")) return irix_x11_bulk_lookup(name);
    if(!strcmp(name,"XChangeWindowAttributes")) return irix_x11_window_bridge_lookup(name);
    MAP(XDisplayWidth); MAP(XDisplayHeight); MAP(XDefaultDepth); MAP(XBlackPixel); MAP(XWhitePixel); MAP(XDefaultColormap);
    if(!strcmp(name, "_IrixXkbDisplayInit")) return bridge__IrixXkbDisplayInit;
    MAP(XkbKeysymToModifiers); MAP(XkbGetUpdatedMap); MAP(XkbSelectEventDetails); MAP(XkbQueryExtension); MAP(XkbUseExtension); MAP(XInitExtension); MAP(XAddExtension); MAP(XQueryExtension); MAP(XInternAtom); MAP(XGetAtomName); MAP(XLoadQueryFont); MAP(XFlush); MAP(XSync); MAP(XPending); MAP(XNextEvent); MAP(XWindowEvent); MAP(XCheckWindowEvent); MAP(XFilterEvent); MAP(XEventsQueued); MAP(XBell); MAP(XLookupKeysym); MAP(XLookupString); MAP(XGrabKeyboard); MAP(XUngrabKeyboard); MAP(XGrabPointer); MAP(XUngrabPointer);
    MAP(XCreateSimpleWindow); MAP(XDestroyWindow); MAP(XMapWindow); MAP(XMapRaised); MAP(XUnmapWindow); MAP(XRaiseWindow); MAP(XLowerWindow);
    MAP(XMoveWindow); MAP(XResizeWindow); MAP(XMoveResizeWindow); MAP(XConfigureWindow); MAP(XSelectInput); MAP(XSetWindowBackground); MAP(XSetWindowBorder); MAP(XClearWindow); MAP(XClearArea);
    MAP(XStoreName); MAP(XSetIconName); MAP(XCreatePixmap); MAP(XFreePixmap); MAP(XCreateGC); MAP(XChangeGC); MAP(XGetGCValues); MAP(XCopyGC); MAP(XFreeGC);
    MAP(XSetForeground); MAP(XSetBackground); MAP(XSetFunction); MAP(XSetPlaneMask); MAP(XSetFillStyle); MAP(XSetFillRule); MAP(XSetArcMode); MAP(XSetClipMask); MAP(XSetClipOrigin); MAP(XSetFont); MAP(XSetSubwindowMode); MAP(XSetGraphicsExposures); MAP(XSetLineAttributes);
    MAP(XDrawLine); MAP(XDrawRectangle); MAP(XFillRectangle); MAP(XCopyArea); MAP(XGetGeometry); MAP(XTranslateCoordinates);
    MAP(XGetInputFocus); MAP(XSetInputFocus); MAP(XCreateFontCursor); MAP(XRecolorCursor); MAP(XFreeCursor); MAP(XDefineCursor); MAP(XUndefineCursor);
    MAP(XSetSelectionOwner); MAP(XGetSelectionOwner); MAP(XDeleteProperty); MAP(XChangeProperty);
#ifdef IRIX_HAVE_XEXT
    MAP(XdbeQueryExtension);
    MAP(XShapeQueryExtension); MAP(XShapeSelectInput); MAP(XShapeQueryExtents);
    MAP(XShapeCombineShape); MAP(XShapeCombineRectangles); MAP(XShapeCombineMask);
#endif
#undef MAP
    return 0;
}
