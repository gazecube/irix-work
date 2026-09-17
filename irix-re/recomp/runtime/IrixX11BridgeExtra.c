#include "IrixGuest.h"

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_MAX_DISPLAYS 16u
#define IRIX_X11_DISPLAY_BASE 0x72400000u
#define IRIX_X11_DISPLAY_STRIDE 0x00001000u
#define IRIX_X11_SCREEN_BASE 0x72440000u
#define IRIX_X11_SCREEN_STRIDE 0x00001000u
#define IRIX_X11_VISUAL_BASE 0x72480000u
#define IRIX_X11_VISUAL_STRIDE 0x00001000u
#define IRIX_X11_DISPLAY_SHADOW_SIZE 0x00100000u
#define IRIX_X11_MAX_GCS 1024u
#define IRIX_X11_GC_BASE 0x72500000u
#define IRIX_X11_TMP_BASE 0x72600000u
#define IRIX_X11_TMP_SIZE 0x00100000u

typedef struct {
    IrixGuestRuntime *rt;
    Display *displays[IRIX_X11_MAX_DISPLAYS];
    size_t display_count;
    GC gcs[IRIX_X11_MAX_GCS];
    size_t gc_count;
    IrixGuestAddr tmp_cur;
} IrixX11ExtraState;

static IrixX11ExtraState g_x11;

static IrixGuestAddr gc_to_guest(GC gc);

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || g_x11.tmp_cur + n < g_x11.tmp_cur ||
        g_x11.tmp_cur + n > IRIX_X11_TMP_BASE + IRIX_X11_TMP_SIZE)
        return 0;
    a = g_x11.tmp_cur;
    g_x11.tmp_cur += n;
    return a;
}

static IrixGuestAddr copy_host_string(const char *s)
{
    IrixGuestAddr a;
    size_t n;
    void *p;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    a = tmp_alloc((uint32_t)n);
    if (!a) return 0;
    p = irix_guest_ptr(g_x11.rt, a, n);
    if (!p) return 0;
    memcpy(p, s, n);
    return a;
}

static IrixGuestAddr display_guest_addr(size_t i)
{
    return IRIX_X11_DISPLAY_BASE + (IrixGuestAddr)(i * IRIX_X11_DISPLAY_STRIDE);
}

static Display *display_from_guest(IrixGuestAddr addr)
{
    uint32_t off, i;
    if (addr < IRIX_X11_DISPLAY_BASE || addr >= IRIX_X11_SCREEN_BASE) return 0;
    off = addr - IRIX_X11_DISPLAY_BASE;
    if (off % IRIX_X11_DISPLAY_STRIDE) return 0;
    i = off / IRIX_X11_DISPLAY_STRIDE;
    return i < g_x11.display_count ? g_x11.displays[i] : 0;
}

static void write_visual_shadow(IrixGuestRuntime *rt, IrixGuestAddr a, Visual *v)
{
    if (!a || !v) return;
    wr32(rt, a + 0, 0);
    wr32(rt, a + 4, (uint32_t)v->visualid);
    wr32(rt, a + 8, (uint32_t)v->class);
    wr32(rt, a + 12, (uint32_t)v->red_mask);
    wr32(rt, a + 16, (uint32_t)v->green_mask);
    wr32(rt, a + 20, (uint32_t)v->blue_mask);
    wr32(rt, a + 24, (uint32_t)v->bits_per_rgb);
    wr32(rt, a + 28, (uint32_t)v->map_entries);
}

static void write_display_shadow(size_t i, Display *dpy)
{
    IrixGuestRuntime *rt = g_x11.rt;
    IrixGuestAddr d = display_guest_addr(i);
    IrixGuestAddr screens = IRIX_X11_SCREEN_BASE + (IrixGuestAddr)(i * IRIX_X11_SCREEN_STRIDE);
    IrixGuestAddr visuals = IRIX_X11_VISUAL_BASE + (IrixGuestAddr)(i * IRIX_X11_VISUAL_STRIDE);
    IrixGuestAddr vendor = copy_host_string(ServerVendor(dpy));
    IrixGuestAddr name = copy_host_string(DisplayString(dpy));
    int ns = ScreenCount(dpy);
    int s;

    /* Classic 32-bit Xlib Display layout used by Xlib.h macros on IRIX/N32. */
    wr32(rt, d + 0, 0);                  /* ext_data */
    wr32(rt, d + 4, 0);                  /* private1 */
    wr32(rt, d + 8, (uint32_t)ConnectionNumber(dpy));
    wr32(rt, d + 12, 0);                 /* private2 */
    wr32(rt, d + 16, (uint32_t)ProtocolVersion(dpy));
    wr32(rt, d + 20, (uint32_t)ProtocolRevision(dpy));
    wr32(rt, d + 24, vendor);
    wr32(rt, d + 48, (uint32_t)ImageByteOrder(dpy));
    wr32(rt, d + 52, (uint32_t)BitmapUnit(dpy));
    wr32(rt, d + 56, (uint32_t)BitmapPad(dpy));
    wr32(rt, d + 60, (uint32_t)BitmapBitOrder(dpy));
    wr32(rt, d + 76, (uint32_t)VendorRelease(dpy));
    wr32(rt, d + 88, (uint32_t)QLength(dpy));
    wr32(rt, d + 92, (uint32_t)LastKnownRequestProcessed(dpy));
    wr32(rt, d + 96, (uint32_t)(NextRequest(dpy) - 1));
    wr32(rt, d + 128, name);
    wr32(rt, d + 132, (uint32_t)DefaultScreen(dpy));
    wr32(rt, d + 136, (uint32_t)ns);
    wr32(rt, d + 140, screens);

    if (ns > 16) ns = 16;
    for (s = 0; s < ns; ++s) {
        Screen *hs = ScreenOfDisplay(dpy, s);
        Visual *hv = DefaultVisual(dpy, s);
        IrixGuestAddr gs = screens + (IrixGuestAddr)(s * 80u);
        IrixGuestAddr gv = visuals + (IrixGuestAddr)(s * 32u);
        write_visual_shadow(rt, gv, hv);
        wr32(rt, gs + 0, 0);
        wr32(rt, gs + 4, d);
        wr32(rt, gs + 8, (uint32_t)hs->root);
        wr32(rt, gs + 12, (uint32_t)hs->width);
        wr32(rt, gs + 16, (uint32_t)hs->height);
        wr32(rt, gs + 20, (uint32_t)hs->mwidth);
        wr32(rt, gs + 24, (uint32_t)hs->mheight);
        wr32(rt, gs + 28, (uint32_t)hs->ndepths);
        wr32(rt, gs + 32, 0);            /* Depth[] needs explicit marshalling if used. */
        wr32(rt, gs + 36, (uint32_t)hs->root_depth);
        wr32(rt, gs + 40, gv);
        wr32(rt, gs + 44, gc_to_guest(hs->default_gc));
        wr32(rt, gs + 48, (uint32_t)hs->cmap);
        wr32(rt, gs + 52, (uint32_t)hs->white_pixel);
        wr32(rt, gs + 56, (uint32_t)hs->black_pixel);
        wr32(rt, gs + 60, (uint32_t)hs->max_maps);
        wr32(rt, gs + 64, (uint32_t)hs->min_maps);
        wr32(rt, gs + 68, (uint32_t)hs->backing_store);
        wr32(rt, gs + 72, (uint32_t)hs->save_unders);
        wr32(rt, gs + 76, (uint32_t)hs->root_input_mask);
    }
}

static IrixGuestAddr display_to_guest(Display *dpy)
{
    size_t i;
    if (!dpy) return 0;
    for (i = 0; i < g_x11.display_count; ++i) {
        if (g_x11.displays[i] == dpy) {
            write_display_shadow(i, dpy);
            return display_guest_addr(i);
        }
    }
    if (g_x11.display_count >= IRIX_X11_MAX_DISPLAYS) return 0;
    i = g_x11.display_count++;
    g_x11.displays[i] = dpy;
    write_display_shadow(i, dpy);
    return display_guest_addr(i);
}

static GC gc_from_guest(IrixGuestAddr addr)
{
    uint32_t i;
    if (addr < IRIX_X11_GC_BASE) return 0;
    i = (addr - IRIX_X11_GC_BASE) / 4u;
    return i < g_x11.gc_count ? g_x11.gcs[i] : 0;
}

static IrixGuestAddr gc_to_guest(GC gc)
{
    size_t i;
    if (!gc) return 0;
    for (i = 0; i < g_x11.gc_count; ++i)
        if (g_x11.gcs[i] == gc)
            return IRIX_X11_GC_BASE + (IrixGuestAddr)(i * 4u);
    if (g_x11.gc_count >= IRIX_X11_MAX_GCS) return 0;
    i = g_x11.gc_count++;
    g_x11.gcs[i] = gc;
    return IRIX_X11_GC_BASE + (IrixGuestAddr)(i * 4u);
}

static void bridge_XrmParseCommand(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    /* For the current desktop bootstrap argc is one, so there is nothing to
       consume. Keep the call ABI-correct and leave the Xrm database untouched.
       Full option-list marshalling can be layered in when command-line options
       are actually exercised. */
    (void)rt;
    cpu->gpr[2] = 0;
}

static void bridge_XOpenDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = display_to_guest(XOpenDisplay(name));
}

static void bridge_XCloseDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCloseDisplay(d) : 0;
}

static void bridge_XDefaultScreen(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultScreen(d) : 0;
}

static void bridge_XRootWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)RootWindow(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultRootWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultRootWindow(d) : 0;
}

static void bridge_XDisplayWidth(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DisplayWidth(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDisplayHeight(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DisplayHeight(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultDepth(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultDepth(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XBlackPixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)BlackPixel(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XWhitePixel(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)WhitePixel(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XDefaultColormap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)DefaultColormap(d, (int)cpu->gpr[5]) : 0;
}

static void bridge_XInternAtom(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (d && name) ? (uint32_t)XInternAtom(d, name, (Bool)cpu->gpr[6]) : 0;
}

static void bridge_XGetAtomName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    char *s = d ? XGetAtomName(d, (Atom)(uint32_t)cpu->gpr[5]) : 0;
    IrixGuestAddr out = 0;
    if (s) {
        size_t n = strlen(s) + 1u;
        out = tmp_alloc((uint32_t)n);
        if (out) {
            void *p = irix_guest_ptr(rt, out, n);
            if (p) memcpy(p, s, n); else out = 0;
        }
        XFree(s);
    }
    cpu->gpr[2] = out;
}

#define SIMPLE_DISPLAY0(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt; \
    cpu->gpr[2] = d ? (uint32_t)(call) : 0; }

SIMPLE_DISPLAY0(bridge_XFlush, XFlush(d))
SIMPLE_DISPLAY0(bridge_XSync, XSync(d, (Bool)cpu->gpr[5]))
SIMPLE_DISPLAY0(bridge_XPending, XPending(d))
SIMPLE_DISPLAY0(bridge_XEventsQueued, XEventsQueued(d, (int)cpu->gpr[5]))
SIMPLE_DISPLAY0(bridge_XBell, XBell(d, (int)cpu->gpr[5]))

static void bridge_XCreateSimpleWindow(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCreateSimpleWindow(d, (Window)(uint32_t)cpu->gpr[5],
        (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9],
        (unsigned)cpu->gpr[10], (unsigned long)cpu->gpr[11], (unsigned long)cpu->gpr[12]) : 0;
}

#define WINDOW_CALL1(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt; \
    cpu->gpr[2] = d ? (uint32_t)(call) : 0; }

WINDOW_CALL1(bridge_XDestroyWindow, XDestroyWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapWindow, XMapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMapRaised, XMapRaised(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XUnmapWindow, XUnmapWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XRaiseWindow, XRaiseWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XLowerWindow, XLowerWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XClearWindow, XClearWindow(d, (Window)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XMoveWindow, XMoveWindow(d, (Window)(uint32_t)cpu->gpr[5], (int)cpu->gpr[6], (int)cpu->gpr[7]))
WINDOW_CALL1(bridge_XResizeWindow, XResizeWindow(d, (Window)(uint32_t)cpu->gpr[5], (unsigned)cpu->gpr[6], (unsigned)cpu->gpr[7]))
WINDOW_CALL1(bridge_XMoveResizeWindow, XMoveResizeWindow(d, (Window)(uint32_t)cpu->gpr[5], (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9]))
WINDOW_CALL1(bridge_XSelectInput, XSelectInput(d, (Window)(uint32_t)cpu->gpr[5], (long)(int32_t)cpu->gpr[6]))
WINDOW_CALL1(bridge_XSetWindowBackground, XSetWindowBackground(d, (Window)(uint32_t)cpu->gpr[5], (unsigned long)cpu->gpr[6]))
WINDOW_CALL1(bridge_XSetWindowBorder, XSetWindowBorder(d, (Window)(uint32_t)cpu->gpr[5], (unsigned long)cpu->gpr[6]))

static void bridge_XClearArea(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XClearArea(d, (Window)(uint32_t)cpu->gpr[5],
        (int)cpu->gpr[6], (int)cpu->gpr[7], (unsigned)cpu->gpr[8], (unsigned)cpu->gpr[9], (Bool)cpu->gpr[10]) : 0;
}

static void bridge_XStoreName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    cpu->gpr[2] = (d && s) ? (uint32_t)XStoreName(d, (Window)(uint32_t)cpu->gpr[5], s) : 0;
}

static void bridge_XSetIconName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    cpu->gpr[2] = (d && s) ? (uint32_t)XSetIconName(d, (Window)(uint32_t)cpu->gpr[5], s) : 0;
}

static void bridge_XCreatePixmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? (uint32_t)XCreatePixmap(d, (Drawable)(uint32_t)cpu->gpr[5],
        (unsigned)cpu->gpr[6], (unsigned)cpu->gpr[7], (unsigned)cpu->gpr[8]) : 0;
}

WINDOW_CALL1(bridge_XFreePixmap, XFreePixmap(d, (Pixmap)(uint32_t)cpu->gpr[5]))

static void bridge_XCreateGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2] = d ? gc_to_guest(XCreateGC(d, (Drawable)(uint32_t)cpu->gpr[5], 0, 0)) : 0;
}

static void bridge_XFreeGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]); (void)rt;
    cpu->gpr[2] = (d && gc) ? (uint32_t)XFreeGC(d, gc) : 0;
}

#define GC_CALL(name, call) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]); \
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]); (void)rt; \
    cpu->gpr[2] = (d && gc) ? (uint32_t)(call) : 0; }

GC_CALL(bridge_XSetForeground, XSetForeground(d, gc, (unsigned long)cpu->gpr[6]))
GC_CALL(bridge_XSetBackground, XSetBackground(d, gc, (unsigned long)cpu->gpr[6]))
GC_CALL(bridge_XSetFunction, XSetFunction(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetFillStyle, XSetFillStyle(d, gc, (int)cpu->gpr[6]))
GC_CALL(bridge_XSetClipMask, XSetClipMask(d, gc, (Pixmap)(uint32_t)cpu->gpr[6]))
GC_CALL(bridge_XSetClipOrigin, XSetClipOrigin(d, gc, (int)cpu->gpr[6], (int)cpu->gpr[7]))

static void bridge_XSetLineAttributes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = gc_from_guest((uint32_t)cpu->gpr[5]); (void)rt;
    cpu->gpr[2] = (d && gc) ? (uint32_t)XSetLineAttributes(d, gc, (unsigned)cpu->gpr[6],
        (int)cpu->gpr[7], (int)cpu->gpr[8], (int)cpu->gpr[9]) : 0;
}

static void bridge_XDrawLine(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawLine(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(int)cpu->gpr[9],(int)cpu->gpr[10]):0;
}

static void bridge_XDrawRectangle(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawRectangle(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10]):0;
}

static void bridge_XFillRectangle(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[6]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XFillRectangle(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10]):0;
}

static void bridge_XCopyArea(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); GC gc=gc_from_guest((uint32_t)cpu->gpr[7]); (void)rt;
    cpu->gpr[2]=(d&&gc)?(uint32_t)XCopyArea(d,(Drawable)(uint32_t)cpu->gpr[5],(Drawable)(uint32_t)cpu->gpr[6],gc,
        (int)cpu->gpr[8],(int)cpu->gpr[9],(unsigned)cpu->gpr[10],(unsigned)cpu->gpr[11],(int)cpu->gpr[12],(int)cpu->gpr[13]):0;
}

static void bridge_XGetGeometry(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); Window root=0; int x=0,y=0; unsigned w=0,h=0,b=0,depth=0; Status s=0;
    if (d) s=XGetGeometry(d,(Drawable)(uint32_t)cpu->gpr[5],&root,&x,&y,&w,&h,&b,&depth);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)root); wr32(rt,(uint32_t)cpu->gpr[7],(uint32_t)x);
    wr32(rt,(uint32_t)cpu->gpr[8],(uint32_t)y); wr32(rt,(uint32_t)cpu->gpr[9],w);
    wr32(rt,(uint32_t)cpu->gpr[10],h); wr32(rt,(uint32_t)cpu->gpr[11],b); wr32(rt,(uint32_t)cpu->gpr[12],depth);
    cpu->gpr[2]=(uint32_t)s;
}

static void bridge_XTranslateCoordinates(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); int x=0,y=0; Window child=0; Bool s=False;
    if(d) s=XTranslateCoordinates(d,(Window)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6],
        (int)cpu->gpr[7],(int)cpu->gpr[8],&x,&y,&child);
    wr32(rt,(uint32_t)cpu->gpr[9],(uint32_t)x); wr32(rt,(uint32_t)cpu->gpr[10],(uint32_t)y); wr32(rt,(uint32_t)cpu->gpr[11],(uint32_t)child);
    cpu->gpr[2]=(uint32_t)s;
}

static void bridge_XGetInputFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); Window w=0; int revert=0;
    if(d) XGetInputFocus(d,&w,&revert); wr32(rt,(uint32_t)cpu->gpr[5],(uint32_t)w); wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)revert); cpu->gpr[2]=0;
}

static void bridge_XSetInputFocus(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XSetInputFocus(d,(Window)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6],(Time)(uint32_t)cpu->gpr[7]):0;
}

static void bridge_XCreateFontCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XCreateFontCursor(d,(unsigned)cpu->gpr[5]):0;
}

static void bridge_XRecolorCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr fg_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr bg_addr = (uint32_t)cpu->gpr[7];
    XColor fg, bg;

    if (!d || !fg_addr || !bg_addr ||
        !irix_guest_ptr(rt, fg_addr, 12u) || !irix_guest_ptr(rt, bg_addr, 12u)) {
        cpu->gpr[2] = 0;
        return;
    }

    memset(&fg, 0, sizeof(fg));
    memset(&bg, 0, sizeof(bg));
    fg.pixel = (unsigned long)irix_guest_read32be(rt, fg_addr + 0u);
    fg.red = irix_guest_read16be(rt, fg_addr + 4u);
    fg.green = irix_guest_read16be(rt, fg_addr + 6u);
    fg.blue = irix_guest_read16be(rt, fg_addr + 8u);
    fg.flags = (char)irix_guest_read8(rt, fg_addr + 10u);
    fg.pad = (char)irix_guest_read8(rt, fg_addr + 11u);
    bg.pixel = (unsigned long)irix_guest_read32be(rt, bg_addr + 0u);
    bg.red = irix_guest_read16be(rt, bg_addr + 4u);
    bg.green = irix_guest_read16be(rt, bg_addr + 6u);
    bg.blue = irix_guest_read16be(rt, bg_addr + 8u);
    bg.flags = (char)irix_guest_read8(rt, bg_addr + 10u);
    bg.pad = (char)irix_guest_read8(rt, bg_addr + 11u);
    if (rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }

    cpu->gpr[2] = (uint32_t)XRecolorCursor(d, (Cursor)(uint32_t)cpu->gpr[5], &fg, &bg);
}

WINDOW_CALL1(bridge_XFreeCursor, XFreeCursor(d,(Cursor)(uint32_t)cpu->gpr[5]))
WINDOW_CALL1(bridge_XDefineCursor, XDefineCursor(d,(Window)(uint32_t)cpu->gpr[5],(Cursor)(uint32_t)cpu->gpr[6]))
WINDOW_CALL1(bridge_XUndefineCursor, XUndefineCursor(d,(Window)(uint32_t)cpu->gpr[5]))

static void bridge_XSetSelectionOwner(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XSetSelectionOwner(d,(Atom)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6],(Time)(uint32_t)cpu->gpr[7]):0;
}

static void bridge_XGetSelectionOwner(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XGetSelectionOwner(d,(Atom)(uint32_t)cpu->gpr[5]):0;
}

static void bridge_XDeleteProperty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]); (void)rt;
    cpu->gpr[2]=d?(uint32_t)XDeleteProperty(d,(Window)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6]):0;
}

static void bridge_XChangeProperty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=display_from_guest((uint32_t)cpu->gpr[4]);
    int format=(int)cpu->gpr[8]; int nelem=(int)cpu->gpr[11]; IrixGuestAddr data_addr=(uint32_t)cpu->gpr[10];
    const unsigned char *data=0; unsigned long *longs=0; int i;
    if(format==8) data=(const unsigned char *)irix_guest_ptr(rt,data_addr,(size_t)(nelem>0?nelem:0));
    else if(format==16) data=(const unsigned char *)irix_guest_ptr(rt,data_addr,(size_t)(nelem>0?nelem*2:0));
    else if(format==32 && nelem>0) {
        longs=(unsigned long *)calloc((size_t)nelem,sizeof(*longs));
        if(longs) for(i=0;i<nelem;++i) longs[i]=irix_guest_read32be(rt,data_addr+(IrixGuestAddr)i*4u);
        data=(const unsigned char *)longs;
    }
    cpu->gpr[2]=(d && (nelem==0 || data))?(uint32_t)XChangeProperty(d,(Window)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6],
        (Atom)(uint32_t)cpu->gpr[7],format,(int)cpu->gpr[9],data,nelem):0;
    free(longs);
}

void irix_x11_extra_init(IrixGuestRuntime *rt)
{
    memset(&g_x11,0,sizeof(g_x11));
    g_x11.rt=rt;
    (void)irix_guest_map(rt,IRIX_X11_DISPLAY_BASE,IRIX_X11_DISPLAY_SHADOW_SIZE,3u);
    if(irix_guest_map(rt,IRIX_X11_TMP_BASE,IRIX_X11_TMP_SIZE,3u)==0 || irix_guest_ptr(rt,IRIX_X11_TMP_BASE,1))
        g_x11.tmp_cur=IRIX_X11_TMP_BASE;
}

IrixGuestFunction irix_x11_extra_lookup(const char *name)
{
    if(!name) return 0;
#define MAP(n) if(!strcmp(name,#n)) return bridge_##n
    MAP(XrmParseCommand);
    MAP(XOpenDisplay); MAP(XCloseDisplay); MAP(XDefaultScreen); MAP(XRootWindow); MAP(XDefaultRootWindow);
    MAP(XDisplayWidth); MAP(XDisplayHeight); MAP(XDefaultDepth); MAP(XBlackPixel); MAP(XWhitePixel); MAP(XDefaultColormap);
    MAP(XInternAtom); MAP(XGetAtomName); MAP(XFlush); MAP(XSync); MAP(XPending); MAP(XEventsQueued); MAP(XBell);
    MAP(XCreateSimpleWindow); MAP(XDestroyWindow); MAP(XMapWindow); MAP(XMapRaised); MAP(XUnmapWindow); MAP(XRaiseWindow); MAP(XLowerWindow);
    MAP(XMoveWindow); MAP(XResizeWindow); MAP(XMoveResizeWindow); MAP(XSelectInput); MAP(XSetWindowBackground); MAP(XSetWindowBorder); MAP(XClearWindow); MAP(XClearArea);
    MAP(XStoreName); MAP(XSetIconName); MAP(XCreatePixmap); MAP(XFreePixmap); MAP(XCreateGC); MAP(XFreeGC);
    MAP(XSetForeground); MAP(XSetBackground); MAP(XSetFunction); MAP(XSetFillStyle); MAP(XSetClipMask); MAP(XSetClipOrigin); MAP(XSetLineAttributes);
    MAP(XDrawLine); MAP(XDrawRectangle); MAP(XFillRectangle); MAP(XCopyArea); MAP(XGetGeometry); MAP(XTranslateCoordinates);
    MAP(XGetInputFocus); MAP(XSetInputFocus); MAP(XCreateFontCursor); MAP(XRecolorCursor); MAP(XFreeCursor); MAP(XDefineCursor); MAP(XUndefineCursor);
    MAP(XSetSelectionOwner); MAP(XGetSelectionOwner); MAP(XDeleteProperty); MAP(XChangeProperty);
#undef MAP
    return 0;
}
