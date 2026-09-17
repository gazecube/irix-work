#include "IrixX11WindowBridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_WINDOW_MAX_DISPLAYS 16u
#define IRIX_N32_XSETWINDOWATTRIBUTES_SIZE 60u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr guest_display[IRIX_X11_WINDOW_MAX_DISPLAYS];
    Display *host_display[IRIX_X11_WINDOW_MAX_DISPLAYS];
    size_t display_count;
} IrixX11WindowState;

static IrixX11WindowState g_window;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
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

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
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
    /* This bridge currently owns a separate host Xlib connection from the
       other translated-X11 bridges.  XCreateWindow is normally buffered;
       flush it here so a subsequent bridge cannot send a request referencing
       this XID before the server has seen the CreateWindow request. */
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

void irix_x11_window_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_window, 0, sizeof(g_window));
    g_window.rt = rt;
}

IrixGuestFunction irix_x11_window_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XCreateWindow")) return bridge_XCreateWindow;
    if (!strcmp(name, "XChangeWindowAttributes")) return bridge_XChangeWindowAttributes;
    return 0;
}
