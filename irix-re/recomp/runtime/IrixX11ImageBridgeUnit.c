#define irix_x11_image_bridge_lookup irix_x11_image_bridge_lookup_base
#include "IrixX11ImageBridge.c"
#undef irix_x11_image_bridge_lookup

#include "IrixX11BridgeExtraAccess.h"

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

IrixGuestFunction irix_x11_image_bridge_lookup(const char *name)
{
    if (name && !strcmp(name, "XPutImage")) return bridge_XPutImage;
    return irix_x11_image_bridge_lookup_base(name);
}
