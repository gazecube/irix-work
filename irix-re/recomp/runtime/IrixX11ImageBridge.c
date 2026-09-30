#include "IrixGuest.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_IMAGE_TMP_BASE 0x72b00000u
#define IRIX_X11_IMAGE_TMP_SIZE 0x00100000u
#define IRIX_X11_IMAGE_MAX_DISPLAYS 16u
#define IRIX_X11_IMAGE_MAX_IMAGES 256u
#define IRIX_N32_XIMAGE_SIZE 88u

#define IRIX_XIMAGE_CB_CREATE  0x72c00000u
#define IRIX_XIMAGE_CB_DESTROY 0x72c00008u
#define IRIX_XIMAGE_CB_GET     0x72c00010u
#define IRIX_XIMAGE_CB_PUT     0x72c00018u
#define IRIX_XIMAGE_CB_SUB     0x72c00020u
#define IRIX_XIMAGE_CB_ADD     0x72c00028u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr tmp_cur;
    IrixGuestAddr guest_display[IRIX_X11_IMAGE_MAX_DISPLAYS];
    Display *host_display[IRIX_X11_IMAGE_MAX_DISPLAYS];
    size_t display_count;
    IrixGuestAddr guest_image[IRIX_X11_IMAGE_MAX_IMAGES];
    XImage *host_image[IRIX_X11_IMAGE_MAX_IMAGES];
    size_t image_count;
    int callbacks_registered;
} IrixX11ImageState;

static IrixX11ImageState g_image;

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || !g_image.tmp_cur || g_image.tmp_cur + n < g_image.tmp_cur ||
        g_image.tmp_cur + n > IRIX_X11_IMAGE_TMP_BASE + IRIX_X11_IMAGE_TMP_SIZE)
        return 0;
    a = g_image.tmp_cur;
    g_image.tmp_cur += n;
    return a;
}

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
    for (i = 0; i < g_image.display_count; ++i)
        if (g_image.guest_display[i] == guest)
            return g_image.host_display[i];
    if (g_image.display_count >= IRIX_X11_IMAGE_MAX_DISPLAYS) return 0;
    name_addr = irix_guest_read32be(rt, guest + 128u);
    if (!rt->trapped) name = guest_cstr(rt, name_addr);
    if (rt->trapped) return 0;
    i = g_image.display_count++;
    g_image.guest_display[i] = guest;
    g_image.host_display[i] = XOpenDisplay(name);
    return g_image.host_display[i];
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
    if (infos && count > 0) visual = infos[0].visual;
    if (infos) XFree(infos);
    return visual;
}

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
}

static int image_index(IrixGuestAddr guest)
{
    size_t i;
    for (i = 0; i < g_image.image_count; ++i)
        if (g_image.guest_image[i] == guest && g_image.host_image[i])
            return (int)i;
    return -1;
}

static uint64_t image_data_size(const XImage *im)
{
    uint64_t n;
    if (!im || im->bytes_per_line <= 0 || im->height <= 0) return 0;
    n = (uint64_t)(unsigned)im->bytes_per_line * (uint64_t)(unsigned)im->height;
    if (im->format != ZPixmap && im->depth > 1)
        n *= (uint64_t)(unsigned)im->depth;
    return n;
}

static void write_image_shadow(IrixGuestRuntime *rt, IrixGuestAddr guest,
                               const XImage *im, IrixGuestAddr guest_data)
{
    if (!guest || !im || !irix_guest_ptr(rt, guest, IRIX_N32_XIMAGE_SIZE)) return;
    irix_guest_write32be(rt, guest + 0u, (uint32_t)im->width);
    irix_guest_write32be(rt, guest + 4u, (uint32_t)im->height);
    irix_guest_write32be(rt, guest + 8u, (uint32_t)im->xoffset);
    irix_guest_write32be(rt, guest + 12u, (uint32_t)im->format);
    irix_guest_write32be(rt, guest + 16u, guest_data);
    irix_guest_write32be(rt, guest + 20u, (uint32_t)im->byte_order);
    irix_guest_write32be(rt, guest + 24u, (uint32_t)im->bitmap_unit);
    irix_guest_write32be(rt, guest + 28u, (uint32_t)im->bitmap_bit_order);
    irix_guest_write32be(rt, guest + 32u, (uint32_t)im->bitmap_pad);
    irix_guest_write32be(rt, guest + 36u, (uint32_t)im->depth);
    irix_guest_write32be(rt, guest + 40u, (uint32_t)im->bytes_per_line);
    irix_guest_write32be(rt, guest + 44u, (uint32_t)im->bits_per_pixel);
    irix_guest_write32be(rt, guest + 48u, (uint32_t)im->red_mask);
    irix_guest_write32be(rt, guest + 52u, (uint32_t)im->green_mask);
    irix_guest_write32be(rt, guest + 56u, (uint32_t)im->blue_mask);
    irix_guest_write32be(rt, guest + 60u, 0u);
    irix_guest_write32be(rt, guest + 64u, IRIX_XIMAGE_CB_CREATE);
    irix_guest_write32be(rt, guest + 68u, IRIX_XIMAGE_CB_DESTROY);
    irix_guest_write32be(rt, guest + 72u, IRIX_XIMAGE_CB_GET);
    irix_guest_write32be(rt, guest + 76u, IRIX_XIMAGE_CB_PUT);
    irix_guest_write32be(rt, guest + 80u, IRIX_XIMAGE_CB_SUB);
    irix_guest_write32be(rt, guest + 84u, IRIX_XIMAGE_CB_ADD);
}

static XImage *sync_image_from_guest(IrixGuestRuntime *rt, IrixGuestAddr guest)
{
    int idx = image_index(guest);
    XImage *im;
    IrixGuestAddr data;
    if (idx < 0 || !irix_guest_ptr(rt, guest, IRIX_N32_XIMAGE_SIZE)) return 0;
    im = g_image.host_image[idx];
    im->width = (int32_t)irix_guest_read32be(rt, guest + 0u);
    im->height = (int32_t)irix_guest_read32be(rt, guest + 4u);
    im->xoffset = (int32_t)irix_guest_read32be(rt, guest + 8u);
    im->format = (int32_t)irix_guest_read32be(rt, guest + 12u);
    data = irix_guest_read32be(rt, guest + 16u);
    im->byte_order = (int32_t)irix_guest_read32be(rt, guest + 20u);
    im->bitmap_unit = (int32_t)irix_guest_read32be(rt, guest + 24u);
    im->bitmap_bit_order = (int32_t)irix_guest_read32be(rt, guest + 28u);
    im->bitmap_pad = (int32_t)irix_guest_read32be(rt, guest + 32u);
    im->depth = (int32_t)irix_guest_read32be(rt, guest + 36u);
    im->bytes_per_line = (int32_t)irix_guest_read32be(rt, guest + 40u);
    im->bits_per_pixel = (int32_t)irix_guest_read32be(rt, guest + 44u);
    im->red_mask = (unsigned long)irix_guest_read32be(rt, guest + 48u);
    im->green_mask = (unsigned long)irix_guest_read32be(rt, guest + 52u);
    im->blue_mask = (unsigned long)irix_guest_read32be(rt, guest + 56u);
    if (rt->trapped) return 0;
    if (data) {
        uint64_t n = image_data_size(im);
        if (!n || n > SIZE_MAX || !irix_guest_ptr(rt, data, (size_t)n))
            return 0;
        im->data = (char *)irix_guest_ptr(rt, data, (size_t)n);
    } else {
        im->data = 0;
    }
    return rt->trapped ? 0 : im;
}

static IrixGuestAddr register_image(IrixGuestRuntime *rt, XImage *im, IrixGuestAddr guest_data)
{
    IrixGuestAddr guest;
    size_t i;
    if (!im) return 0;
    for (i = 0; i < IRIX_X11_IMAGE_MAX_IMAGES; ++i) {
        if (i >= g_image.image_count || !g_image.host_image[i]) {
            if (i >= g_image.image_count) g_image.image_count = i + 1u;
            guest = tmp_alloc(IRIX_N32_XIMAGE_SIZE);
            if (!guest) return 0;
            g_image.guest_image[i] = guest;
            g_image.host_image[i] = im;
            write_image_shadow(rt, guest, im, guest_data);
            return guest;
        }
    }
    return 0;
}

static void bridge_XCreateImage(IrixGuestRuntime *rt, IrixGuestCpu *cpu);

static void cb_destroy_image(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest = (uint32_t)cpu->gpr[4];
    int idx = image_index(guest);
    XImage *im = sync_image_from_guest(rt, guest);
    int rc = 0;
    if (im && idx >= 0) {
        im->data = 0;
        rc = XDestroyImage(im);
        g_image.host_image[idx] = 0;
        g_image.guest_image[idx] = 0;
    }
    cpu->gpr[2] = (uint32_t)rc;
}

static void cb_get_pixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XImage *im = sync_image_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = im ? (uint32_t)XGetPixel(im, (int32_t)cpu->gpr[5],
                                           (int32_t)cpu->gpr[6]) : 0;
}

static void cb_put_pixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XImage *im = sync_image_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = im ? (uint32_t)XPutPixel(im, (int32_t)cpu->gpr[5],
                                           (int32_t)cpu->gpr[6],
                                           (unsigned long)(uint32_t)cpu->gpr[7]) : 0;
}

static void cb_sub_image(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XImage *im = sync_image_from_guest(rt, (uint32_t)cpu->gpr[4]);
    XImage *sub = 0;
    IrixGuestAddr guest_data = 0;
    uint64_t n;
    if (im)
        sub = XSubImage(im, (int32_t)cpu->gpr[5], (int32_t)cpu->gpr[6],
                        (unsigned int)(uint32_t)cpu->gpr[7],
                        (unsigned int)(uint32_t)cpu->gpr[8]);
    if (!sub) { cpu->gpr[2] = 0; return; }
    n = image_data_size(sub);
    if (n && n <= UINT32_MAX) {
        guest_data = tmp_alloc((uint32_t)n);
        if (guest_data && sub->data) {
            void *dst = irix_guest_ptr(rt, guest_data, (size_t)n);
            if (dst) memcpy(dst, sub->data, (size_t)n);
        }
    }
    if (sub->data) XFree(sub->data);
    sub->data = guest_data ? (char *)irix_guest_ptr(rt, guest_data, 1u) : 0;
    cpu->gpr[2] = register_image(rt, sub, guest_data);
    if (!cpu->gpr[2]) {
        sub->data = 0;
        XDestroyImage(sub);
    }
}

static void cb_add_pixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XImage *im = sync_image_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = im ? (uint32_t)XAddPixel(im, (long)(int32_t)cpu->gpr[5]) : 0;
}

static void register_callbacks(IrixGuestRuntime *rt)
{
    if (g_image.callbacks_registered) return;
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_CREATE, bridge_XCreateImage,
                                 "XImage.create_image");
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_DESTROY, cb_destroy_image,
                                 "XImage.destroy_image");
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_GET, cb_get_pixel,
                                 "XImage.get_pixel");
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_PUT, cb_put_pixel,
                                 "XImage.put_pixel");
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_SUB, cb_sub_image,
                                 "XImage.sub_image");
    irix_guest_register_function(rt, IRIX_XIMAGE_CB_ADD, cb_add_pixel,
                                 "XImage.add_pixel");
    g_image.callbacks_registered = 1;
}

static void bridge_XCreateImage(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *dpy = fallback_display(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr guest_visual = (uint32_t)cpu->gpr[5];
    Visual *visual = visual_from_guest(rt, dpy, guest_visual);
    unsigned int depth = (uint32_t)cpu->gpr[6];
    int format = (int32_t)cpu->gpr[7];
    int offset = (int32_t)cpu->gpr[8];
    IrixGuestAddr guest_data = (uint32_t)cpu->gpr[9];
    unsigned int width = (uint32_t)cpu->gpr[10];
    unsigned int height = (uint32_t)cpu->gpr[11];
    int bitmap_pad = (int32_t)n32_stack_arg32(rt, cpu, 0u);
    int bytes_per_line = (int32_t)n32_stack_arg32(rt, cpu, 1u);
    char *data = 0;
    XImage *im;

    register_callbacks(rt);
    if (!dpy || !visual || rt->trapped) { cpu->gpr[2] = 0; return; }

    /*
     * Xlib keeps and later dereferences the supplied image data pointer.
     * Never hand it a host pointer obtained by validating only the first guest
     * byte.  Build the XImage first so Xlib computes bytes_per_line, then
     * require the complete backing range before exposing guest storage.
     */
    im = XCreateImage(dpy, visual, depth, format, offset, 0, width, height,
                      bitmap_pad, bytes_per_line);
    if (im && guest_data) {
        uint64_t n = image_data_size(im);
        if (!n || n > SIZE_MAX ||
            !(data = (char *)irix_guest_ptr(rt, guest_data, (size_t)n))) {
            im->data = 0;
            XDestroyImage(im);
            cpu->gpr[2] = 0;
            return;
        }
        im->data = data;
    }
    cpu->gpr[2] = register_image(rt, im, guest_data);
    if (im && !cpu->gpr[2]) {
        im->data = 0;
        XDestroyImage(im);
    }
}

void irix_x11_image_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_image, 0, sizeof(g_image));
    g_image.rt = rt;
    if (irix_guest_map(rt, IRIX_X11_IMAGE_TMP_BASE, IRIX_X11_IMAGE_TMP_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_X11_IMAGE_TMP_BASE, 1u))
        g_image.tmp_cur = IRIX_X11_IMAGE_TMP_BASE;
}

IrixGuestFunction irix_x11_image_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XCreateImage")) return bridge_XCreateImage;
    return 0;
}
