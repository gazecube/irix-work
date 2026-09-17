#define irix_x11_image_bridge_lookup irix_x11_image_bridge_lookup_base
#include "IrixX11ImageBridge.c"
#undef irix_x11_image_bridge_lookup

#include "IrixX11BridgeExtraAccess.h"

#define IRIX_N32_XEVENT_SIZE 96u

static int write_n32_event(IrixGuestRuntime *rt, IrixGuestAddr guest,
                           IrixGuestAddr guest_display, const XEvent *ev)
{
    void *p;
    if (!guest || !ev) return 0;
    p = irix_guest_ptr(rt, guest, IRIX_N32_XEVENT_SIZE);
    if (!p) return 0;
    memset(p, 0, IRIX_N32_XEVENT_SIZE);

    irix_guest_write32be(rt, guest + 0u, (uint32_t)ev->type);
    irix_guest_write32be(rt, guest + 4u, (uint32_t)ev->xany.serial);
    irix_guest_write32be(rt, guest + 8u, (uint32_t)ev->xany.send_event);
    irix_guest_write32be(rt, guest + 12u, guest_display);
    irix_guest_write32be(rt, guest + 16u, (uint32_t)ev->xany.window);

    switch (ev->type) {
    case Expose:
        irix_guest_write32be(rt, guest + 20u, (uint32_t)ev->xexpose.x);
        irix_guest_write32be(rt, guest + 24u, (uint32_t)ev->xexpose.y);
        irix_guest_write32be(rt, guest + 28u, (uint32_t)ev->xexpose.width);
        irix_guest_write32be(rt, guest + 32u, (uint32_t)ev->xexpose.height);
        irix_guest_write32be(rt, guest + 36u, (uint32_t)ev->xexpose.count);
        break;
    case VisibilityNotify:
        irix_guest_write32be(rt, guest + 20u, (uint32_t)ev->xvisibility.state);
        break;
    default:
        break;
    }
    return !rt->trapped;
}

static void bridge_XPutImage(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    Drawable drawable = (Drawable)(uint32_t)cpu->gpr[5];
    GC gc = irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);
    XImage *im = sync_image_from_guest(rt, (uint32_t)cpu->gpr[7]);
    int src_x = (int32_t)cpu->gpr[8];
    int src_y = (int32_t)cpu->gpr[9];
    int dst_x = (int32_t)cpu->gpr[10];
    int dst_y = (int32_t)cpu->gpr[11];
    unsigned int width = n32_stack_arg32(rt, cpu, 0u);
    unsigned int height = n32_stack_arg32(rt, cpu, 1u);

    if (!dpy || !gc || !im || rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }

    cpu->gpr[2] = (uint32_t)XPutImage(dpy, drawable, gc, im,
                                      src_x, src_y, dst_x, dst_y,
                                      width, height);
}

static void bridge_XCreateBitmapFromData(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    Drawable drawable = (Drawable)(uint32_t)cpu->gpr[5];
    IrixGuestAddr data_addr = (uint32_t)cpu->gpr[6];
    unsigned int width = (uint32_t)cpu->gpr[7];
    unsigned int height = (uint32_t)cpu->gpr[8];
    uint64_t stride = ((uint64_t)width + 7u) >> 3;
    uint64_t bytes = stride * (uint64_t)height;
    const char *data;

    if (!dpy || bytes > SIZE_MAX) {
        cpu->gpr[2] = 0;
        return;
    }

    data = (const char *)irix_guest_ptr(rt, data_addr, (size_t)bytes);
    if (!data || rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }

    cpu->gpr[2] = (uint32_t)XCreateBitmapFromData(dpy, drawable, data,
                                                  width, height);
}

static void bridge_XCheckMaskEvent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_display = (uint32_t)cpu->gpr[4];
    Display *dpy = irix_x11_primary_display_from_guest(guest_display);
    long mask = (long)(uint32_t)cpu->gpr[5];
    IrixGuestAddr guest_event = (uint32_t)cpu->gpr[6];
    XEvent ev;
    Bool ok = False;

    if (dpy && guest_event)
        ok = XCheckMaskEvent(dpy, mask, &ev);
    if (ok && !write_n32_event(rt, guest_event, guest_display, &ev))
        ok = False;
    cpu->gpr[2] = (uint32_t)ok;
}

IrixGuestFunction irix_x11_image_bridge_lookup(const char *name)
{
    if (name && !strcmp(name, "XPutImage")) return bridge_XPutImage;
    if (name && !strcmp(name, "XCreateBitmapFromData")) return bridge_XCreateBitmapFromData;
    if (name && !strcmp(name, "XCheckMaskEvent")) return bridge_XCheckMaskEvent;
    return irix_x11_image_bridge_lookup_base(name);
}
