#include "IrixX11WmBridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_WM_MAX_DISPLAYS 16u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr guest_display[IRIX_X11_WM_MAX_DISPLAYS];
    Display *host_display[IRIX_X11_WM_MAX_DISPLAYS];
    size_t display_count;
} IrixX11WmState;

static IrixX11WmState g_wm;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static Display *fallback_display(IrixGuestRuntime *rt, IrixGuestAddr guest)
{
    size_t i;
    IrixGuestAddr name_addr;
    const char *name = 0;

    if (!guest) return 0;
    for (i = 0; i < g_wm.display_count; ++i)
        if (g_wm.guest_display[i] == guest)
            return g_wm.host_display[i];
    if (g_wm.display_count >= IRIX_X11_WM_MAX_DISPLAYS) return 0;

    name_addr = irix_guest_read32be(rt, guest + 128u);
    if (!rt->trapped) name = guest_cstr(rt, name_addr);
    if (rt->trapped) return 0;

    i = g_wm.display_count++;
    g_wm.guest_display[i] = guest;
    g_wm.host_display[i] = XOpenDisplay(name);
    return g_wm.host_display[i];
}

static int read_text_property(IrixGuestRuntime *rt, IrixGuestAddr addr, XTextProperty *out)
{
    IrixGuestAddr value;
    unsigned long nitems;
    size_t bytes = 0;

    if (!out || !addr || !irix_guest_ptr(rt, addr, 16u)) return 0;
    memset(out, 0, sizeof(*out));
    value = irix_guest_read32be(rt, addr + 0u);
    out->encoding = (Atom)irix_guest_read32be(rt, addr + 4u);
    out->format = (int32_t)irix_guest_read32be(rt, addr + 8u);
    nitems = (unsigned long)irix_guest_read32be(rt, addr + 12u);
    out->nitems = nitems;
    if (rt->trapped) return 0;

    if (value && nitems) {
        if (out->format == 8) bytes = (size_t)nitems;
        else if (out->format == 16) bytes = (size_t)nitems * 2u;
        else if (out->format == 32) bytes = (size_t)nitems * 4u;
        else return 0;
        out->value = (unsigned char *)irix_guest_ptr(rt, value, bytes ? bytes : 1u);
        if (!out->value) return 0;
    }
    return 1;
}

static int read_size_hints(IrixGuestRuntime *rt, IrixGuestAddr addr, XSizeHints *h)
{
    if (!h || !addr || !irix_guest_ptr(rt, addr, 72u)) return 0;
    memset(h, 0, sizeof(*h));
    h->flags = (long)(int32_t)irix_guest_read32be(rt, addr + 0u);
    h->x = (int32_t)irix_guest_read32be(rt, addr + 4u);
    h->y = (int32_t)irix_guest_read32be(rt, addr + 8u);
    h->width = (int32_t)irix_guest_read32be(rt, addr + 12u);
    h->height = (int32_t)irix_guest_read32be(rt, addr + 16u);
    h->min_width = (int32_t)irix_guest_read32be(rt, addr + 20u);
    h->min_height = (int32_t)irix_guest_read32be(rt, addr + 24u);
    h->max_width = (int32_t)irix_guest_read32be(rt, addr + 28u);
    h->max_height = (int32_t)irix_guest_read32be(rt, addr + 32u);
    h->width_inc = (int32_t)irix_guest_read32be(rt, addr + 36u);
    h->height_inc = (int32_t)irix_guest_read32be(rt, addr + 40u);
    h->min_aspect.x = (int32_t)irix_guest_read32be(rt, addr + 44u);
    h->min_aspect.y = (int32_t)irix_guest_read32be(rt, addr + 48u);
    h->max_aspect.x = (int32_t)irix_guest_read32be(rt, addr + 52u);
    h->max_aspect.y = (int32_t)irix_guest_read32be(rt, addr + 56u);
    h->base_width = (int32_t)irix_guest_read32be(rt, addr + 60u);
    h->base_height = (int32_t)irix_guest_read32be(rt, addr + 64u);
    h->win_gravity = (int32_t)irix_guest_read32be(rt, addr + 68u);
    return !rt->trapped;
}

static int read_wm_hints(IrixGuestRuntime *rt, IrixGuestAddr addr, XWMHints *h)
{
    if (!h || !addr || !irix_guest_ptr(rt, addr, 36u)) return 0;
    memset(h, 0, sizeof(*h));
    h->flags = (long)(int32_t)irix_guest_read32be(rt, addr + 0u);
    h->input = (Bool)(int32_t)irix_guest_read32be(rt, addr + 4u);
    h->initial_state = (int32_t)irix_guest_read32be(rt, addr + 8u);
    h->icon_pixmap = (Pixmap)irix_guest_read32be(rt, addr + 12u);
    h->icon_window = (Window)irix_guest_read32be(rt, addr + 16u);
    h->icon_x = (int32_t)irix_guest_read32be(rt, addr + 20u);
    h->icon_y = (int32_t)irix_guest_read32be(rt, addr + 24u);
    h->icon_mask = (Pixmap)irix_guest_read32be(rt, addr + 28u);
    h->window_group = (XID)irix_guest_read32be(rt, addr + 32u);
    return !rt->trapped;
}

static int read_class_hint(IrixGuestRuntime *rt, IrixGuestAddr addr, XClassHint *h)
{
    if (!h || !addr || !irix_guest_ptr(rt, addr, 8u)) return 0;
    h->res_name = (char *)guest_cstr(rt, irix_guest_read32be(rt, addr + 0u));
    h->res_class = (char *)guest_cstr(rt, irix_guest_read32be(rt, addr + 4u));
    return !rt->trapped;
}

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
}

static char **read_argv(IrixGuestRuntime *rt, IrixGuestAddr guest_argv, int argc)
{
    char **argv;
    int i;
    if (argc <= 0) return 0;
    if (!guest_argv || argc > 4096) return 0;
    argv = (char **)calloc((size_t)argc + 1u, sizeof(*argv));
    if (!argv) return 0;
    for (i = 0; i < argc; ++i) {
        IrixGuestAddr s = irix_guest_read32be(rt, guest_argv + (IrixGuestAddr)i * 4u);
        argv[i] = (char *)guest_cstr(rt, s);
        if (!argv[i]) { free(argv); return 0; }
    }
    return argv;
}

static void bridge_XSetWMProperties(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    IrixGuestAddr wn_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr in_addr = (uint32_t)cpu->gpr[7];
    IrixGuestAddr argv_addr = (uint32_t)cpu->gpr[8];
    int argc = (int32_t)cpu->gpr[9];
    IrixGuestAddr sh_addr = (uint32_t)cpu->gpr[10];
    IrixGuestAddr wh_addr = (uint32_t)cpu->gpr[11];
    IrixGuestAddr ch_addr = n32_stack_arg32(rt, cpu, 0u);
    XTextProperty wn, in;
    XSizeHints sh;
    XWMHints wh;
    XClassHint ch;
    XTextProperty *wnp = 0, *inp = 0;
    XSizeHints *shp = 0;
    XWMHints *whp = 0;
    XClassHint *chp = 0;
    char **argv = 0;

    if (!d || rt->trapped || argc < 0) { cpu->gpr[2] = 0; return; }
    if (wn_addr) { if (!read_text_property(rt, wn_addr, &wn)) { cpu->gpr[2] = 0; return; } wnp = &wn; }
    if (in_addr) { if (!read_text_property(rt, in_addr, &in)) { cpu->gpr[2] = 0; return; } inp = &in; }
    if (sh_addr) { if (!read_size_hints(rt, sh_addr, &sh)) { cpu->gpr[2] = 0; return; } shp = &sh; }
    if (wh_addr) { if (!read_wm_hints(rt, wh_addr, &wh)) { cpu->gpr[2] = 0; return; } whp = &wh; }
    if (ch_addr) { if (!read_class_hint(rt, ch_addr, &ch)) { cpu->gpr[2] = 0; return; } chp = &ch; }
    if (argc > 0) {
        argv = read_argv(rt, argv_addr, argc);
        if (!argv) { cpu->gpr[2] = 0; return; }
    }

    XSetWMProperties(d, w, wnp, inp, argv, argc, shp, whp, chp);
    free(argv);
    cpu->gpr[2] = 0;
}

static void bridge_XSetWMName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    XTextProperty p;
    if (d && read_text_property(rt, (uint32_t)cpu->gpr[6], &p)) XSetWMName(d, (Window)(uint32_t)cpu->gpr[5], &p);
    cpu->gpr[2] = 0;
}

static void bridge_XSetWMNormalHints(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    XSizeHints h;
    if (d && read_size_hints(rt, (uint32_t)cpu->gpr[6], &h)) XSetWMNormalHints(d, (Window)(uint32_t)cpu->gpr[5], &h);
    cpu->gpr[2] = 0;
}

static void bridge_XSetWMHints(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    XWMHints h;
    cpu->gpr[2] = (d && read_wm_hints(rt, (uint32_t)cpu->gpr[6], &h)) ?
        (uint32_t)XSetWMHints(d, (Window)(uint32_t)cpu->gpr[5], &h) : 0;
}

static void bridge_XSetClassHint(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    XClassHint h;
    cpu->gpr[2] = (d && read_class_hint(rt, (uint32_t)cpu->gpr[6], &h)) ?
        (uint32_t)XSetClassHint(d, (Window)(uint32_t)cpu->gpr[5], &h) : 0;
}

void irix_x11_wm_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_wm, 0, sizeof(g_wm));
    g_wm.rt = rt;
}

IrixGuestFunction irix_x11_wm_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XSetWMProperties")) return bridge_XSetWMProperties;
    if (!strcmp(name, "XSetWMName")) return bridge_XSetWMName;
    if (!strcmp(name, "XSetWMNormalHints")) return bridge_XSetWMNormalHints;
    if (!strcmp(name, "XSetWMHints")) return bridge_XSetWMHints;
    if (!strcmp(name, "XSetClassHint")) return bridge_XSetClassHint;
    return 0;
}
