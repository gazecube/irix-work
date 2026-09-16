#include "IrixRuntimeX11Bridge.h"

#include <X11/Xlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_RUNTIME_X11_TMP_BASE 0x74400000u
#define IRIX_RUNTIME_X11_TMP_SIZE 0x00100000u
#define IRIX_RUNTIME_X11_MAX_DISPLAYS 16u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr tmp_cur;
    IrixGuestAddr guest_display[IRIX_RUNTIME_X11_MAX_DISPLAYS];
    Display *host_display[IRIX_RUNTIME_X11_MAX_DISPLAYS];
    size_t display_count;
} IrixRuntimeX11State;

static IrixRuntimeX11State g_x11;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || !g_x11.tmp_cur || g_x11.tmp_cur + n < g_x11.tmp_cur ||
        g_x11.tmp_cur + n > IRIX_RUNTIME_X11_TMP_BASE + IRIX_RUNTIME_X11_TMP_SIZE)
        return 0;
    a = g_x11.tmp_cur;
    g_x11.tmp_cur += n;
    return a;
}

static Display *fallback_display(IrixGuestRuntime *rt, IrixGuestAddr guest)
{
    size_t i;
    IrixGuestAddr name_addr;
    const char *name = 0;
    if (!guest) return 0;
    for (i = 0; i < g_x11.display_count; ++i)
        if (g_x11.guest_display[i] == guest)
            return g_x11.host_display[i];
    if (g_x11.display_count >= IRIX_RUNTIME_X11_MAX_DISPLAYS) return 0;
    name_addr = irix_guest_read32be(rt, guest + 128u);
    if (!rt->trapped) name = guest_cstr(rt, name_addr);
    if (rt->trapped) return 0;
    i = g_x11.display_count++;
    g_x11.guest_display[i] = guest;
    g_x11.host_display[i] = XOpenDisplay(name);
    return g_x11.host_display[i];
}

static void bridge_XDisableAccessControl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XDisableAccessControl(d) : 0;
}

static void bridge_XEnableAccessControl(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XEnableAccessControl(d) : 0;
}

static void bridge_XGrabServer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XGrabServer(d) : 0;
}

static void bridge_XUngrabServer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XUngrabServer(d) : 0;
}

static void bridge_XMaxRequestSize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XMaxRequestSize(d) : 0;
}

static void bridge_XScreenNumberOfScreen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr gs = (uint32_t)cpu->gpr[4];
    IrixGuestAddr gd = gs ? irix_guest_read32be(rt, gs + 4u) : 0;
    IrixGuestAddr base = gd ? irix_guest_read32be(rt, gd + 140u) : 0;
    uint32_t count = gd ? irix_guest_read32be(rt, gd + 136u) : 0;
    uint32_t index = 0;
    if (!gs || !gd || !base || gs < base || ((gs - base) % 80u) != 0u) {
        cpu->gpr[2] = (uint64_t)(int64_t)-1;
        return;
    }
    index = (gs - base) / 80u;
    cpu->gpr[2] = index < count ? index : (uint64_t)(int64_t)-1;
}

static void bridge_XVisualIDFromVisual(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr gv = (uint32_t)cpu->gpr[4];
    cpu->gpr[2] = gv ? irix_guest_read32be(rt, gv + 4u) : 0;
}

static void bridge_XSetErrorHandler(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu->gpr[4];
    /* Never pass a translated MIPS callback to host Xlib.  The current
       desktop uses this to install its own diagnostic handler; host Xlib
       retains its default handler until callback trampolining is available. */
    cpu->gpr[2] = 0;
}

static void bridge_XSetIOErrorHandler(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu->gpr[4];
    cpu->gpr[2] = 0;
}

static void bridge_XFree(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* Bridge-returned Xlib arrays/strings live in guest arenas, not host Xlib
       allocations, so XFree is intentionally a guest-side no-op. */
    (void)cpu->gpr[4];
    cpu->gpr[2] = 1;
}

static void bridge_XFreeStringList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu->gpr[4];
    cpu->gpr[2] = 0;
}

static void bridge_XInternAtoms(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr names_addr = (uint32_t)cpu->gpr[5];
    int count = (int)(int32_t)cpu->gpr[6];
    Bool only_if_exists = (Bool)cpu->gpr[7];
    IrixGuestAddr atoms_addr = (uint32_t)cpu->gpr[8];
    char **names = 0;
    Atom *atoms = 0;
    int i, rc = 0;

    if (!d || count < 0 || !atoms_addr) { cpu->gpr[2] = 0; return; }
    if (count == 0) { cpu->gpr[2] = 1; return; }
    names = (char **)calloc((size_t)count, sizeof(*names));
    atoms = (Atom *)calloc((size_t)count, sizeof(*atoms));
    if (!names || !atoms) goto out;
    for (i = 0; i < count; ++i) {
        IrixGuestAddr s = irix_guest_read32be(rt, names_addr + (IrixGuestAddr)i * 4u);
        names[i] = (char *)guest_cstr(rt, s);
        if (!names[i]) goto out;
    }
    rc = XInternAtoms(d, names, count, only_if_exists, atoms);
    if (rc) {
        for (i = 0; i < count; ++i)
            irix_guest_write32be(rt, atoms_addr + (IrixGuestAddr)i * 4u, (uint32_t)atoms[i]);
    }
out:
    free(names);
    free(atoms);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XRestackWindows(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr list_addr = (uint32_t)cpu->gpr[5];
    int count = (int)(int32_t)cpu->gpr[6];
    Window *list = 0;
    int i, rc = 0;
    if (!d || count < 0) { cpu->gpr[2] = 0; return; }
    if (count == 0) { cpu->gpr[2] = 1; return; }
    list = (Window *)calloc((size_t)count, sizeof(*list));
    if (!list) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < count; ++i)
        list[i] = (Window)irix_guest_read32be(rt, list_addr + (IrixGuestAddr)i * 4u);
    rc = XRestackWindows(d, list, count);
    free(list);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XInstallColormap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XInstallColormap(d, (Colormap)(uint32_t)cpu->gpr[5]) : 0;
}

static void bridge_XFreeColormap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XFreeColormap(d, (Colormap)(uint32_t)cpu->gpr[5]) : 0;
}

static void bridge_XListInstalledColormaps(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    IrixGuestAddr nout = (uint32_t)cpu->gpr[6];
    int n = 0, i;
    Colormap *host = d ? XListInstalledColormaps(d, w, &n) : 0;
    IrixGuestAddr guest = 0;
    if (host && n > 0) {
        guest = tmp_alloc((uint32_t)n * 4u);
        if (guest)
            for (i = 0; i < n; ++i)
                irix_guest_write32be(rt, guest + (IrixGuestAddr)i * 4u, (uint32_t)host[i]);
    }
    if (host) XFree(host);
    if (nout) irix_guest_write32be(rt, nout, (uint32_t)n);
    cpu->gpr[2] = guest;
}

static void bridge_XSetWMColormapWindows(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Window w = (Window)(uint32_t)cpu->gpr[5];
    IrixGuestAddr list_addr = (uint32_t)cpu->gpr[6];
    int count = (int)(int32_t)cpu->gpr[7];
    Window *list = 0;
    int i, rc = 0;
    if (!d || count < 0) { cpu->gpr[2] = 0; return; }
    if (count > 0) {
        list = (Window *)calloc((size_t)count, sizeof(*list));
        if (!list) { cpu->gpr[2] = 0; return; }
        for (i = 0; i < count; ++i)
            list[i] = (Window)irix_guest_read32be(rt, list_addr + (IrixGuestAddr)i * 4u);
    }
    rc = XSetWMColormapWindows(d, w, list, count);
    free(list);
    cpu->gpr[2] = (uint32_t)rc;
}

void irix_runtime_x11_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_x11, 0, sizeof(g_x11));
    g_x11.rt = rt;
    if (irix_guest_map(rt, IRIX_RUNTIME_X11_TMP_BASE, IRIX_RUNTIME_X11_TMP_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_RUNTIME_X11_TMP_BASE, 1))
        g_x11.tmp_cur = IRIX_RUNTIME_X11_TMP_BASE;
}

IrixGuestFunction irix_runtime_x11_bridge_lookup(const char *name)
{
    if (!name) return 0;
#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(XDisableAccessControl); MAP(XEnableAccessControl);
    MAP(XGrabServer); MAP(XUngrabServer); MAP(XMaxRequestSize);
    MAP(XScreenNumberOfScreen); MAP(XVisualIDFromVisual);
    MAP(XSetErrorHandler); MAP(XSetIOErrorHandler); MAP(XFree); MAP(XFreeStringList);
    MAP(XInternAtoms); MAP(XRestackWindows);
    MAP(XInstallColormap); MAP(XFreeColormap); MAP(XListInstalledColormaps); MAP(XSetWMColormapWindows);
#undef MAP
    return 0;
}
