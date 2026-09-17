#include "IrixX11WindowBridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_WINDOW_MAX_DISPLAYS 16u
#define IRIX_N32_XSETWINDOWATTRIBUTES_SIZE 60u
#define IRIX_N32_XWINDOWCHANGES_SIZE 28u
#define IRIX_X11_WINDOW_TMP_BASE 0x72900000u
#define IRIX_X11_WINDOW_TMP_SIZE 0x00100000u
#define IRIX_X11_FONTSET_HANDLE_BASE 0x72a00000u
#define IRIX_X11_MAX_FONTSETS 256u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr guest_display[IRIX_X11_WINDOW_MAX_DISPLAYS];
    Display *host_display[IRIX_X11_WINDOW_MAX_DISPLAYS];
    size_t display_count;
    XFontSet fontsets[IRIX_X11_MAX_FONTSETS];
    size_t fontset_count;
    IrixGuestAddr tmp_cur;
} IrixX11WindowState;

static IrixX11WindowState g_window;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    if (addr) irix_guest_write32be(rt, addr, value);
}

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || !g_window.tmp_cur || g_window.tmp_cur + n < g_window.tmp_cur ||
        g_window.tmp_cur + n > IRIX_X11_WINDOW_TMP_BASE + IRIX_X11_WINDOW_TMP_SIZE)
        return 0;
    a = g_window.tmp_cur;
    g_window.tmp_cur += n;
    return a;
}

static IrixGuestAddr copy_host_string(IrixGuestRuntime *rt, const char *s)
{
    IrixGuestAddr out;
    size_t n;
    void *p;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    out = tmp_alloc((uint32_t)n);
    if (!out) return 0;
    p = irix_guest_ptr(rt, out, n);
    if (!p) return 0;
    memcpy(p, s, n);
    return out;
}

static Display *fallback_display(IrixGuestRuntime *rt, IrixGuestAddr guest)
{
    size_t i;
    IrixGuestAddr name_addr;
    const char *name = 0;

    if (!guest) return 0;
    for (i = 0; i < g_window.display_count; ++i)
        if (g_window.guest_display[i] == guest)
            return g_window.host_display[i];
    if (g_window.display_count >= IRIX_X11_WINDOW_MAX_DISPLAYS) return 0;

    /* The IRIX Display shadow built by IrixX11BridgeExtra.c stores
       display_name at offset 128 in the classic 32-bit Xlib layout. */
    name_addr = irix_guest_read32be(rt, guest + 128u);
    if (!rt->trapped) name = guest_cstr(rt, name_addr);
    if (rt->trapped) return 0;

    i = g_window.display_count++;
    g_window.guest_display[i] = guest;
    g_window.host_display[i] = XOpenDisplay(name);
    return g_window.host_display[i];
}

static Visual *visual_from_guest(IrixGuestRuntime *rt, Display *dpy, IrixGuestAddr guest_visual)
{
    XVisualInfo templ;
    XVisualInfo *infos;
    Visual *visual = 0;
    int count = 0;

    if (!guest_visual) return (Visual *)CopyFromParent;
    if (!dpy || !irix_guest_ptr(rt, guest_visual, 32u)) return 0;

    memset(&templ, 0, sizeof(templ));
    templ.visualid = (VisualID)irix_guest_read32be(rt, guest_visual + 4u);
    if (rt->trapped) return 0;

    infos = XGetVisualInfo(dpy, VisualIDMask, &templ, &count);
    if (infos && count > 0)
        visual = infos[0].visual;
    if (infos) XFree(infos);
    return visual;
}

static int read_window_attributes(IrixGuestRuntime *rt, IrixGuestAddr addr,
                                  XSetWindowAttributes *out)
{
    if (!out || !addr ||
        !irix_guest_ptr(rt, addr, IRIX_N32_XSETWINDOWATTRIBUTES_SIZE))
        return 0;

    memset(out, 0, sizeof(*out));
    out->background_pixmap = (Pixmap)irix_guest_read32be(rt, addr + 0u);
    out->background_pixel = (unsigned long)irix_guest_read32be(rt, addr + 4u);
    out->border_pixmap = (Pixmap)irix_guest_read32be(rt, addr + 8u);
    out->border_pixel = (unsigned long)irix_guest_read32be(rt, addr + 12u);
    out->bit_gravity = (int32_t)irix_guest_read32be(rt, addr + 16u);
    out->win_gravity = (int32_t)irix_guest_read32be(rt, addr + 20u);
    out->backing_store = (int32_t)irix_guest_read32be(rt, addr + 24u);
    out->backing_planes = (unsigned long)irix_guest_read32be(rt, addr + 28u);
    out->backing_pixel = (unsigned long)irix_guest_read32be(rt, addr + 32u);
    out->save_under = (Bool)(int32_t)irix_guest_read32be(rt, addr + 36u);
    out->event_mask = (long)(uint32_t)irix_guest_read32be(rt, addr + 40u);
    out->do_not_propagate_mask = (long)(uint32_t)irix_guest_read32be(rt, addr + 44u);
    out->override_redirect = (Bool)(int32_t)irix_guest_read32be(rt, addr + 48u);
    out->colormap = (Colormap)irix_guest_read32be(rt, addr + 52u);
    out->cursor = (Cursor)irix_guest_read32be(rt, addr + 56u);
    return !rt->trapped;
}

static int read_window_changes(IrixGuestRuntime *rt, IrixGuestAddr addr,
                               XWindowChanges *out)
{
    if (!out || !addr || !irix_guest_ptr(rt, addr, IRIX_N32_XWINDOWCHANGES_SIZE))
        return 0;
    memset(out, 0, sizeof(*out));
    out->x = (int32_t)irix_guest_read32be(rt, addr + 0u);
    out->y = (int32_t)irix_guest_read32be(rt, addr + 4u);
    out->width = (int32_t)irix_guest_read32be(rt, addr + 8u);
    out->height = (int32_t)irix_guest_read32be(rt, addr + 12u);
    out->border_width = (int32_t)irix_guest_read32be(rt, addr + 16u);
    out->sibling = (Window)irix_guest_read32be(rt, addr + 20u);
    out->stack_mode = (int32_t)irix_guest_read32be(rt, addr + 24u);
    return !rt->trapped;
}

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
}

static IrixGuestAddr fontset_to_guest(XFontSet set)
{
    size_t i;
    if (!set) return 0;
    for (i = 0; i < g_window.fontset_count; ++i)
        if (g_window.fontsets[i] == set)
            return IRIX_X11_FONTSET_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
    if (g_window.fontset_count >= IRIX_X11_MAX_FONTSETS) return 0;
    i = g_window.fontset_count++;
    g_window.fontsets[i] = set;
    return IRIX_X11_FONTSET_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
}

static XFontSet fontset_from_guest(IrixGuestAddr addr)
{
    uint32_t i;
    if (addr < IRIX_X11_FONTSET_HANDLE_BASE) return 0;
    i = (addr - IRIX_X11_FONTSET_HANDLE_BASE) / 4u;
    return i < g_window.fontset_count ? g_window.fontsets[i] : 0;
}

static void write_char_struct(IrixGuestRuntime *rt, IrixGuestAddr addr,
                              const XCharStruct *c)
{
    if (!addr || !c || !irix_guest_ptr(rt, addr, 12u)) return;
    irix_guest_write16be(rt, addr + 0u, (uint16_t)c->lbearing);
    irix_guest_write16be(rt, addr + 2u, (uint16_t)c->rbearing);
    irix_guest_write16be(rt, addr + 4u, (uint16_t)c->width);
    irix_guest_write16be(rt, addr + 6u, (uint16_t)c->ascent);
    irix_guest_write16be(rt, addr + 8u, (uint16_t)c->descent);
    irix_guest_write16be(rt, addr + 10u, c->attributes);
}

static IrixGuestAddr font_to_guest(IrixGuestRuntime *rt, const XFontStruct *f)
{
    IrixGuestAddr out, props = 0, per = 0;
    uint64_t rows, cols, nchar = 0;
    size_t i;

    if (!f) return 0;
    out = tmp_alloc(80u);
    if (!out) return 0;

    if (f->n_properties > 0 && f->properties) {
        uint64_t nbytes = (uint64_t)(unsigned)f->n_properties * 8u;
        if (nbytes > UINT32_MAX) return 0;
        props = tmp_alloc((uint32_t)nbytes);
        if (!props) return 0;
        for (i = 0; i < (size_t)f->n_properties; ++i) {
            wr32(rt, props + (IrixGuestAddr)i * 8u + 0u,
                 (uint32_t)f->properties[i].name);
            wr32(rt, props + (IrixGuestAddr)i * 8u + 4u,
                 (uint32_t)f->properties[i].card32);
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
    return out;
}

static void write_xrectangle(IrixGuestRuntime *rt, IrixGuestAddr addr,
                             const XRectangle *r)
{
    if (!addr || !r || !irix_guest_ptr(rt, addr, 8u)) return;
    irix_guest_write16be(rt, addr + 0u, (uint16_t)r->x);
    irix_guest_write16be(rt, addr + 2u, (uint16_t)r->y);
    irix_guest_write16be(rt, addr + 4u, r->width);
    irix_guest_write16be(rt, addr + 6u, r->height);
}

static void bridge_XCreateWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window parent = (Window)(uint32_t)cpu->gpr[5];
    int x = (int32_t)cpu->gpr[6];
    int y = (int32_t)cpu->gpr[7];
    unsigned int width = (uint32_t)cpu->gpr[8];
    unsigned int height = (uint32_t)cpu->gpr[9];
    unsigned int border_width = (uint32_t)cpu->gpr[10];
    int depth = (int32_t)cpu->gpr[11];
    unsigned int class_value = n32_stack_arg32(rt, cpu, 0u);
    IrixGuestAddr guest_visual = n32_stack_arg32(rt, cpu, 1u);
    unsigned long valuemask = (unsigned long)n32_stack_arg32(rt, cpu, 2u);
    IrixGuestAddr guest_attrs = n32_stack_arg32(rt, cpu, 3u);
    XSetWindowAttributes attrs;
    XSetWindowAttributes *attrs_ptr = 0;
    Visual *visual;
    Window w = 0;

    if (!dpy || rt->trapped) { cpu->gpr[2] = 0; return; }
    visual = visual_from_guest(rt, dpy, guest_visual);
    if (guest_visual && !visual) { cpu->gpr[2] = 0; return; }

    if (valuemask) {
        if (!read_window_attributes(rt, guest_attrs, &attrs)) {
            cpu->gpr[2] = 0;
            return;
        }
        attrs_ptr = &attrs;
    }

    w = XCreateWindow(dpy, parent, x, y, width, height, border_width,
                      depth, class_value, visual, valuemask, attrs_ptr);
    if (w) XFlush(dpy);
    cpu->gpr[2] = (uint32_t)w;
}

static void bridge_XChangeWindowAttributes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    unsigned long valuemask = (unsigned long)(uint32_t)cpu->gpr[6];
    IrixGuestAddr guest_attrs = (uint32_t)cpu->gpr[7];
    XSetWindowAttributes attrs;
    int rc = 0;

    if (!dpy) { cpu->gpr[2] = 0; return; }
    if (!valuemask) {
        memset(&attrs, 0, sizeof(attrs));
        cpu->gpr[2] = (uint32_t)XChangeWindowAttributes(dpy, w, 0, &attrs);
        return;
    }
    if (!read_window_attributes(rt, guest_attrs, &attrs)) {
        cpu->gpr[2] = 0;
        return;
    }
    rc = XChangeWindowAttributes(dpy, w, valuemask, &attrs);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XConfigureWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    XWindowChanges changes;
    if (!dpy || !read_window_changes(rt, (uint32_t)cpu->gpr[7], &changes)) {
        cpu->gpr[2] = 0;
        return;
    }
    cpu->gpr[2] = (uint32_t)XConfigureWindow(dpy, (Window)(uint32_t)cpu->gpr[5],
                                               (unsigned int)(uint32_t)cpu->gpr[6], &changes);
}

#define SIMPLE_WINDOW_CALL(name, expr) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]); \
    cpu->gpr[2] = dpy ? (uint32_t)(expr) : 0; \
}

SIMPLE_WINDOW_CALL(bridge_XMapSubwindows,
                   XMapSubwindows(dpy, (Window)(uint32_t)cpu->gpr[5]))
SIMPLE_WINDOW_CALL(bridge_XUnmapSubwindows,
                   XUnmapSubwindows(dpy, (Window)(uint32_t)cpu->gpr[5]))
SIMPLE_WINDOW_CALL(bridge_XReparentWindow,
                   XReparentWindow(dpy, (Window)(uint32_t)cpu->gpr[5],
                                   (Window)(uint32_t)cpu->gpr[6],
                                   (int32_t)cpu->gpr[7], (int32_t)cpu->gpr[8]))
SIMPLE_WINDOW_CALL(bridge_XSetWindowBorderWidth,
                   XSetWindowBorderWidth(dpy, (Window)(uint32_t)cpu->gpr[5],
                                         (unsigned int)(uint32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XSetWindowColormap,
                   XSetWindowColormap(dpy, (Window)(uint32_t)cpu->gpr[5],
                                      (Colormap)(uint32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XSetWindowBackgroundPixmap,
                   XSetWindowBackgroundPixmap(dpy, (Window)(uint32_t)cpu->gpr[5],
                                              (Pixmap)(uint32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XSetWindowBorderPixmap,
                   XSetWindowBorderPixmap(dpy, (Window)(uint32_t)cpu->gpr[5],
                                          (Pixmap)(uint32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XAddToSaveSet,
                   XAddToSaveSet(dpy, (Window)(uint32_t)cpu->gpr[5]))
SIMPLE_WINDOW_CALL(bridge_XRemoveFromSaveSet,
                   XRemoveFromSaveSet(dpy, (Window)(uint32_t)cpu->gpr[5]))
SIMPLE_WINDOW_CALL(bridge_XChangeSaveSet,
                   XChangeSaveSet(dpy, (Window)(uint32_t)cpu->gpr[5],
                                  (int32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XCirculateSubwindows,
                   XCirculateSubwindows(dpy, (Window)(uint32_t)cpu->gpr[5],
                                        (int32_t)cpu->gpr[6]))
SIMPLE_WINDOW_CALL(bridge_XCirculateSubwindowsUp,
                   XCirculateSubwindowsUp(dpy, (Window)(uint32_t)cpu->gpr[5]))
SIMPLE_WINDOW_CALL(bridge_XCirculateSubwindowsDown,
                   XCirculateSubwindowsDown(dpy, (Window)(uint32_t)cpu->gpr[5]))

static void bridge_XRestackWindows(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr guest_windows = (uint32_t)cpu->gpr[5];
    int count = (int32_t)cpu->gpr[6];
    Window *windows = 0;
    int i, rc = 0;
    if (!dpy || count < 0) { cpu->gpr[2] = 0; return; }
    if (count > 0) {
        if (!irix_guest_ptr(rt, guest_windows, (size_t)count * 4u)) {
            cpu->gpr[2] = 0;
            return;
        }
        windows = (Window *)calloc((size_t)count, sizeof(*windows));
        if (!windows) { cpu->gpr[2] = 0; return; }
        for (i = 0; i < count; ++i)
            windows[i] = (Window)irix_guest_read32be(rt, guest_windows + (IrixGuestAddr)i * 4u);
    }
    if (!rt->trapped) rc = XRestackWindows(dpy, windows, count);
    free(windows);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XQueryTree(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window root = 0, parent = 0, *children = 0;
    unsigned int nchildren = 0;
    IrixGuestAddr guest_children = 0;
    Status ok = 0;
    unsigned int i;

    if (dpy)
        ok = XQueryTree(dpy, (Window)(uint32_t)cpu->gpr[5],
                        &root, &parent, &children, &nchildren);
    if (ok && children && nchildren) {
        if ((uint64_t)nchildren * 4u <= UINT32_MAX)
            guest_children = tmp_alloc(nchildren * 4u);
        if (guest_children) {
            for (i = 0; i < nchildren; ++i)
                wr32(rt, guest_children + (IrixGuestAddr)i * 4u,
                     (uint32_t)children[i]);
        } else {
            ok = 0;
            nchildren = 0;
        }
    }
    if (children) XFree(children);
    wr32(rt, (uint32_t)cpu->gpr[6], (uint32_t)root);
    wr32(rt, (uint32_t)cpu->gpr[7], (uint32_t)parent);
    wr32(rt, (uint32_t)cpu->gpr[8], guest_children);
    wr32(rt, (uint32_t)cpu->gpr[9], nchildren);
    cpu->gpr[2] = (uint32_t)ok;
}

static void bridge_XCreateFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    const char *base_name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    char **missing = 0;
    int missing_count = 0;
    char *def_string = 0;
    XFontSet set = 0;
    IrixGuestAddr guest_missing = 0, guest_def = 0;
    int i;

    if (dpy && base_name)
        set = XCreateFontSet(dpy, (char *)base_name, &missing,
                             &missing_count, &def_string);

    if (missing_count > 0 && missing) {
        guest_missing = tmp_alloc((uint32_t)missing_count * 4u);
        if (guest_missing) {
            for (i = 0; i < missing_count; ++i)
                wr32(rt, guest_missing + (IrixGuestAddr)i * 4u,
                     copy_host_string(rt, missing[i]));
        }
    }
    if (def_string) guest_def = copy_host_string(rt, def_string);
    if (missing) XFreeStringList(missing);

    wr32(rt, (uint32_t)cpu->gpr[6], guest_missing);
    wr32(rt, (uint32_t)cpu->gpr[7], (uint32_t)missing_count);
    wr32(rt, (uint32_t)cpu->gpr[8], guest_def);
    cpu->gpr[2] = fontset_to_guest(set);
}

static void bridge_XFreeFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr guest = (uint32_t)cpu->gpr[5];
    XFontSet set = fontset_from_guest(guest);
    uint32_t i;
    if (dpy && set) {
        XFreeFontSet(dpy, set);
        i = (guest - IRIX_X11_FONTSET_HANDLE_BASE) / 4u;
        if (i < g_window.fontset_count) g_window.fontsets[i] = 0;
    }
    cpu->gpr[2] = 0;
}

static void bridge_XBaseFontNameListOfFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XFontSet set = fontset_from_guest((uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = set ? copy_host_string(rt, XBaseFontNameListOfFontSet(set)) : 0;
}

static void bridge_XLocaleOfFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XFontSet set = fontset_from_guest((uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = set ? copy_host_string(rt, XLocaleOfFontSet(set)) : 0;
}

static void bridge_XFontsOfFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XFontSet set = fontset_from_guest((uint32_t)cpu->gpr[4]);
    XFontStruct **fonts = 0;
    char **names = 0;
    IrixGuestAddr guest_fonts = 0, guest_names = 0;
    int count = 0, i;

    if (set)
        count = XFontsOfFontSet(set, &fonts, &names);
    if (count > 0) {
        if ((uint64_t)(unsigned)count * 4u <= UINT32_MAX) {
            guest_fonts = tmp_alloc((uint32_t)count * 4u);
            guest_names = tmp_alloc((uint32_t)count * 4u);
        }
        if (!guest_fonts || !guest_names) {
            count = 0;
            guest_fonts = 0;
            guest_names = 0;
        } else {
            for (i = 0; i < count; ++i) {
                IrixGuestAddr gf = font_to_guest(rt, fonts ? fonts[i] : 0);
                IrixGuestAddr gn = copy_host_string(rt, names ? names[i] : 0);
                wr32(rt, guest_fonts + (IrixGuestAddr)i * 4u, gf);
                wr32(rt, guest_names + (IrixGuestAddr)i * 4u, gn);
                if (!gf || rt->trapped) {
                    count = 0;
                    guest_fonts = 0;
                    guest_names = 0;
                    break;
                }
            }
        }
    }

    wr32(rt, (uint32_t)cpu->gpr[5], guest_fonts);
    wr32(rt, (uint32_t)cpu->gpr[6], guest_names);
    cpu->gpr[2] = (uint64_t)(int64_t)count;
}

static void bridge_XExtentsOfFontSet(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XFontSet set = fontset_from_guest((uint32_t)cpu->gpr[4]);
    XFontSetExtents *e = set ? XExtentsOfFontSet(set) : 0;
    IrixGuestAddr out = 0;
    if (e) {
        out = tmp_alloc(16u);
        if (out) {
            write_xrectangle(rt, out + 0u, &e->max_ink_extent);
            write_xrectangle(rt, out + 8u, &e->max_logical_extent);
        }
    }
    cpu->gpr[2] = out;
}

static void bridge_XmbTextExtents(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XFontSet set = fontset_from_guest((uint32_t)cpu->gpr[4]);
    const char *text = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    int bytes = (int32_t)cpu->gpr[6];
    XRectangle ink, logical;
    int rc = 0;
    memset(&ink, 0, sizeof(ink));
    memset(&logical, 0, sizeof(logical));
    if (set && text)
        rc = XmbTextExtents(set, text, bytes, &ink, &logical);
    write_xrectangle(rt, (uint32_t)cpu->gpr[7], &ink);
    write_xrectangle(rt, (uint32_t)cpu->gpr[8], &logical);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XFreeStringList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu->gpr[4];
    /* Font-set string lists are copied into the monotonic guest scratch arena;
       the corresponding host list is freed immediately after marshalling. */
    cpu->gpr[2] = 0;
}

void irix_x11_window_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_window, 0, sizeof(g_window));
    g_window.rt = rt;
    if (irix_guest_map(rt, IRIX_X11_WINDOW_TMP_BASE, IRIX_X11_WINDOW_TMP_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_X11_WINDOW_TMP_BASE, 1))
        g_window.tmp_cur = IRIX_X11_WINDOW_TMP_BASE;
}

IrixGuestFunction irix_x11_window_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XCreateWindow")) return bridge_XCreateWindow;
    if (!strcmp(name, "XChangeWindowAttributes")) return bridge_XChangeWindowAttributes;
    if (!strcmp(name, "XConfigureWindow")) return bridge_XConfigureWindow;
    if (!strcmp(name, "XMapSubwindows")) return bridge_XMapSubwindows;
    if (!strcmp(name, "XUnmapSubwindows")) return bridge_XUnmapSubwindows;
    if (!strcmp(name, "XReparentWindow")) return bridge_XReparentWindow;
    if (!strcmp(name, "XSetWindowBorderWidth")) return bridge_XSetWindowBorderWidth;
    if (!strcmp(name, "XSetWindowColormap")) return bridge_XSetWindowColormap;
    if (!strcmp(name, "XSetWindowBackgroundPixmap")) return bridge_XSetWindowBackgroundPixmap;
    if (!strcmp(name, "XSetWindowBorderPixmap")) return bridge_XSetWindowBorderPixmap;
    if (!strcmp(name, "XAddToSaveSet")) return bridge_XAddToSaveSet;
    if (!strcmp(name, "XRemoveFromSaveSet")) return bridge_XRemoveFromSaveSet;
    if (!strcmp(name, "XChangeSaveSet")) return bridge_XChangeSaveSet;
    if (!strcmp(name, "XCirculateSubwindows")) return bridge_XCirculateSubwindows;
    if (!strcmp(name, "XCirculateSubwindowsUp")) return bridge_XCirculateSubwindowsUp;
    if (!strcmp(name, "XCirculateSubwindowsDown")) return bridge_XCirculateSubwindowsDown;
    if (!strcmp(name, "XRestackWindows")) return bridge_XRestackWindows;
    if (!strcmp(name, "XQueryTree")) return bridge_XQueryTree;
    if (!strcmp(name, "XCreateFontSet")) return bridge_XCreateFontSet;
    if (!strcmp(name, "XFreeFontSet")) return bridge_XFreeFontSet;
    if (!strcmp(name, "XBaseFontNameListOfFontSet")) return bridge_XBaseFontNameListOfFontSet;
    if (!strcmp(name, "XLocaleOfFontSet")) return bridge_XLocaleOfFontSet;
    if (!strcmp(name, "XFontsOfFontSet")) return bridge_XFontsOfFontSet;
    if (!strcmp(name, "XExtentsOfFontSet")) return bridge_XExtentsOfFontSet;
    if (!strcmp(name, "XmbTextExtents")) return bridge_XmbTextExtents;
    if (!strcmp(name, "XFreeStringList")) return bridge_XFreeStringList;
    return 0;
}
