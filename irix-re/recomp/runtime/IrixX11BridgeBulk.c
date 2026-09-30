#include "IrixGuest.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_BULK_TMP_BASE 0x72900000u
#define IRIX_X11_BULK_TMP_SIZE 0x00100000u
#define IRIX_X11_MAX_FALLBACK_DISPLAYS 16u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr tmp_cur;
    IrixGuestAddr guest_display[IRIX_X11_MAX_FALLBACK_DISPLAYS];
    Display *host_display[IRIX_X11_MAX_FALLBACK_DISPLAYS];
    size_t display_count;
} IrixX11BulkState;

static IrixX11BulkState g_bulk;

static IrixGuestAddr bulk_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || g_bulk.tmp_cur + n < g_bulk.tmp_cur ||
        g_bulk.tmp_cur + n > IRIX_X11_BULK_TMP_BASE + IRIX_X11_BULK_TMP_SIZE)
        return 0;
    a = g_bulk.tmp_cur;
    g_bulk.tmp_cur += n;
    return a;
}

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr a)
{
    return irix_guest_cstr(rt, a, 65536u);
}

static IrixGuestAddr copy_string(IrixGuestRuntime *rt, const char *s)
{
    size_t n;
    IrixGuestAddr a;
    void *p;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    a = bulk_alloc((uint32_t)n);
    if (!a) return 0;
    p = irix_guest_ptr(rt, a, n);
    if (!p) return 0;
    memcpy(p, s, n);
    return a;
}

static Display *fallback_display(IrixGuestRuntime *rt, IrixGuestAddr guest)
{
    size_t i;
    const char *name = 0;
    IrixGuestAddr name_addr;
    if (!guest) return 0;
    for (i = 0; i < g_bulk.display_count; ++i)
        if (g_bulk.guest_display[i] == guest)
            return g_bulk.host_display[i];
    if (g_bulk.display_count >= IRIX_X11_MAX_FALLBACK_DISPLAYS) return 0;
    name_addr = irix_guest_read32be(rt, guest + 128u);
    if (!rt->trapped) name = guest_cstr(rt, name_addr);
    if (rt->trapped) return 0;
    i = g_bulk.display_count++;
    g_bulk.guest_display[i] = guest;
    g_bulk.host_display[i] = XOpenDisplay(name);
    return g_bulk.host_display[i];
}

static IrixGuestAddr guest_screen(IrixGuestRuntime *rt, IrixGuestAddr dpy, int screen)
{
    IrixGuestAddr base;
    uint32_t count;
    if (!dpy || screen < 0) return 0;
    count = irix_guest_read32be(rt, dpy + 136u);
    base = irix_guest_read32be(rt, dpy + 140u);
    if (rt->trapped || (uint32_t)screen >= count) return 0;
    return base + (IrixGuestAddr)((uint32_t)screen * 80u);
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static void write_visual_shadow(IrixGuestRuntime *rt, IrixGuestAddr out, Visual *v)
{
    if (!out || !v) return;
    wr32(rt,out+0u,0u);
    wr32(rt,out+4u,(uint32_t)v->visualid);
    wr32(rt,out+8u,(uint32_t)v->class);
    wr32(rt,out+12u,(uint32_t)v->red_mask);
    wr32(rt,out+16u,(uint32_t)v->green_mask);
    wr32(rt,out+20u,(uint32_t)v->blue_mask);
    wr32(rt,out+24u,(uint32_t)v->bits_per_rgb);
    wr32(rt,out+28u,(uint32_t)v->map_entries);
}

static uint32_t n32_stack_arg32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
}

#define D0() Display *d = fallback_display(rt, (uint32_t)cpu->gpr[4])
#define RET_D(expr) do { D0(); cpu->gpr[2] = d ? (uint32_t)(expr) : 0; } while (0)

static void bridge_XResourceManagerString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    cpu->gpr[2] = d ? copy_string(rt, XResourceManagerString(d)) : 0;
}

static void bridge_XScreenResourceString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr gs = (uint32_t)cpu->gpr[4];
    IrixGuestAddr gd = gs ? irix_guest_read32be(rt, gs + 4u) : 0;
    Display *d = fallback_display(rt, gd);
    int screen = 0;
    IrixGuestAddr screens;
    if (gd && !rt->trapped) {
        screens = irix_guest_read32be(rt, gd + 140u);
        if (screens && gs >= screens) screen = (int)((gs - screens) / 80u);
    }
    cpu->gpr[2] = d ? copy_string(rt, XScreenResourceString(ScreenOfDisplay(d, screen))) : 0;
}

static void bridge_XDisplayString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(copy_string(rt, DisplayString(d))); }
static void bridge_XServerVendor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(copy_string(rt, ServerVendor(d))); }
static void bridge_XProtocolVersion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(ProtocolVersion(d)); }
static void bridge_XProtocolRevision(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(ProtocolRevision(d)); }
static void bridge_XVendorRelease(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(VendorRelease(d)); }
static void bridge_XScreenCount(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(ScreenCount(d)); }
static void bridge_XImageByteOrder(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(ImageByteOrder(d)); }
static void bridge_XBitmapUnit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(BitmapUnit(d)); }
static void bridge_XBitmapPad(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(BitmapPad(d)); }
static void bridge_XBitmapBitOrder(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(BitmapBitOrder(d)); }
static void bridge_XDisplayCells(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(DisplayCells(d, (int)cpu->gpr[5])); }
static void bridge_XDisplayPlanes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(DisplayPlanes(d, (int)cpu->gpr[5])); }
static void bridge_XQLength(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(QLength(d)); }
static void bridge_XNextRequest(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(NextRequest(d)); }
static void bridge_XLastKnownRequestProcessed(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ RET_D(LastKnownRequestProcessed(d)); }

static void bridge_XSynchronize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    if (d) (void)XSynchronize(d, (Bool)cpu->gpr[5]);
    cpu->gpr[2] = 0;
}

static void bridge_XSetAfterFunction(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    if (d && (uint32_t)cpu->gpr[5] == 0)
        (void)XSetAfterFunction(d, 0);
    cpu->gpr[2] = 0;
}

static void bridge_XScreenOfDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = guest_screen(rt, (uint32_t)cpu->gpr[4], (int)cpu->gpr[5]);
}
static void bridge_XDefaultVisual(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr s = guest_screen(rt, (uint32_t)cpu->gpr[4], (int)cpu->gpr[5]);
    cpu->gpr[2] = s ? irix_guest_read32be(rt, s + 40u) : 0;
}
static void bridge_XDefaultGC(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr s = guest_screen(rt, (uint32_t)cpu->gpr[4], (int)cpu->gpr[5]);
    cpu->gpr[2] = s ? irix_guest_read32be(rt, s + 44u) : 0;
}

#define SCREEN_RET(name, off) \
static void name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { \
    IrixGuestAddr s=(uint32_t)cpu->gpr[4]; cpu->gpr[2]=s?irix_guest_read32be(rt,s+(off)):0; }
SCREEN_RET(bridge_XDisplayOfScreen, 4u)
SCREEN_RET(bridge_XRootWindowOfScreen, 8u)
SCREEN_RET(bridge_XWidthOfScreen, 12u)
SCREEN_RET(bridge_XHeightOfScreen, 16u)
SCREEN_RET(bridge_XWidthMMOfScreen, 20u)
SCREEN_RET(bridge_XHeightMMOfScreen, 24u)
SCREEN_RET(bridge_XDefaultDepthOfScreen, 36u)
SCREEN_RET(bridge_XDefaultVisualOfScreen, 40u)
SCREEN_RET(bridge_XDefaultGCOfScreen, 44u)
SCREEN_RET(bridge_XDefaultColormapOfScreen, 48u)
SCREEN_RET(bridge_XWhitePixelOfScreen, 52u)
SCREEN_RET(bridge_XBlackPixelOfScreen, 56u)
SCREEN_RET(bridge_XMaxCmapsOfScreen, 60u)
SCREEN_RET(bridge_XMinCmapsOfScreen, 64u)
SCREEN_RET(bridge_XDoesBackingStore, 68u)
SCREEN_RET(bridge_XDoesSaveUnders, 72u)
SCREEN_RET(bridge_XEventMaskOfScreen, 76u)

static void bridge_XGetDefault(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    const char *program = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *option = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    const char *v = (d && program && option) ? XGetDefault(d, program, option) : 0;
    cpu->gpr[2] = copy_string(rt, v);
}

static void bridge_XParseGeometry(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    int x=0,y=0; unsigned w=0,h=0;
    int m = s ? XParseGeometry(s,&x,&y,&w,&h) : 0;
    wr32(rt,(uint32_t)cpu->gpr[5],(uint32_t)x);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)y);
    wr32(rt,(uint32_t)cpu->gpr[7],w);
    wr32(rt,(uint32_t)cpu->gpr[8],h);
    cpu->gpr[2]=(uint32_t)m;
}

static void bridge_XQueryPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0(); Window root=0,child=0; int rx=0,ry=0,wx=0,wy=0; unsigned mask=0; Bool ok=False;
    IrixGuestAddr mask_return=n32_stack_arg32(rt,cpu,0u);
    if(d && !rt->trapped) ok=XQueryPointer(d,(Window)(uint32_t)cpu->gpr[5],&root,&child,&rx,&ry,&wx,&wy,&mask);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)root); wr32(rt,(uint32_t)cpu->gpr[7],(uint32_t)child);
    wr32(rt,(uint32_t)cpu->gpr[8],(uint32_t)rx); wr32(rt,(uint32_t)cpu->gpr[9],(uint32_t)ry);
    wr32(rt,(uint32_t)cpu->gpr[10],(uint32_t)wx); wr32(rt,(uint32_t)cpu->gpr[11],(uint32_t)wy);
    wr32(rt,mask_return,mask); cpu->gpr[2]=ok?1u:0u;
}

static void bridge_XWarpPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    int dest_y=(int32_t)n32_stack_arg32(rt,cpu,0u);
    cpu->gpr[2]=(d && !rt->trapped)?(uint32_t)XWarpPointer(d,(Window)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6],
        (int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10],(int)cpu->gpr[11],dest_y):0;
}

static void bridge_XGetWindowAttributes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0(); XWindowAttributes a; Status ok=0; IrixGuestAddr out=(uint32_t)cpu->gpr[6];
    memset(&a,0,sizeof(a)); if(d) ok=XGetWindowAttributes(d,(Window)(uint32_t)cpu->gpr[5],&a);
    if(ok && out) {
        wr32(rt,out+0,(uint32_t)a.x); wr32(rt,out+4,(uint32_t)a.y); wr32(rt,out+8,(uint32_t)a.width); wr32(rt,out+12,(uint32_t)a.height);
        wr32(rt,out+16,(uint32_t)a.border_width); wr32(rt,out+20,(uint32_t)a.depth); wr32(rt,out+28,(uint32_t)a.root);
        wr32(rt,out+32,(uint32_t)a.class); wr32(rt,out+36,(uint32_t)a.bit_gravity); wr32(rt,out+40,(uint32_t)a.win_gravity);
        wr32(rt,out+44,(uint32_t)a.backing_store); wr32(rt,out+48,(uint32_t)a.backing_planes); wr32(rt,out+52,(uint32_t)a.backing_pixel);
        wr32(rt,out+56,(uint32_t)a.save_under); wr32(rt,out+60,(uint32_t)a.colormap); wr32(rt,out+64,(uint32_t)a.map_installed);
        wr32(rt,out+68,(uint32_t)a.map_state); wr32(rt,out+72,(uint32_t)a.all_event_masks); wr32(rt,out+76,(uint32_t)a.your_event_mask);
        wr32(rt,out+80,(uint32_t)a.do_not_propagate_mask); wr32(rt,out+84,(uint32_t)a.override_redirect);
    }
    cpu->gpr[2]=(uint32_t)ok;
}

static void bridge_XQueryTree(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0(); Window root=0,parent=0,*children=0; unsigned n=0,i; Status ok=0; IrixGuestAddr ga=0;
    if(d) ok=XQueryTree(d,(Window)(uint32_t)cpu->gpr[5],&root,&parent,&children,&n);
    if(ok && n) {
        ga=bulk_alloc(n*4u);
        if(ga) for(i=0;i<n;++i) wr32(rt,ga+i*4u,(uint32_t)children[i]);
    }
    if(children) XFree(children);
    wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)root); wr32(rt,(uint32_t)cpu->gpr[7],(uint32_t)parent);
    wr32(rt,(uint32_t)cpu->gpr[8],ga); wr32(rt,(uint32_t)cpu->gpr[9],n); cpu->gpr[2]=(uint32_t)ok;
}

static void bridge_XListProperties(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0(); int n=0,i; Atom *p=d?XListProperties(d,(Window)(uint32_t)cpu->gpr[5],&n):0; IrixGuestAddr ga=0;
    if(p && n>0) { ga=bulk_alloc((uint32_t)n*4u); if(ga) for(i=0;i<n;++i) wr32(rt,ga+(IrixGuestAddr)i*4u,(uint32_t)p[i]); }
    if(p) XFree(p); wr32(rt,(uint32_t)cpu->gpr[6],(uint32_t)n); cpu->gpr[2]=ga;
}

static void bridge_XGetWindowProperty(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0(); Atom type=0; int format=0; unsigned long n=0,after=0; unsigned char *data=0; int rc=BadRequest; IrixGuestAddr ga=0; size_t bytes=0,i;
    IrixGuestAddr actual_type_return=(uint32_t)cpu->gpr[11];
    IrixGuestAddr actual_format_return=n32_stack_arg32(rt,cpu,0u);
    IrixGuestAddr nitems_return=n32_stack_arg32(rt,cpu,1u);
    IrixGuestAddr bytes_after_return=n32_stack_arg32(rt,cpu,2u);
    IrixGuestAddr prop_return=n32_stack_arg32(rt,cpu,3u);
    if(d && !rt->trapped) rc=XGetWindowProperty(d,(Window)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6],
        (long)(int32_t)cpu->gpr[7],(long)(int32_t)cpu->gpr[8],(Bool)cpu->gpr[9],(Atom)(uint32_t)cpu->gpr[10],&type,&format,&n,&after,&data);
    if(rc==Success && data && n) {
        bytes = format==8 ? (size_t)n : format==16 ? (size_t)n*2u : (size_t)n*4u;
        if(bytes<=UINT32_MAX) ga=bulk_alloc((uint32_t)bytes);
        if(ga) {
            if(format==32) for(i=0;i<n;++i) wr32(rt,ga+(IrixGuestAddr)i*4u,(uint32_t)((unsigned long *)data)[i]);
            else { void *dst=irix_guest_ptr(rt,ga,bytes); if(dst) memcpy(dst,data,bytes); }
        }
    }
    if(data) XFree(data);
    wr32(rt,actual_type_return,(uint32_t)type);
    wr32(rt,actual_format_return,(uint32_t)format);
    wr32(rt,nitems_return,(uint32_t)n);
    wr32(rt,bytes_after_return,(uint32_t)after);
    wr32(rt,prop_return,ga);
    cpu->gpr[2]=(uint32_t)rc;
}

static void bridge_XGetVisualInfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    D0();
    long mask=(long)(int32_t)cpu->gpr[5];
    IrixGuestAddr tmpl_addr=(uint32_t)cpu->gpr[6];
    IrixGuestAddr nitems_return=(uint32_t)cpu->gpr[7];
    XVisualInfo tmpl;
    XVisualInfo *list=0;
    int n=0,i;
    IrixGuestAddr out=0;
    memset(&tmpl,0,sizeof(tmpl));

    if(tmpl_addr) {
        tmpl.visualid=(VisualID)irix_guest_read32be(rt,tmpl_addr+4u);
        tmpl.screen=(int32_t)irix_guest_read32be(rt,tmpl_addr+8u);
        tmpl.depth=(int32_t)irix_guest_read32be(rt,tmpl_addr+12u);
        tmpl.class=(int32_t)irix_guest_read32be(rt,tmpl_addr+16u);
        tmpl.red_mask=(unsigned long)irix_guest_read32be(rt,tmpl_addr+20u);
        tmpl.green_mask=(unsigned long)irix_guest_read32be(rt,tmpl_addr+24u);
        tmpl.blue_mask=(unsigned long)irix_guest_read32be(rt,tmpl_addr+28u);
        tmpl.colormap_size=(int32_t)irix_guest_read32be(rt,tmpl_addr+32u);
        tmpl.bits_per_rgb=(int32_t)irix_guest_read32be(rt,tmpl_addr+36u);
    }

    if(d && !rt->trapped) list=XGetVisualInfo(d,mask,&tmpl,&n);
    if(list && n>0 && (uint32_t)n<=UINT32_MAX/40u) {
        out=bulk_alloc((uint32_t)n*40u);
        if(out) {
            for(i=0;i<n;++i) {
                IrixGuestAddr row=out+(IrixGuestAddr)i*40u;
                IrixGuestAddr gv=bulk_alloc(32u);
                if(gv) write_visual_shadow(rt,gv,list[i].visual);
                wr32(rt,row+0u,gv);
                wr32(rt,row+4u,(uint32_t)list[i].visualid);
                wr32(rt,row+8u,(uint32_t)list[i].screen);
                wr32(rt,row+12u,(uint32_t)list[i].depth);
                wr32(rt,row+16u,(uint32_t)list[i].class);
                wr32(rt,row+20u,(uint32_t)list[i].red_mask);
                wr32(rt,row+24u,(uint32_t)list[i].green_mask);
                wr32(rt,row+28u,(uint32_t)list[i].blue_mask);
                wr32(rt,row+32u,(uint32_t)list[i].colormap_size);
                wr32(rt,row+36u,(uint32_t)list[i].bits_per_rgb);
            }
        }
    }
    if(list) XFree(list);
    wr32(rt,nitems_return,(uint32_t)n);
    cpu->gpr[2]=out;
}

static void bridge_XSetCloseDownMode(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ D0(); (void)rt; cpu->gpr[2]=d?(uint32_t)XSetCloseDownMode(d,(int)cpu->gpr[5]):0; }
static void bridge_XKillClient(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ D0(); (void)rt; cpu->gpr[2]=d?(uint32_t)XKillClient(d,(XID)(uint32_t)cpu->gpr[5]):0; }

void irix_x11_bulk_init(IrixGuestRuntime *rt)
{
    memset(&g_bulk,0,sizeof(g_bulk)); g_bulk.rt=rt;
    if(irix_guest_map(rt,IRIX_X11_BULK_TMP_BASE,IRIX_X11_BULK_TMP_SIZE,3u)==0 || irix_guest_ptr(rt,IRIX_X11_BULK_TMP_BASE,1))
        g_bulk.tmp_cur=IRIX_X11_BULK_TMP_BASE;
}

IrixGuestFunction irix_x11_bulk_lookup(const char *name)
{
    if(!name) return 0;
#define MAP(n) if(!strcmp(name,#n)) return bridge_##n
    MAP(XResourceManagerString); MAP(XScreenResourceString); MAP(XDisplayString); MAP(XServerVendor);
    MAP(XProtocolVersion); MAP(XProtocolRevision); MAP(XVendorRelease); MAP(XScreenCount); MAP(XImageByteOrder);
    MAP(XBitmapUnit); MAP(XBitmapPad); MAP(XBitmapBitOrder); MAP(XDisplayCells); MAP(XDisplayPlanes); MAP(XQLength);
    MAP(XNextRequest); MAP(XLastKnownRequestProcessed); MAP(XSynchronize); MAP(XSetAfterFunction);
    MAP(XScreenOfDisplay); MAP(XDefaultVisual); MAP(XDefaultGC);
    MAP(XDisplayOfScreen); MAP(XRootWindowOfScreen); MAP(XWidthOfScreen); MAP(XHeightOfScreen); MAP(XWidthMMOfScreen); MAP(XHeightMMOfScreen);
    MAP(XDefaultDepthOfScreen); MAP(XDefaultVisualOfScreen); MAP(XDefaultGCOfScreen); MAP(XDefaultColormapOfScreen);
    MAP(XWhitePixelOfScreen); MAP(XBlackPixelOfScreen); MAP(XMaxCmapsOfScreen); MAP(XMinCmapsOfScreen); MAP(XDoesBackingStore); MAP(XDoesSaveUnders); MAP(XEventMaskOfScreen);
    MAP(XGetDefault); MAP(XParseGeometry); MAP(XQueryPointer); MAP(XWarpPointer); MAP(XGetWindowAttributes); MAP(XQueryTree);
    MAP(XListProperties); MAP(XGetWindowProperty); MAP(XGetVisualInfo); MAP(XSetCloseDownMode); MAP(XKillClient);
#undef MAP
    return 0;
}
