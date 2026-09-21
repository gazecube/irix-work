#include "IrixGuest.h"

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
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
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
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
    wr32(g_x11.rt, a + 8u, (uint32_t)gc->rects);
    wr32(g_x11.rt, a + 12u, (uint32_t)gc->dashes);
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

    fprintf(stderr,
            "IRIX Display shadow: guest=%08x free_funcs=%08x min_keycode=%d max_keycode=%d screens=%d\\n",
            d, free_funcs, min_keycode, max_keycode, ns);

    if (ns > 16) ns = 16;
    for (s = 0; s < ns; ++s) {
        Screen *hs = ScreenOfDisplay(dpy, s);
        Visual *hv = DefaultVisual(dpy, s);
        IrixGuestAddr gs = screens + (IrixGuestAddr)(s * 80u);
        IrixGuestAddr gv = visuals + (IrixGuestAddr)(s * 32u);
        write_visual_shadow(rt, gv, hv);
        wr32(rt, gs + 0, 0);
        wr32(rt, gs + 4, d);
        wr32(rt, gs + 8, (uint32_t)hs->root);
        wr32(rt, gs + 12, (uint32_t)hs->width);
        wr32(rt, gs + 16, (uint32_t)hs->height);
        wr32(rt, gs + 20, (uint32_t)hs->mwidth);
        wr32(rt, gs + 24, (uint32_t)hs->mheight);
        wr32(rt, gs + 28, (uint32_t)hs->ndepths);
        wr32(rt, gs + 32, 0);
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
        if (g_x11.displays[i] == dpy) {
            write_display_shadow(i, dpy);
            return display_guest_addr(i);
        }
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
    if (!gc) return 0;
    for (i = 0; i < g_x11.gc_count; ++i)
        if (g_x11.gcs[i] == gc) {
            sync_gc_shadow(i);
            return IRIX_X11_GC_BASE + (IrixGuestAddr)(i * IRIX_X11_GC_STRIDE);
        }
    if (g_x11.gc_count >= IRIX_X11_MAX_GCS) return 0;
    i = g_x11.gc_count++;
    g_x11.gcs[i] = gc;
    sync_gc_shadow(i);
    return IRIX_X11_GC_BASE + (IrixGuestAddr)(i * IRIX_X11_GC_STRIDE);
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
    XFontStruct *f = (d && name) ? XLoadQueryFont(d, name) : 0;
    cpu->gpr[2] = font_to_guest(rt, f);
}

static void bridge_XrmParseCommand(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
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

static void bridge_XOpenDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr name_addr = (uint32_t)cpu->gpr[4];
    const char *name = guest_cstr(rt, name_addr);
    const char *host_display = getenv("DISPLAY");
    Display *dpy;
    IrixGuestAddr guest_display;

    fprintf(stderr,
            "IRIX XOpenDisplay: guest_name=0x%08x name=%s%s%s host_DISPLAY=%s%s%s\\n",
            name_addr,
            name ? "\\\"" : "<", name ? name : "NULL", name ? "\\\"" : ">",
            host_display ? "\\\"" : "<", host_display ? host_display : "NULL",
            host_display ? "\\\"" : ">");

    dpy = XOpenDisplay(name);
    guest_display = display_to_guest(dpy);
    fprintf(stderr,
            "IRIX XOpenDisplay: host_result=%p guest_result=0x%08x\\n",
            (void *)dpy, guest_display);
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

SIMPLE_DISPLAY0(bridge_XFlush, XFlush(d))
SIMPLE_DISPLAY0(bridge_XSync, XSync(d, (Bool)cpu->gpr[5]))
SIMPLE_DISPLAY0(bridge_XPending, XPending(d))
SIMPLE_DISPLAY0(bridge_XEventsQueued, XEventsQueued(d, (int)cpu->gpr[5]))
SIMPLE_DISPLAY0(bridge_XBell, XBell(d, (int)cpu->gpr[5]))

static void bridge_XCreateSimpleWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCreateSimpleWindow(d, (Window)(uint32_t)cpu->gpr[5],
        (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9],
        (unsigned)cpu->gpr[10], (unsigned long)cpu->gpr[11], (unsigned long)cpu->gpr[12]) : 0;
}

#define WINDOW_CALL1(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt; \
    cpu->gpr[2] = d ? (uint32_t)(call) : 0; }

WINDOW_CALL1(bridge_XDestroyWindow, XDestroyWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapWindow, XMapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapRaised, XMapRaised(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XUnmapWindow, XUnmapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XRaiseWindow, XRaiseWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XLowerWindow, XLowerWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XClearWindow, XClearWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMoveWindow, XMoveWindow(d, (Window)(uint32_t)cpu->gpr[5], (int)cpu->gpr[6], (int)cpu->gpr[7]))
WINDOW_CALL1(bridge_XResizeWindow, XResizeWindow(d, (Window)(uint32_t)cpu->gpr[5], (unsigned)cpu->gpr[6], (unsigned)cpu->gpr[7]))
WINDOW_CALL1(bridge_XMoveResizeWindow, XMoveResizeWindow(d, (Window)(uint32_t)cpu->gpr[5], (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9]))
WINDOW_CALL1(bridge_XSelectInput, XSelectInput(d, (Window)(uint32_t)cpu->gpr[5], (long)(int32_t)cpu->gpr[6]))
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
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[7]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XCopyArea(d,(Drawable)(uint32_t)cpu->gpr[5],(Drawable)(uint32_t)cpu->gpr[6],gc,
        (int)cpu->gpr[8],(int)cpu->gpr[9],(unsigned)cpu->gpr[10],(unsigned)cpu->gpr[11],(int)cpu->gpr[12],(int)cpu->gpr[13]):0;
}

static void bridge_XGetGeometry(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); Window root=0; int x=0,y=0; unsigned w=0,h=0,b=0,depth=0; Status s=0;
    if (d) s=XGetGeometry(d,(Drawable)(uint32_t)cpu->gpr[5],&root,&x,&y,&w,&h,&b,&depth);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)root); wr32(rt,(uint32_t)cpu->gpr[7],(uint32_t)x);
    wr32(rt,(uint32_t)cpu->gpr[8],(uint32_t)y); wr32(rt,(uint32_t)cpu->gpr[9],w);
    wr32(rt,(uint32_t)cpu->gpr[10],h); wr32(rt,(uint32_t)cpu->gpr[11],b); wr32(rt,(uint32_t)cpu->gpr[12],depth);
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

static void bridge_XCreateFontCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XCreateFontCursor(d,(unsigned)cpu->gpr[5]):0;
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
    cpu->gpr[2] = (uint32_t)XRecolorCursor(d, (Cursor)(uint32_t)cpu->gpr[5], &fg, &bg);
}

WINDOW_CALL1(bridge_XFreeCursor, XFreeCursor(d,(Cursor)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XDefineCursor, XDefineCursor(d,(Window)(uint32_t)cpu->gpr[5],(Cursor)(uint32_t)cpu->gpr[6]))
WINDOW_CALL1(bridge_XUndefineCursor, XUndefineCursor(d,(Window)(uint32_t)cpu->gpr[5]))

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

void irix_x11_extra_init(IrixGuestRuntime *rt)
{
    memset(&g_x11,0,sizeof(g_x11));
    g_x11.rt=rt;
    (void)irix_guest_map(rt,IRIX_X11_DISPLAY_BASE,IRIX_X11_DISPLAY_SHADOW_SIZE,3u);
    (void)irix_guest_map(rt,IRIX_X11_GC_BASE,IRIX_X11_GC_SHADOW_SIZE,3u);
    if(irix_guest_map(rt,IRIX_X11_TMP_BASE,IRIX_X11_TMP_SIZE,3u)==0 || irix_guest_ptr(rt,IRIX_X11_TMP_BASE,1))
        g_x11.tmp_cur=IRIX_X11_TMP_BASE;
}

IrixGuestFunction irix_x11_extra_lookup(const char *name)
{
    if(!name) return 0;
#define MAP(n) if(!strcmp(name,#n)) return bridge_##n
    MAP(XrmParseCommand); MAP(_XSGIAddToPreloadAtomList);
    MAP(XOpenDisplay); MAP(XCloseDisplay); MAP(XDefaultScreen); MAP(XRootWindow); MAP(XDefaultRootWindow);
    MAP(XDisplayWidth); MAP(XDisplayHeight); MAP(XDefaultDepth); MAP(XBlackPixel); MAP(XWhitePixel); MAP(XDefaultColormap);
    MAP(XQueryExtension); MAP(XInternAtom); MAP(XGetAtomName); MAP(XLoadQueryFont); MAP(XFlush); MAP(XSync); MAP(XPending); MAP(XEventsQueued); MAP(XBell);
    MAP(XCreateSimpleWindow); MAP(XDestroyWindow); MAP(XMapWindow); MAP(XMapRaised); MAP(XUnmapWindow); MAP(XRaiseWindow); MAP(XLowerWindow);
    MAP(XMoveWindow); MAP(XResizeWindow); MAP(XMoveResizeWindow); MAP(XSelectInput); MAP(XSetWindowBackground); MAP(XSetWindowBorder); MAP(XClearWindow); MAP(XClearArea);
    MAP(XStoreName); MAP(XSetIconName); MAP(XCreatePixmap); MAP(XFreePixmap); MAP(XCreateGC); MAP(XChangeGC); MAP(XGetGCValues); MAP(XCopyGC); MAP(XFreeGC);
    MAP(XSetForeground); MAP(XSetBackground); MAP(XSetFunction); MAP(XSetPlaneMask); MAP(XSetFillStyle); MAP(XSetFillRule); MAP(XSetArcMode); MAP(XSetClipMask); MAP(XSetClipOrigin); MAP(XSetFont); MAP(XSetSubwindowMode); MAP(XSetGraphicsExposures); MAP(XSetLineAttributes);
    MAP(XDrawLine); MAP(XDrawRectangle); MAP(XFillRectangle); MAP(XCopyArea); MAP(XGetGeometry); MAP(XTranslateCoordinates);
    MAP(XGetInputFocus); MAP(XSetInputFocus); MAP(XCreateFontCursor); MAP(XRecolorCursor); MAP(XFreeCursor); MAP(XDefineCursor); MAP(XUndefineCursor);
    MAP(XSetSelectionOwner); MAP(XGetSelectionOwner); MAP(XDeleteProperty); MAP(XChangeProperty);
#undef MAP
    return 0;
}
