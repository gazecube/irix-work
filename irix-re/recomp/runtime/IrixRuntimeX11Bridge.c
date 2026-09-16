#include "IrixRuntimeX11Bridge.h"

#include <X11/Xlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_RUNTIME_X11_TMP_BASE 0x74400000u
#define IRIX_RUNTIME_X11_TMP_SIZE 0x00100000u
#define IRIX_RUNTIME_X11_MAX_DISPLAYS 16u
#define IRIX_SGI_FAST_ATOM_FIRST 69u
#define IRIX_SGI_FAST_ATOM_LAST 200u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr tmp_cur;
    IrixGuestAddr guest_display[IRIX_RUNTIME_X11_MAX_DISPLAYS];
    Display *host_display[IRIX_RUNTIME_X11_MAX_DISPLAYS];
    size_t display_count;
} IrixRuntimeX11State;

static IrixRuntimeX11State g_x11;

/* SGI's X11/SGIFastAtom.h defines extra predefined atoms 69..200 and
   _XSGIFastInternAtom(Display *, int fast_name, Bool onlyIfExists).
   Non-SGI X servers do not reserve those numeric IDs, so translate the SGI
   fast-name ID back to the canonical atom string and intern it on the host
   server.  Atom IDs are server-global, so the same-server fallback Display is
   sufficient here even though connection-local APIs must use the real bridge
   Display. */
static const char *const g_sgi_fast_atom_names[] = {
    "SGI_EXTRA_PREDEFINES", "AVERAGE_WIDTH", "BACKGROUND", "CLIPBOARD",
    "CLIP_TEMPORARY", "COMPOUND_TEXT", "EXTENDED_TARGETS", "FOREGROUND",
    "INCR", "LENGTH", "MOTIF_DESTINATION", "MULTIPLE", "NATIVE_TARGETS",
    "NULL", "PIXEL", "PIXEL_SIZE", "RESOLUTION_Y", "SCREEN_RESOURCES",
    "SERVER_OVERLAY_VISUALS", "SM_CLIENT_ID", "TARGETS", "TEXT", "TIMESTAMP",
    "WM_CHANGE_STATE", "WM_CLIENT_LEADER", "WM_COLORMAP_WINDOWS",
    "WM_DELETE_WINDOW", "WM_LOCALE_NAME", "WM_PROTOCOLS", "WM_STATE",
    "WM_WINDOW_ROLE", "XDCCC_CORRECT_ATOM_NAME", "XDCCC_MATRIX_ATOM_NAME",
    "_MOTIF_BINDINGS", "_MOTIF_CANCEL_DROP_EFFECT", "_MOTIF_CLIP_DATA_DELETE",
    "_MOTIF_CLIP_DATA_REQUEST", "_MOTIF_CLIP_HEADER", "_MOTIF_CLIP_LOCK",
    "_MOTIF_CLIP_LOCK_ACCESS_VALID", "_MOTIF_CLIP_MESSAGE", "_MOTIF_CLIP_TIME",
    "_MOTIF_DEFAULT_BINDINGS", "_MOTIF_DRAG_ATOMS", "_MOTIF_DRAG_ATOM_PAIRS",
    "_MOTIF_DRAG_INITIATOR_INFO", "_MOTIF_DRAG_PROXY_WINDOW",
    "_MOTIF_DRAG_RECEIVER_INFO", "_MOTIF_DRAG_TARGETS", "_MOTIF_DRAG_WINDOW",
    "_MOTIF_DROP", "_MOTIF_PROXY_WINDOW", "_MOTIF_WINDOW", "_MOTIF_WM_HINTS",
    "_MOTIF_WM_INFO", "_MOTIF_WM_MESSAGES", "_MOTIF_WM_OFFSET", "_WM_QUIT_APP",
    "_XM_TEXT_I_S_PROP", "_XT_SELECTION_0", "SGI_HELP_REQUEST", "SGI_HELP_SERVER",
    "_SGI_GAMMA_FLAG", "_SGI_ICON", "_SGI_ICONS_VISUALS", "_SGI__MENU_WINDOW",
    "Compose", "close", "play", "install", "level", "mute", "_SGI_SoundScheme",
    "DELETE", "INSERT_SELECTION", "ATOM_PAIR", "Editres", "EditresCommand",
    "EditresClientVal", "EditresProtocol", "_MOTIF_WM_MENU", "CHARACTER_POSITION",
    "CLASS", "CLIENT_WINDOW", "CLIPBOARD_MANAGER", "COLUMN_NUMBER", "DONE",
    "FILE", "FILE_NAME", "HOST_NAME", "INSERT_PROPERTY", "LINE_NUMBER",
    "LINK_SELECTION", "LIST_LENGTH", "MODULE", "NAME", "None", "ODIF",
    "OWNER_OS", "PDM_EXIT_CANCEL", "PDM_EXIT_ERROR", "PDM_EXIT_OK", "PDM_REPLY",
    "PDM_START", "PDM_START_ERROR", "PDM_START_OK", "PDM_START_PXAUTH",
    "PDM_START_VXAUTH", "PROCEDURE", "PROCESS", "Pixel_Sets", "SDT_Pixel_Set",
    "SPAN", "TASK", "TRANSFER_FAILURE", "TRANSFER_SUCCESS", "USER",
    "_MOTIF_ATOM_0", "_MOTIF_CLIPBOARD_TARGETS", "_MOTIF_COMPOUND_STRING",
    "_MOTIF_CURRENT_TIME", "_MOTIF_DEFERRED_CLIPBOARD_TARGETS",
    "_MOTIF_DESTINATION", "_MOTIF_DRAG_OFFSET", "_MOTIF_ENCODING_REGISTRY",
    "_MOTIF_EXPORT_TARGETS", "_MOTIF_LOSE_SELECTION", "_MOTIF_RENDER_TABLE",
    "_MOTIF_SELECTION_TEXT", "_MOTIF_SNAPSHOT", "_MOTIF_WM_ALL_CLIENTS",
    "_MOTIF_WM_QUERY"
};

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

/* IRIX N32 XColor is 12 bytes: 32-bit pixel; three 16-bit channels;
   flags and pad bytes.  Never memcpy the host LP64 XColor across the ABI. */
static int guest_xcolor_read(IrixGuestRuntime *rt, IrixGuestAddr addr, XColor *c)
{
    if (!addr || !c || !irix_guest_ptr(rt, addr, 12u)) return 0;
    memset(c, 0, sizeof(*c));
    c->pixel = (unsigned long)irix_guest_read32be(rt, addr + 0u);
    c->red = irix_guest_read16be(rt, addr + 4u);
    c->green = irix_guest_read16be(rt, addr + 6u);
    c->blue = irix_guest_read16be(rt, addr + 8u);
    c->flags = (char)irix_guest_read8(rt, addr + 10u);
    c->pad = (char)irix_guest_read8(rt, addr + 11u);
    return !rt->trapped;
}

static int guest_xcolor_write(IrixGuestRuntime *rt, IrixGuestAddr addr, const XColor *c)
{
    if (!addr || !c || !irix_guest_ptr(rt, addr, 12u)) return 0;
    irix_guest_write32be(rt, addr + 0u, (uint32_t)c->pixel);
    irix_guest_write16be(rt, addr + 4u, c->red);
    irix_guest_write16be(rt, addr + 6u, c->green);
    irix_guest_write16be(rt, addr + 8u, c->blue);
    irix_guest_write8(rt, addr + 10u, (uint8_t)c->flags);
    irix_guest_write8(rt, addr + 11u, (uint8_t)c->pad);
    return !rt->trapped;
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

static void bridge__XSGIFastInternAtom(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    uint32_t fast_name = (uint32_t)cpu->gpr[5];
    Bool only_if_exists = (Bool)cpu->gpr[6];
    const char *name;
    Atom atom;

    if (!d || fast_name < IRIX_SGI_FAST_ATOM_FIRST ||
        fast_name > IRIX_SGI_FAST_ATOM_LAST) {
        cpu->gpr[2] = 0;
        return;
    }
    name = g_sgi_fast_atom_names[fast_name - IRIX_SGI_FAST_ATOM_FIRST];
    atom = XInternAtom(d, name, only_if_exists);
    cpu->gpr[2] = (uint32_t)atom;
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

static void bridge_XAllocNamedColor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Colormap cmap = (Colormap)(uint32_t)cpu->gpr[5];
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    IrixGuestAddr screen_out = (uint32_t)cpu->gpr[7];
    IrixGuestAddr exact_out = (uint32_t)cpu->gpr[8];
    XColor screen_def, exact_def;
    int rc = 0;
    if (d && name && screen_out && exact_out) {
        rc = XAllocNamedColor(d, cmap, name, &screen_def, &exact_def);
        if (rc) {
            guest_xcolor_write(rt, screen_out, &screen_def);
            guest_xcolor_write(rt, exact_out, &exact_def);
        }
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XAllocColor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Colormap cmap = (Colormap)(uint32_t)cpu->gpr[5];
    IrixGuestAddr color_addr = (uint32_t)cpu->gpr[6];
    XColor color;
    int rc = 0;
    if (d && guest_xcolor_read(rt, color_addr, &color)) {
        rc = XAllocColor(d, cmap, &color);
        if (rc) guest_xcolor_write(rt, color_addr, &color);
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XParseColor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Colormap cmap = (Colormap)(uint32_t)cpu->gpr[5];
    const char *spec = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    XColor color;
    int rc = 0;
    if (d && spec && out) {
        memset(&color, 0, sizeof(color));
        rc = XParseColor(d, cmap, spec, &color);
        if (rc) guest_xcolor_write(rt, out, &color);
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XQueryColors(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Colormap cmap = (Colormap)(uint32_t)cpu->gpr[5];
    IrixGuestAddr guest_colors = (uint32_t)cpu->gpr[6];
    int count = (int)(int32_t)cpu->gpr[7];
    XColor *colors = 0;
    int i, rc = 0;
    if (!d || count < 0 || (count && !guest_colors)) { cpu->gpr[2] = 0; return; }
    if (!count) { cpu->gpr[2] = 1; return; }
    colors = (XColor *)calloc((size_t)count, sizeof(*colors));
    if (!colors) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < count; ++i) {
        IrixGuestAddr a = guest_colors + (IrixGuestAddr)i * 12u;
        if (!guest_xcolor_read(rt, a, &colors[i])) goto out;
    }
    rc = XQueryColors(d, cmap, colors, count);
    if (rc)
        for (i = 0; i < count; ++i)
            guest_xcolor_write(rt, guest_colors + (IrixGuestAddr)i * 12u, &colors[i]);
out:
    free(colors);
    cpu->gpr[2] = (uint32_t)rc;
}

static void bridge_XFreeColors(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    Colormap cmap = (Colormap)(uint32_t)cpu->gpr[5];
    IrixGuestAddr guest_pixels = (uint32_t)cpu->gpr[6];
    int count = (int)(int32_t)cpu->gpr[7];
    unsigned long planes = (unsigned long)(uint32_t)cpu->gpr[8];
    unsigned long *pixels = 0;
    int i, rc = 0;
    if (!d || count < 0 || (count && !guest_pixels)) { cpu->gpr[2] = 0; return; }
    if (!count) { cpu->gpr[2] = 1; return; }
    pixels = (unsigned long *)calloc((size_t)count, sizeof(*pixels));
    if (!pixels) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < count; ++i)
        pixels[i] = (unsigned long)irix_guest_read32be(rt, guest_pixels + (IrixGuestAddr)i * 4u);
    if (!rt->trapped) rc = XFreeColors(d, cmap, pixels, count, planes);
    free(pixels);
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
    MAP(_XSGIFastInternAtom); MAP(XInternAtoms); MAP(XRestackWindows);
    MAP(XAllocNamedColor); MAP(XAllocColor); MAP(XParseColor); MAP(XQueryColors); MAP(XFreeColors);
    MAP(XInstallColormap); MAP(XFreeColormap); MAP(XListInstalledColormaps); MAP(XSetWMColormapWindows);
#undef MAP
    return 0;
}
