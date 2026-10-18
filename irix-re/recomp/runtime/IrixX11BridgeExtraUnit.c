#define irix_x11_extra_lookup irix_x11_extra_lookup_base
#include "IrixX11BridgeExtra.c"
#undef irix_x11_extra_lookup
#include "IrixX11BridgeExtraAccess.h"

#ifdef IRIX_HAVE_XPM
#include <X11/xpm.h>

static char **xpm_data_from_guest(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    size_t count = 0, cap = 32;
    char **out;

    if (!addr) return 0;
    out = (char **)calloc(cap, sizeof(*out));
    if (!out) return 0;

    for (;;) {
        IrixGuestAddr saddr;
        if (count + 1u >= cap) {
            size_t new_cap = cap * 2u;
            char **tmp;
            if (new_cap > 4096u) { free(out); return 0; }
            tmp = (char **)realloc(out, new_cap * sizeof(*out));
            if (!tmp) { free(out); return 0; }
            memset(tmp + cap, 0, (new_cap - cap) * sizeof(*out));
            out = tmp;
            cap = new_cap;
        }

        saddr = irix_guest_read32be(rt, addr + (IrixGuestAddr)(count * 4u));
        if (rt->trapped) { free(out); return 0; }
        if (!saddr) break;
        out[count] = (char *)guest_cstr(rt, saddr);
        if (!out[count]) { free(out); return 0; }
        ++count;
    }
    out[count] = 0;
    return out;
}

static void bridge_XpmCreatePixmapFromData(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    Drawable drawable = (Drawable)(uint32_t)cpu->gpr[5];
    IrixGuestAddr data_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr pixmap_out = (uint32_t)cpu->gpr[7];
    IrixGuestAddr mask_out = (uint32_t)cpu->gpr[8];
    IrixGuestAddr attrs_addr = (uint32_t)cpu->gpr[9];
    char **data = 0;
    Pixmap pixmap = None, mask = None;
    XpmAttributes attrs;
    XpmAttributes *attrs_ptr = 0;
    int rc = XpmOpenFailed;

    if (!d) {
        cpu->gpr[2] = (uint64_t)(int64_t)rc;
        return;
    }

    data = xpm_data_from_guest(rt, data_addr);
    if (!data) {
        cpu->gpr[2] = (uint64_t)(int64_t)XpmFileInvalid;
        return;
    }

    if (attrs_addr) {
        unsigned long guest_mask = (unsigned long)irix_guest_read32be(rt, attrs_addr + 0u);
        memset(&attrs, 0, sizeof(attrs));
        if (guest_mask & XpmCloseness) {
            attrs.valuemask |= XpmCloseness;
            attrs.closeness = irix_guest_read32be(rt, attrs_addr + 92u);
        }
        if (rt->trapped) {
            free(data);
            cpu->gpr[2] = (uint64_t)(int64_t)XpmFileInvalid;
            return;
        }
        attrs_ptr = &attrs;
    }

    rc = XpmCreatePixmapFromData(d, drawable, data, &pixmap, &mask, attrs_ptr);
    if (pixmap_out) wr32(rt, pixmap_out, (uint32_t)pixmap);
    if (mask_out) wr32(rt, mask_out, (uint32_t)mask);
    if (attrs_ptr) XpmFreeAttributes(attrs_ptr);
    free(data);
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_XpmFreeAttributes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    /* Guest XpmAttributes never contains host pointers: the create bridge
       marshals into a temporary host struct and frees host-owned results there. */
    (void)rt;
    (void)cpu;
}
#endif

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr)
{
    return display_from_guest(addr);
}

Screen *irix_x11_primary_screen_from_guest(IrixGuestAddr display_addr,
                                           IrixGuestAddr screen_addr)
{
    Display *display = display_from_guest(display_addr);
    IrixGuestAddr screens;
    uint32_t count, index;
    if (!display || !g_x11.rt || !display_addr || !screen_addr)
        return NULL;
    count = irix_guest_read32be(g_x11.rt, display_addr + 136u);
    screens = irix_guest_read32be(g_x11.rt, display_addr + 140u);
    if (g_x11.rt->trapped || screen_addr < screens ||
        (screen_addr - screens) % 80u != 0u)
        return NULL;
    index = (screen_addr - screens) / 80u;
    if (index >= count || index >= (uint32_t)ScreenCount(display))
        return NULL;
    return ScreenOfDisplay(display, (int)index);
}

GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr)
{
    return gc_from_guest(addr);
}

XFontStruct *irix_x11_primary_font_from_guest(IrixGuestAddr addr)
{
    size_t i;
    for (i = 0; i < g_x11.font_count; ++i)
        if (g_x11.font_guest[i] == addr)
            return g_x11.fonts[i];
    return 0;
}

IrixGuestAddr irix_x11_primary_font_to_guest(IrixGuestRuntime *rt, XFontStruct *font)
{
    return font_to_guest(rt, font);
}

void irix_x11_primary_forget_font(IrixGuestAddr addr)
{
    size_t i;
    for (i = 0; i < g_x11.font_count; ++i) {
        if (g_x11.font_guest[i] == addr) {
            g_x11.fonts[i] = 0;
            g_x11.font_guest[i] = 0;
            return;
        }
    }
}

IrixGuestFunction irix_x11_extra_lookup(const char *name)
{
#ifdef IRIX_HAVE_XPM
    if (name && !strcmp(name, "XpmCreatePixmapFromData")) return bridge_XpmCreatePixmapFromData;
    if (name && !strcmp(name, "XpmFreeAttributes")) return bridge_XpmFreeAttributes;
#endif
    return irix_x11_extra_lookup_base(name);
}
