#define irix_x11_bridge_init irix_x11_bridge_init_base
#define irix_x11_bridge_lookup irix_x11_bridge_lookup_base
#include "IrixX11Bridge.c"
#undef irix_x11_bridge_lookup
#undef irix_x11_bridge_init

#include "IrixX11BridgeExtraAccess.h"

#define IRIX_XIM_BASE 0x72d00000u
#define IRIX_XIC_BASE 0x72e00000u
#define IRIX_XVA_BASE 0x72e80000u
#define IRIX_XIM_MAX 64u
#define IRIX_XIC_MAX 256u
#define IRIX_XVA_MAX 256u

typedef struct {
    XIM ims[IRIX_XIM_MAX];
    XIC ics[IRIX_XIC_MAX];
    unsigned char nested_used[IRIX_XVA_MAX];
    size_t im_count;
    size_t ic_count;
} IrixXimState;

static IrixXimState g_xim;

static IrixGuestAddr unit_copy_string(IrixGuestRuntime *rt, const char *s)
{
    IrixGuestAddr a;
    size_t n;
    void *p;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    a = value_alloc(rt, (uint32_t)n);
    if (!a) return 0;
    p = irix_guest_ptr(rt, a, n);
    if (!p) return 0;
    memcpy(p, s, n);
    return a;
}

static uint32_t unit_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    if (index < 8u) return (uint32_t)cpu->gpr[4u + index];
    return (uint32_t)irix_guest_read64be(rt,
        (IrixGuestAddr)(uint32_t)cpu->gpr[29] + (IrixGuestAddr)((index - 8u) * 8u));
}

static IrixGuestAddr xim_to_guest(XIM im)
{
    size_t i;
    if (!im) return 0;
    for (i = 0; i < g_xim.im_count; ++i)
        if (g_xim.ims[i] == im) return IRIX_XIM_BASE + (IrixGuestAddr)(i * 4u);
    if (g_xim.im_count >= IRIX_XIM_MAX) return 0;
    i = g_xim.im_count++;
    g_xim.ims[i] = im;
    return IRIX_XIM_BASE + (IrixGuestAddr)(i * 4u);
}

static XIM xim_from_guest(IrixGuestAddr a)
{
    uint32_t i;
    if (a < IRIX_XIM_BASE || a >= IRIX_XIM_BASE + IRIX_XIM_MAX * 4u) return 0;
    i = (a - IRIX_XIM_BASE) / 4u;
    return i < g_xim.im_count ? g_xim.ims[i] : 0;
}

static IrixGuestAddr xic_to_guest(XIC ic)
{
    size_t i;
    if (!ic) return 0;
    for (i = 0; i < g_xim.ic_count; ++i)
        if (g_xim.ics[i] == ic) return IRIX_XIC_BASE + (IrixGuestAddr)(i * 4u);
    if (g_xim.ic_count >= IRIX_XIC_MAX) return 0;
    i = g_xim.ic_count++;
    g_xim.ics[i] = ic;
    return IRIX_XIC_BASE + (IrixGuestAddr)(i * 4u);
}

static XIC xic_from_guest(IrixGuestAddr a)
{
    uint32_t i;
    if (a < IRIX_XIC_BASE || a >= IRIX_XIC_BASE + IRIX_XIC_MAX * 4u) return 0;
    i = (a - IRIX_XIC_BASE) / 4u;
    return i < g_xim.ic_count ? g_xim.ics[i] : 0;
}

static IrixGuestAddr nested_alloc(void)
{
    size_t i;
    for (i = 0; i < IRIX_XVA_MAX; ++i) {
        if (!g_xim.nested_used[i]) {
            g_xim.nested_used[i] = 1u;
            return IRIX_XVA_BASE + (IrixGuestAddr)(i * 4u);
        }
    }
    return 0;
}

static int nested_is_guest(IrixGuestAddr a)
{
    uint32_t i;
    if (a < IRIX_XVA_BASE || a >= IRIX_XVA_BASE + IRIX_XVA_MAX * 4u) return 0;
    i = (a - IRIX_XVA_BASE) / 4u;
    return g_xim.nested_used[i] ? 1 : 0;
}

static XPoint *unit_read_points(IrixGuestRuntime *rt, IrixGuestAddr addr, int n)
{
    XPoint *p;
    int i;
    if (n <= 0) return 0;
    if (!addr || !irix_guest_ptr(rt, addr, (size_t)n * 4u)) return 0;
    p = (XPoint *)calloc((size_t)n, sizeof(*p));
    if (!p) return 0;
    for (i = 0; i < n; ++i) {
        p[i].x = (short)irix_guest_read16be(rt, addr + (IrixGuestAddr)i * 4u + 0u);
        p[i].y = (short)irix_guest_read16be(rt, addr + (IrixGuestAddr)i * 4u + 2u);
    }
    return p;
}

static void bridge_unit_XPolygonRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int n = (int32_t)cpu->gpr[5];
    XPoint *points = unit_read_points(rt, (uint32_t)cpu->gpr[4], n);
    Region r = (n == 0 || points) ? XPolygonRegion(points, n, (int)cpu->gpr[6]) : 0;
    free(points);
    cpu->gpr[2] = region_to_guest(r);
}

static void bridge_unit_XSetRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);
    Region r = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (d && gc && r) ? (uint32_t)XSetRegion(d, gc, r) : 0;
}

static void bridge_unit_XGrabPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    int r = d ? XGrabPointer(
        d,
        (Window)(uint32_t)cpu->gpr[5],
        (Bool)cpu->gpr[6],
        (unsigned int)cpu->gpr[7],
        (int)cpu->gpr[8],
        (int)cpu->gpr[9],
        (Window)(uint32_t)cpu->gpr[10],
        (Cursor)(uint32_t)cpu->gpr[11],
        (Time)unit_arg32(rt, cpu, 8u)) : GrabNotViewable;
    irix_x11_after_pointer_grab(d, (Window)(uint32_t)cpu->gpr[5],
                                (unsigned int)cpu->gpr[7], r);
    cpu->gpr[2] = (uint32_t)r;
}

static void bridge_unit_XOpenIM(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    const char *rn = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    const char *rc = guest_cstr(rt, (uint32_t)cpu->gpr[7]);
    XIM im = d ? XOpenIM(d, 0, (char *)rn, (char *)rc) : 0;
    cpu->gpr[2] = xim_to_guest(im);
}

static void bridge_unit_XCloseIM(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr a = (uint32_t)cpu->gpr[4];
    XIM im = xim_from_guest(a);
    uint32_t i;
    (void)rt;
    cpu->gpr[2] = im ? (uint32_t)XCloseIM(im) : 0;
    if (im) {
        i = (a - IRIX_XIM_BASE) / 4u;
        if (i < g_xim.im_count) g_xim.ims[i] = 0;
    }
}

static void bridge_unit_XGetIMValues(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIM im = xim_from_guest((uint32_t)cpu->gpr[4]);
    unsigned ai = 1u;
    IrixGuestAddr failed = 0;
    if (!im) { cpu->gpr[2] = 0; return; }
    while (ai < 32u) {
        IrixGuestAddr name_addr = unit_arg32(rt, cpu, ai++);
        IrixGuestAddr out_addr;
        const char *name;
        char *bad = 0;
        if (!name_addr) break;
        out_addr = unit_arg32(rt, cpu, ai++);
        name = guest_cstr(rt, name_addr);
        if (!name) { failed = name_addr; break; }
        if (!strcmp(name, XNQueryInputStyle)) {
            XIMStyles *styles = 0;
            bad = XGetIMValues(im, XNQueryInputStyle, &styles, NULL);
            if (!bad && styles && out_addr) {
                IrixGuestAddr gs = value_alloc(rt, 8u);
                IrixGuestAddr list = 0;
                unsigned i;
                if (styles->count_styles) {
                    list = value_alloc(rt, (uint32_t)styles->count_styles * 4u);
                    if (list) for (i = 0; i < styles->count_styles; ++i)
                        write32(rt, list + (IrixGuestAddr)i * 4u,
                                (uint32_t)styles->supported_styles[i]);
                }
                if (gs) {
                    irix_guest_write16be(rt, gs + 0u, styles->count_styles);
                    irix_guest_write16be(rt, gs + 2u, 0u);
                    write32(rt, gs + 4u, list);
                    write32(rt, out_addr, gs);
                }
            }
            if (styles) XFree(styles);
        } else {
            failed = name_addr;
            break;
        }
        if (bad) { failed = unit_copy_string(rt, bad); break; }
    }
    cpu->gpr[2] = failed;
}

static void bridge_unit_XVaCreateNestedList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned ai = 1u;
    (void)rt;
    while (ai < 32u) {
        uint32_t name = unit_arg32(rt, cpu, ai++);
        if (!name) break;
        (void)unit_arg32(rt, cpu, ai++);
    }
    cpu->gpr[2] = nested_alloc();
}

static void bridge_unit_XCreateIC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIM im = xim_from_guest((uint32_t)cpu->gpr[4]);
    XIMStyle style = 0;
    Window client = 0, focus = 0;
    const char *resource_name = 0, *resource_class = 0;
    unsigned ai = 1u;
    XIC ic = 0;

    if (!im) { cpu->gpr[2] = 0; return; }
    while (ai < 32u) {
        IrixGuestAddr name_addr = unit_arg32(rt, cpu, ai++);
        uint32_t value;
        const char *name;
        if (!name_addr) break;
        value = unit_arg32(rt, cpu, ai++);
        name = guest_cstr(rt, name_addr);
        if (!name) break;
        if (!strcmp(name, XNInputStyle)) style = (XIMStyle)value;
        else if (!strcmp(name, XNClientWindow)) client = (Window)value;
        else if (!strcmp(name, XNFocusWindow)) focus = (Window)value;
        else if (!strcmp(name, XNResourceName)) resource_name = guest_cstr(rt, value);
        else if (!strcmp(name, XNResourceClass)) resource_class = guest_cstr(rt, value);
        else if (!strcmp(name, XNPreeditAttributes) || !strcmp(name, XNStatusAttributes)) {
            (void)nested_is_guest(value);
        }
    }

    if (style && client && focus)
        ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, client,
                       XNFocusWindow, focus, NULL);
    else if (style && client)
        ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, client, NULL);
    else if (style)
        ic = XCreateIC(im, XNInputStyle, style, NULL);
    else if (client)
        ic = XCreateIC(im, XNClientWindow, client, NULL);

    if (ic && resource_name)
        (void)XSetICValues(ic, XNResourceName, resource_name, NULL);
    if (ic && resource_class)
        (void)XSetICValues(ic, XNResourceClass, resource_class, NULL);
    cpu->gpr[2] = xic_to_guest(ic);
}

static void bridge_unit_XDestroyIC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr a = (uint32_t)cpu->gpr[4];
    XIC ic = xic_from_guest(a);
    uint32_t i;
    (void)rt;
    if (ic) XDestroyIC(ic);
    if (a >= IRIX_XIC_BASE) {
        i = (a - IRIX_XIC_BASE) / 4u;
        if (i < g_xim.ic_count) g_xim.ics[i] = 0;
    }
    cpu->gpr[2] = 0;
}

static void bridge_unit_XSetICFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIC ic = xic_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    if (ic) XSetICFocus(ic);
    cpu->gpr[2] = 0;
}

static void bridge_unit_XUnsetICFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIC ic = xic_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    if (ic) XUnsetICFocus(ic);
    cpu->gpr[2] = 0;
}

static void bridge_unit_XSetICValues(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIC ic = xic_from_guest((uint32_t)cpu->gpr[4]);
    unsigned ai = 1u;
    IrixGuestAddr failed = 0;
    if (!ic) { cpu->gpr[2] = 0; return; }
    while (ai < 32u) {
        IrixGuestAddr name_addr = unit_arg32(rt, cpu, ai++);
        uint32_t value;
        const char *name;
        char *bad = 0;
        if (!name_addr) break;
        value = unit_arg32(rt, cpu, ai++);
        name = guest_cstr(rt, name_addr);
        if (!name) { failed = name_addr; break; }
        if (!strcmp(name, XNFocusWindow)) bad = XSetICValues(ic, XNFocusWindow, (Window)value, NULL);
        else if (!strcmp(name, XNClientWindow)) bad = XSetICValues(ic, XNClientWindow, (Window)value, NULL);
        else if (!strcmp(name, XNResourceName)) bad = XSetICValues(ic, XNResourceName, guest_cstr(rt, value), NULL);
        else if (!strcmp(name, XNResourceClass)) bad = XSetICValues(ic, XNResourceClass, guest_cstr(rt, value), NULL);
        else if (!strcmp(name, XNPreeditAttributes) || !strcmp(name, XNStatusAttributes)) {
            if (!nested_is_guest(value)) { failed = name_addr; break; }
        } else { failed = name_addr; break; }
        if (bad) { failed = unit_copy_string(rt, bad); break; }
    }
    cpu->gpr[2] = failed;
}

static void bridge_unit_XGetICValues(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIC ic = xic_from_guest((uint32_t)cpu->gpr[4]);
    unsigned ai = 1u;
    IrixGuestAddr failed = 0;
    if (!ic) { cpu->gpr[2] = 0; return; }
    while (ai < 32u) {
        IrixGuestAddr name_addr = unit_arg32(rt, cpu, ai++);
        IrixGuestAddr out_addr;
        const char *name;
        char *bad = 0;
        if (!name_addr) break;
        out_addr = unit_arg32(rt, cpu, ai++);
        name = guest_cstr(rt, name_addr);
        if (!name) { failed = name_addr; break; }
        if (!strcmp(name, XNFilterEvents)) {
            long v = 0; bad = XGetICValues(ic, XNFilterEvents, &v, NULL); if (!bad && out_addr) write32(rt, out_addr, (uint32_t)v);
        } else if (!strcmp(name, XNInputStyle)) {
            XIMStyle v = 0; bad = XGetICValues(ic, XNInputStyle, &v, NULL); if (!bad && out_addr) write32(rt, out_addr, (uint32_t)v);
        } else if (!strcmp(name, XNClientWindow)) {
            Window v = 0; bad = XGetICValues(ic, XNClientWindow, &v, NULL); if (!bad && out_addr) write32(rt, out_addr, (uint32_t)v);
        } else if (!strcmp(name, XNFocusWindow)) {
            Window v = 0; bad = XGetICValues(ic, XNFocusWindow, &v, NULL); if (!bad && out_addr) write32(rt, out_addr, (uint32_t)v);
        } else { failed = name_addr; break; }
        if (bad) { failed = unit_copy_string(rt, bad); break; }
    }
    cpu->gpr[2] = failed;
}

static int unit_read_key_event(IrixGuestRuntime *rt, IrixGuestAddr g, XKeyEvent *e)
{
    IrixGuestAddr gd;
    Display *d;
    if (!g || !e || !irix_guest_ptr(rt, g, 60u)) return 0;
    gd = read32(rt, g + 12u);
    d = irix_x11_primary_display_from_guest(gd);
    if (!d) return 0;
    memset(e, 0, sizeof(*e));
    e->type = (int32_t)read32(rt, g + 0u);
    e->serial = (unsigned long)read32(rt, g + 4u);
    e->send_event = (Bool)(int32_t)read32(rt, g + 8u);
    e->display = d;
    e->window = (Window)read32(rt, g + 16u);
    e->root = (Window)read32(rt, g + 20u);
    e->subwindow = (Window)read32(rt, g + 24u);
    e->time = (Time)read32(rt, g + 28u);
    e->x = (int32_t)read32(rt, g + 32u);
    e->y = (int32_t)read32(rt, g + 36u);
    e->x_root = (int32_t)read32(rt, g + 40u);
    e->y_root = (int32_t)read32(rt, g + 44u);
    e->state = read32(rt, g + 48u);
    e->keycode = read32(rt, g + 52u);
    e->same_screen = (Bool)(int32_t)read32(rt, g + 56u);
    return !rt->trapped;
}

static void bridge_unit_XmbLookupString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XIC ic = xic_from_guest((uint32_t)cpu->gpr[4]);
    XKeyEvent ev;
    char *buf = (char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[6], (size_t)(uint32_t)cpu->gpr[7]);
    KeySym ks = 0;
    Status st = 0;
    int n = 0;
    if (ic && buf && unit_read_key_event(rt, (uint32_t)cpu->gpr[5], &ev))
        n = XmbLookupString(ic, &ev, buf, (int)cpu->gpr[7], &ks, &st);
    if ((uint32_t)cpu->gpr[8]) write32(rt, (uint32_t)cpu->gpr[8], (uint32_t)ks);
    if ((uint32_t)cpu->gpr[9]) write32(rt, (uint32_t)cpu->gpr[9], (uint32_t)st);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

void irix_x11_bridge_init(IrixGuestRuntime *rt,
                          const char *const *fast_quark_names,
                          size_t fast_quark_count)
{
    memset(&g_xim, 0, sizeof(g_xim));
    irix_x11_bridge_init_base(rt, fast_quark_names, fast_quark_count);
}

IrixGuestFunction irix_x11_bridge_lookup(const char *name)
{
    IrixGuestFunction fn = irix_x11_bridge_lookup_base(name);
    if (fn || !name) return fn;
#define MAP(n) if (!strcmp(name, #n)) return bridge_unit_##n
    MAP(XPolygonRegion); MAP(XSetRegion); MAP(XGrabPointer);
    MAP(XOpenIM); MAP(XCloseIM); MAP(XGetIMValues); MAP(XCreateIC); MAP(XDestroyIC);
    MAP(XSetICFocus); MAP(XUnsetICFocus); MAP(XSetICValues); MAP(XGetICValues);
    MAP(XVaCreateNestedList); MAP(XmbLookupString);
#undef MAP
    return 0;
}
