#define irix_x11_bulk_lookup irix_x11_bulk_lookup_base
#include "IrixX11BridgeBulk.c"
#include <stdio.h>
#undef irix_x11_bulk_lookup

#include "IrixX11BridgeExtraAccess.h"
#include "IrixX11WindowBridgeAccess.h"
#include "IrixX11Color.h"

#include <X11/Xutil.h>
#include <stdint.h>

static uint32_t common_stack32(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned index)
{
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    return (uint32_t)irix_guest_read64be(rt, sp + (IrixGuestAddr)(index * 8u));
}

static const char *common_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static GC common_gc_from_guest(IrixGuestRuntime *rt, Display *display,
                               Drawable drawable, IrixGuestAddr guest_gc,
                               GC *owned_gc)
{
    GC gc;
    XGCValues values;
    unsigned long mask = GCFunction | GCPlaneMask | GCForeground |
                         GCBackground;

    *owned_gc = 0;
    gc = irix_x11_primary_gc_from_guest(guest_gc);
    if (gc)
        return gc;

    /*
     * Original SGI libX11 keeps GC objects in guest memory. The standard
     * Xmb/Xwc font-set calls are bridged to host Xlib, so a guest GC pointer
     * cannot be passed through as a host GC. Copy the pixel and raster
     * fields used for text into a short-lived host GC on the same Display.
     */
    if (!display || !guest_gc ||
        !irix_guest_ptr(rt, guest_gc, 36u) || rt->trapped)
        return 0;

    memset(&values, 0, sizeof(values));
    values.function = (int)irix_guest_read32be(rt, guest_gc + 20u);
    values.plane_mask = (unsigned long)irix_guest_read32be(rt,
                                                          guest_gc + 24u);
    values.foreground = (unsigned long)irix_guest_read32be(rt,
                                                           guest_gc + 28u);
    values.background = (unsigned long)irix_guest_read32be(rt,
                                                           guest_gc + 32u);
    if (rt->trapped)
        return 0;

    gc = XCreateGC(display, drawable, mask, &values);
    if (gc)
        *owned_gc = gc;
    return gc;
}

static IrixGuestAddr common_copy_string(IrixGuestRuntime *rt, const char *s)
{
    IrixGuestAddr out;
    size_t n;
    void *dst;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    out = bulk_alloc((uint32_t)n);
    if (!out) return 0;
    dst = irix_guest_ptr(rt, out, n);
    if (!dst) return 0;
    memcpy(dst, s, n);
    return out;
}

static int common_read_color(IrixGuestRuntime *rt, IrixGuestAddr addr, XColor *c)
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

static void common_write_color(IrixGuestRuntime *rt, IrixGuestAddr addr, const XColor *c)
{
    if (!addr || !c || !irix_guest_ptr(rt, addr, 12u)) return;
    irix_guest_write32be(rt, addr + 0u, (uint32_t)c->pixel);
    irix_guest_write16be(rt, addr + 4u, c->red);
    irix_guest_write16be(rt, addr + 6u, c->green);
    irix_guest_write16be(rt, addr + 8u, c->blue);
    irix_guest_write8(rt, addr + 10u, (uint8_t)c->flags);
    irix_guest_write8(rt, addr + 11u, (uint8_t)c->pad);
}

static Visual *common_visual_from_guest(IrixGuestRuntime *rt, Display *dpy, IrixGuestAddr guest)
{
    XVisualInfo t, *v;
    int n = 0;
    Visual *out = 0;
    if (!guest) return DefaultVisual(dpy, DefaultScreen(dpy));
    if (!irix_guest_ptr(rt, guest, 8u)) return 0;
    memset(&t, 0, sizeof(t));
    t.visualid = (VisualID)irix_guest_read32be(rt, guest + 4u);
    if (rt->trapped) return 0;
    v = XGetVisualInfo(dpy, VisualIDMask, &t, &n);
    if (v && n > 0) out = v[0].visual;
    if (v) XFree(v);
    return out;
}

static XPoint *common_points(IrixGuestRuntime *rt, IrixGuestAddr addr, int n)
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

static XSegment *common_segments(IrixGuestRuntime *rt, IrixGuestAddr addr, int n)
{
    XSegment *p;
    int i;
    if (n <= 0) return 0;
    if (!addr || !irix_guest_ptr(rt, addr, (size_t)n * 8u)) return 0;
    p = (XSegment *)calloc((size_t)n, sizeof(*p));
    if (!p) return 0;
    for (i = 0; i < n; ++i) {
        IrixGuestAddr a = addr + (IrixGuestAddr)i * 8u;
        p[i].x1 = (short)irix_guest_read16be(rt, a + 0u);
        p[i].y1 = (short)irix_guest_read16be(rt, a + 2u);
        p[i].x2 = (short)irix_guest_read16be(rt, a + 4u);
        p[i].y2 = (short)irix_guest_read16be(rt, a + 6u);
    }
    return p;
}

static XRectangle *common_rectangles(IrixGuestRuntime *rt, IrixGuestAddr addr, int n)
{
    XRectangle *p;
    int i;
    if (n <= 0) return 0;
    if (!addr || !irix_guest_ptr(rt, addr, (size_t)n * 8u)) return 0;
    p = (XRectangle *)calloc((size_t)n, sizeof(*p));
    if (!p) return 0;
    for (i = 0; i < n; ++i) {
        IrixGuestAddr a = addr + (IrixGuestAddr)i * 8u;
        p[i].x = (short)irix_guest_read16be(rt, a + 0u);
        p[i].y = (short)irix_guest_read16be(rt, a + 2u);
        p[i].width = irix_guest_read16be(rt, a + 4u);
        p[i].height = irix_guest_read16be(rt, a + 6u);
    }
    return p;
}

static int common_read_text_property(IrixGuestRuntime *rt, IrixGuestAddr addr, XTextProperty *p)
{
    IrixGuestAddr value;
    if (!addr || !p || !irix_guest_ptr(rt, addr, 16u)) return 0;
    memset(p, 0, sizeof(*p));
    value = irix_guest_read32be(rt, addr + 0u);
    p->encoding = (Atom)irix_guest_read32be(rt, addr + 4u);
    p->format = (int32_t)irix_guest_read32be(rt, addr + 8u);
    p->nitems = (unsigned long)irix_guest_read32be(rt, addr + 12u);
    if (value) {
        size_t bytes = p->format == 8 ? (size_t)p->nitems : p->format == 16 ? (size_t)p->nitems * 2u : (size_t)p->nitems * 4u;
        p->value = (unsigned char *)irix_guest_ptr(rt, value, bytes ? bytes : 1u);
    }
    return !rt->trapped && (!value || p->value);
}

static int common_write_text_property(IrixGuestRuntime *rt, IrixGuestAddr addr, XTextProperty *p)
{
    IrixGuestAddr value = 0;
    size_t bytes = 0, i;
    if (!addr || !p || !irix_guest_ptr(rt, addr, 16u)) return 0;
    if (p->value && p->nitems) {
        bytes = p->format == 8 ? (size_t)p->nitems : p->format == 16 ? (size_t)p->nitems * 2u : (size_t)p->nitems * 4u;
        if (bytes > UINT32_MAX) return 0;
        value = bulk_alloc((uint32_t)bytes);
        if (!value) return 0;
        if (p->format == 32) {
            for (i = 0; i < (size_t)p->nitems; ++i)
                irix_guest_write32be(rt, value + (IrixGuestAddr)i * 4u, (uint32_t)((unsigned long *)p->value)[i]);
        } else {
            void *dst = irix_guest_ptr(rt, value, bytes);
            if (!dst) return 0;
            memcpy(dst, p->value, bytes);
        }
    }
    irix_guest_write32be(rt, addr + 0u, value);
    irix_guest_write32be(rt, addr + 4u, (uint32_t)p->encoding);
    irix_guest_write32be(rt, addr + 8u, (uint32_t)p->format);
    irix_guest_write32be(rt, addr + 12u, (uint32_t)p->nitems);
    return !rt->trapped;
}

static IrixGuestAddr common_copy_string_list(IrixGuestRuntime *rt, char **list, int n)
{
    IrixGuestAddr vec;
    int i;
    if (!list || n < 0) return 0;
    vec = bulk_alloc((uint32_t)(n + 1) * 4u);
    if (!vec) return 0;
    for (i = 0; i < n; ++i)
        irix_guest_write32be(rt, vec + (IrixGuestAddr)i * 4u, common_copy_string(rt, list[i]));
    irix_guest_write32be(rt, vec + (IrixGuestAddr)n * 4u, 0u);
    return vec;
}

static char **common_read_string_list(IrixGuestRuntime *rt, IrixGuestAddr vec, int n)
{
    char **out;
    int i;
    if (n < 0) return 0;
    out = (char **)calloc((size_t)n + 1u, sizeof(*out));
    if (!out) return 0;
    for (i = 0; i < n; ++i) {
        IrixGuestAddr a = irix_guest_read32be(rt, vec + (IrixGuestAddr)i * 4u);
        out[i] = (char *)common_cstr(rt, a);
        if (rt->trapped || !out[i]) { free(out); return 0; }
    }
    return out;
}

static void bridge_common_XCreateColormap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_display=(uint32_t)cpu->gpr[4];
    Display *d = irix_x11_primary_display_from_guest(guest_display);
    IrixGuestAddr guest_visual=(uint32_t)cpu->gpr[6];
    Visual *v = d ? common_visual_from_guest(rt, d, guest_visual) : 0;
    Colormap cmap=0;

    if (d && v)
        cmap=XCreateColormap(d,(Window)(uint32_t)cpu->gpr[5],v,(int)cpu->gpr[7]);

    cpu->gpr[2]=(uint32_t)cmap;
}

static void bridge_common_XCreatePixmapFromBitmapData(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    unsigned w = (unsigned)cpu->gpr[7], h = (unsigned)cpu->gpr[8];
    size_t bytes = ((size_t)w + 7u) / 8u * (size_t)h;
    char *data = (char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[6], bytes ? bytes : 1u);
    cpu->gpr[2] = (d && data) ? (uint32_t)XCreatePixmapFromBitmapData(d, (Drawable)(uint32_t)cpu->gpr[5], data, w, h,
        (unsigned long)(uint32_t)cpu->gpr[9], (unsigned long)(uint32_t)cpu->gpr[10], (unsigned)cpu->gpr[11]) : 0;
}

static void bridge_common_XDrawArc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]); GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);
    cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawArc(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10],(int)cpu->gpr[11],(int32_t)common_stack32(rt,cpu,0u)):0;
}
static void bridge_common_XFillArc(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]); GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);
    cpu->gpr[2]=(d&&gc)?(uint32_t)XFillArc(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10],(int)cpu->gpr[11],(int32_t)common_stack32(rt,cpu,0u)):0;
}
static void bridge_common_XDrawPoint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);(void)rt;cpu->gpr[2]=(d&&gc)?(uint32_t)XDrawPoint(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8]):0;}

static void bridge_common_XDrawLines(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XPoint*p=common_points(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XDrawLines(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n,(int)cpu->gpr[9]):0;free(p);}
static void bridge_common_XDrawPoints(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XPoint*p=common_points(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XDrawPoints(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n,(int)cpu->gpr[9]):0;free(p);}
static void bridge_common_XDrawSegments(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XSegment*p=common_segments(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XDrawSegments(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n):0;free(p);}
static void bridge_common_XDrawRectangles(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XRectangle*p=common_rectangles(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XDrawRectangles(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n):0;free(p);}
static void bridge_common_XFillRectangles(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XRectangle*p=common_rectangles(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XFillRectangles(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n):0;free(p);}
static void bridge_common_XFillPolygon(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[8];XPoint*p=common_points(rt,(uint32_t)cpu->gpr[7],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XFillPolygon(d,(Drawable)(uint32_t)cpu->gpr[5],gc,p,n,(int)cpu->gpr[9],(int)cpu->gpr[10]):0;free(p);}

static void bridge_common_XSetClipRectangles(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);int n=(int32_t)cpu->gpr[9];XRectangle*p=common_rectangles(rt,(uint32_t)cpu->gpr[8],n);cpu->gpr[2]=(d&&gc&&(n==0||p))?(uint32_t)XSetClipRectangles(d,gc,(int)cpu->gpr[6],(int)cpu->gpr[7],p,n,(int)cpu->gpr[10]):0;free(p);}
static void bridge_common_XSetStipple(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);(void)rt;cpu->gpr[2]=(d&&gc)?(uint32_t)XSetStipple(d,gc,(Pixmap)(uint32_t)cpu->gpr[6]):0;}
static void bridge_common_XSetTSOrigin(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);(void)rt;cpu->gpr[2]=(d&&gc)?(uint32_t)XSetTSOrigin(d,gc,(int)cpu->gpr[6],(int)cpu->gpr[7]):0;}
static void bridge_common_XSetDashes(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);int n=(int32_t)cpu->gpr[8];char*p=(char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[7],n>0?(size_t)n:1u);cpu->gpr[2]=(d&&gc&&(n<=0||p))?(uint32_t)XSetDashes(d,gc,(int)cpu->gpr[6],p,n):0;}

static void bridge_common_XCopyPlane(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[7]);int dx=(int32_t)common_stack32(rt,cpu,0u),dy=(int32_t)common_stack32(rt,cpu,1u);unsigned long plane=(unsigned long)common_stack32(rt,cpu,2u);cpu->gpr[2]=(d&&gc)?(uint32_t)XCopyPlane(d,(Drawable)(uint32_t)cpu->gpr[5],(Drawable)(uint32_t)cpu->gpr[6],gc,(int)cpu->gpr[8],(int)cpu->gpr[9],(unsigned)cpu->gpr[10],(unsigned)cpu->gpr[11],dx,dy,plane):0;}

static void bridge_common_XStoreColor(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XColor c;cpu->gpr[2]=(d&&common_read_color(rt,(uint32_t)cpu->gpr[6],&c))?(uint32_t)XStoreColor(d,(Colormap)(uint32_t)cpu->gpr[5],&c):0;}
static void bridge_common_XLookupColor(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[6]);XColor exact,screen;Status ok=(d&&s)?irix_x11_lookup_color(rt,d,(Colormap)(uint32_t)cpu->gpr[5],s,&exact,&screen):0;if(ok){common_write_color(rt,(uint32_t)cpu->gpr[7],&exact);common_write_color(rt,(uint32_t)cpu->gpr[8],&screen);}cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XAllocNamedColor(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Colormap cmap=(Colormap)(uint32_t)cpu->gpr[5];const char*s=common_cstr(rt,(uint32_t)cpu->gpr[6]);XColor screen,exact;Status ok=0;if(d&&s){if(!cmap)cmap=DefaultColormap(d,DefaultScreen(d));ok=irix_x11_alloc_named_color(rt,d,cmap,s,&screen,&exact);}if(ok){common_write_color(rt,(uint32_t)cpu->gpr[7],&screen);common_write_color(rt,(uint32_t)cpu->gpr[8],&exact);}cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XQueryColors(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Colormap cmap=(Colormap)(uint32_t)cpu->gpr[5];IrixGuestAddr ga=(uint32_t)cpu->gpr[6];int n=(int32_t)cpu->gpr[7];XColor*cols=0;int i;if(d&&n>0&&ga&&irix_guest_ptr(rt,ga,(size_t)n*12u)){cols=(XColor*)calloc((size_t)n,sizeof(XColor));if(cols){for(i=0;i<n;++i)common_read_color(rt,ga+(IrixGuestAddr)i*12u,&cols[i]);if(!cmap)cmap=DefaultColormap(d,DefaultScreen(d));XQueryColors(d,cmap,cols,n);for(i=0;i<n;++i)common_write_color(rt,ga+(IrixGuestAddr)i*12u,&cols[i]);free(cols);}}cpu->gpr[2]=0;}

static void bridge_common_XGetErrorDatabaseText(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);const char*n=common_cstr(rt,(uint32_t)cpu->gpr[5]);const char*m=common_cstr(rt,(uint32_t)cpu->gpr[6]);const char*def=common_cstr(rt,(uint32_t)cpu->gpr[7]);char*b=(char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[8],(size_t)(uint32_t)cpu->gpr[9]);cpu->gpr[2]=(d&&n&&m&&def&&b)?(uint32_t)XGetErrorDatabaseText(d,n,m,def,b,(int)cpu->gpr[9]):0;}

static void bridge_common_XGetClassHint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);IrixGuestAddr out=(uint32_t)cpu->gpr[6];XClassHint h;Status ok=0;memset(&h,0,sizeof(h));if(d)ok=XGetClassHint(d,(Window)(uint32_t)cpu->gpr[5],&h);if(ok&&out){irix_guest_write32be(rt,out+0u,common_copy_string(rt,h.res_name));irix_guest_write32be(rt,out+4u,common_copy_string(rt,h.res_class));}if(h.res_name)XFree(h.res_name);if(h.res_class)XFree(h.res_class);cpu->gpr[2]=(uint32_t)ok;}

static void bridge_common_XGetTextProperty(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;Status ok=0;memset(&p,0,sizeof(p));if(d)ok=XGetTextProperty(d,(Window)(uint32_t)cpu->gpr[5],&p,(Atom)(uint32_t)cpu->gpr[7]);if(ok)ok=common_write_text_property(rt,(uint32_t)cpu->gpr[6],&p);if(p.value)XFree(p.value);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XSetTextProperty(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;if(d&&common_read_text_property(rt,(uint32_t)cpu->gpr[6],&p))XSetTextProperty(d,(Window)(uint32_t)cpu->gpr[5],&p,(Atom)(uint32_t)cpu->gpr[7]);cpu->gpr[2]=0;}
static void bridge_common_XGetWMName(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;Status ok=0;memset(&p,0,sizeof(p));if(d)ok=XGetWMName(d,(Window)(uint32_t)cpu->gpr[5],&p);if(ok)ok=common_write_text_property(rt,(uint32_t)cpu->gpr[6],&p);if(p.value)XFree(p.value);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XGetWMIconName(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;Status ok=0;memset(&p,0,sizeof(p));if(d)ok=XGetWMIconName(d,(Window)(uint32_t)cpu->gpr[5],&p);if(ok)ok=common_write_text_property(rt,(uint32_t)cpu->gpr[6],&p);if(p.value)XFree(p.value);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XSetWMIconName(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;if(d&&common_read_text_property(rt,(uint32_t)cpu->gpr[6],&p))XSetWMIconName(d,(Window)(uint32_t)cpu->gpr[5],&p);cpu->gpr[2]=0;}
static void bridge_common_XGetTransientForHint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Window w=0;Status ok=d?XGetTransientForHint(d,(Window)(uint32_t)cpu->gpr[5],&w):0;if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],(uint32_t)w);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XSetTransientForHint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);(void)rt;cpu->gpr[2]=d?(uint32_t)XSetTransientForHint(d,(Window)(uint32_t)cpu->gpr[5],(Window)(uint32_t)cpu->gpr[6]):0;}

static void bridge_common_XGetWMColormapWindows(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Window*list=0;int n=0,i;Status ok=d?XGetWMColormapWindows(d,(Window)(uint32_t)cpu->gpr[5],&list,&n):0;IrixGuestAddr out=0;if(ok&&list&&n>0){out=bulk_alloc((uint32_t)n*4u);if(out)for(i=0;i<n;++i)irix_guest_write32be(rt,out+(IrixGuestAddr)i*4u,(uint32_t)list[i]);}if(list)XFree(list);if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],out);if((uint32_t)cpu->gpr[7])irix_guest_write32be(rt,(uint32_t)cpu->gpr[7],(uint32_t)n);cpu->gpr[2]=(uint32_t)ok;}

static void bridge_common_XStringListToTextProperty(IrixGuestRuntime *rt,IrixGuestCpu *cpu){int n=(int32_t)cpu->gpr[5];char**list=common_read_string_list(rt,(uint32_t)cpu->gpr[4],n);XTextProperty p;Status ok=0;memset(&p,0,sizeof(p));if(n==0||list)ok=XStringListToTextProperty(list,n,&p);if(ok)ok=common_write_text_property(rt,(uint32_t)cpu->gpr[6],&p);if(p.value)XFree(p.value);free(list);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XTextPropertyToStringList(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XTextProperty p;char**list=0;int n=0;Status ok=common_read_text_property(rt,(uint32_t)cpu->gpr[4],&p)?XTextPropertyToStringList(&p,&list,&n):0;IrixGuestAddr out=ok?common_copy_string_list(rt,list,n):0;if(list)XFreeStringList(list);if((uint32_t)cpu->gpr[5])irix_guest_write32be(rt,(uint32_t)cpu->gpr[5],out);if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],(uint32_t)n);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_common_XmbTextListToTextProperty(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);int n=(int32_t)cpu->gpr[6];char**list=common_read_string_list(rt,(uint32_t)cpu->gpr[5],n);XTextProperty p;int rc=-1;memset(&p,0,sizeof(p));if(d&&(n==0||list))rc=XmbTextListToTextProperty(d,list,n,(XICCEncodingStyle)(int32_t)cpu->gpr[7],&p);if(rc>=0)common_write_text_property(rt,(uint32_t)cpu->gpr[8],&p);if(p.value)XFree(p.value);free(list);cpu->gpr[2]=(uint64_t)(int64_t)rc;}
static void bridge_common_XmbTextPropertyToTextList(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XTextProperty p;char**list=0;int n=0,rc=-1;if(d&&common_read_text_property(rt,(uint32_t)cpu->gpr[5],&p))rc=XmbTextPropertyToTextList(d,&p,&list,&n);IrixGuestAddr out=rc>=0?common_copy_string_list(rt,list,n):0;if(list)XFreeStringList(list);if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],out);if((uint32_t)cpu->gpr[7])irix_guest_write32be(rt,(uint32_t)cpu->gpr[7],(uint32_t)n);cpu->gpr[2]=(uint64_t)(int64_t)rc;}

static void bridge_common_XMatchVisualInfo(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    IrixGuestAddr guest_display=(uint32_t)cpu->gpr[4];
    int screen=(int32_t)cpu->gpr[5];
    int depth=(int32_t)cpu->gpr[6];
    int visual_class=(int32_t)cpu->gpr[7];
    Display*d=irix_x11_primary_display_from_guest(guest_display);
    XVisualInfo v;
    Status ok=0;
    IrixGuestAddr out=(uint32_t)cpu->gpr[8];

    memset(&v,0,sizeof(v));
    if(d)ok=XMatchVisualInfo(d,screen,depth,visual_class,&v);
    if(ok&&out){
        IrixGuestAddr gv=guest_visual_by_id(rt,guest_display,v.screen,v.visualid);
        if(!gv){
            gv=bulk_alloc(32u);
            if(gv)write_visual_shadow(rt,gv,v.visual);
        }
        irix_guest_write32be(rt,out+0u,gv);
        irix_guest_write32be(rt,out+4u,(uint32_t)v.visualid);
        irix_guest_write32be(rt,out+8u,(uint32_t)v.screen);
        irix_guest_write32be(rt,out+12u,(uint32_t)v.depth);
        irix_guest_write32be(rt,out+16u,(uint32_t)v.class);
        irix_guest_write32be(rt,out+20u,(uint32_t)v.red_mask);
        irix_guest_write32be(rt,out+24u,(uint32_t)v.green_mask);
        irix_guest_write32be(rt,out+28u,(uint32_t)v.blue_mask);
        irix_guest_write32be(rt,out+32u,(uint32_t)v.colormap_size);
        irix_guest_write32be(rt,out+36u,(uint32_t)v.bits_per_rgb);
    }
    cpu->gpr[2]=(uint32_t)ok;
}

static void bridge_common_XListDepths(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);int n=0,i;int*list=d?XListDepths(d,(int)cpu->gpr[5],&n):0;IrixGuestAddr out=0;if(list&&n>0){out=bulk_alloc((uint32_t)n*4u);if(out)for(i=0;i<n;++i)irix_guest_write32be(rt,out+(IrixGuestAddr)i*4u,(uint32_t)list[i]);}if(list)XFree(list);if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],(uint32_t)n);cpu->gpr[2]=out;}
static void bridge_common_XLoadFont(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[5]);Font f=0;if(d){if(!s||!*s)s="fixed";f=XLoadFont(d,s);if(!f&&strcmp(s,"fixed"))f=XLoadFont(d,"fixed");}cpu->gpr[2]=(uint32_t)f;}
static void bridge_common_XUnloadFont(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);(void)rt;cpu->gpr[2]=d?(uint32_t)XUnloadFont(d,(Font)(uint32_t)cpu->gpr[5]):0;}

static void bridge_common_XTextWidth(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontStruct*f=irix_x11_primary_font_from_guest((uint32_t)cpu->gpr[4]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[5]);cpu->gpr[2]=(uint64_t)(int64_t)((f&&s)?XTextWidth(f,s,(int)cpu->gpr[6]):0);}
static void bridge_common_XTextWidth16(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontStruct*f=irix_x11_primary_font_from_guest((uint32_t)cpu->gpr[4]);int n=(int32_t)cpu->gpr[6];XChar2b*s=(XChar2b*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[5],n>0?(size_t)n*2u:1u);cpu->gpr[2]=(uint64_t)(int64_t)((f&&s)?XTextWidth16(f,s,n):0);}

static void common_write_char_struct(IrixGuestRuntime *rt,IrixGuestAddr a,const XCharStruct*c){if(!a||!c)return;irix_guest_write16be(rt,a+0u,(uint16_t)c->lbearing);irix_guest_write16be(rt,a+2u,(uint16_t)c->rbearing);irix_guest_write16be(rt,a+4u,(uint16_t)c->width);irix_guest_write16be(rt,a+6u,(uint16_t)c->ascent);irix_guest_write16be(rt,a+8u,(uint16_t)c->descent);irix_guest_write16be(rt,a+10u,c->attributes);}
static void bridge_common_XTextExtents(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontStruct*f=irix_x11_primary_font_from_guest((uint32_t)cpu->gpr[4]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[5]);int dir=0,asc=0,desc=0;XCharStruct overall;memset(&overall,0,sizeof(overall));if(f&&s)XTextExtents(f,s,(int)cpu->gpr[6],&dir,&asc,&desc,&overall);if((uint32_t)cpu->gpr[7])irix_guest_write32be(rt,(uint32_t)cpu->gpr[7],(uint32_t)dir);if((uint32_t)cpu->gpr[8])irix_guest_write32be(rt,(uint32_t)cpu->gpr[8],(uint32_t)asc);if((uint32_t)cpu->gpr[9])irix_guest_write32be(rt,(uint32_t)cpu->gpr[9],(uint32_t)desc);common_write_char_struct(rt,(uint32_t)cpu->gpr[10],&overall);cpu->gpr[2]=0;}
static void bridge_common_XTextExtents16(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontStruct*f=irix_x11_primary_font_from_guest((uint32_t)cpu->gpr[4]);int n=(int32_t)cpu->gpr[6];XChar2b*s=(XChar2b*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[5],n>0?(size_t)n*2u:1u);int dir=0,asc=0,desc=0;XCharStruct overall;memset(&overall,0,sizeof(overall));if(f&&s)XTextExtents16(f,s,n,&dir,&asc,&desc,&overall);if((uint32_t)cpu->gpr[7])irix_guest_write32be(rt,(uint32_t)cpu->gpr[7],(uint32_t)dir);if((uint32_t)cpu->gpr[8])irix_guest_write32be(rt,(uint32_t)cpu->gpr[8],(uint32_t)asc);if((uint32_t)cpu->gpr[9])irix_guest_write32be(rt,(uint32_t)cpu->gpr[9],(uint32_t)desc);common_write_char_struct(rt,(uint32_t)cpu->gpr[10],&overall);cpu->gpr[2]=0;}

static void bridge_common_XDrawString(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[9]);int n=(int32_t)cpu->gpr[10];cpu->gpr[2]=(d&&gc&&s)?(uint32_t)XDrawString(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],s,n):0;}
static void bridge_common_XDrawImageString(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[9]);int n=(int32_t)cpu->gpr[10];cpu->gpr[2]=(d&&gc&&s)?(uint32_t)XDrawImageString(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],s,n):0;}
static void bridge_common_XDrawString16(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[10];XChar2b*s=(XChar2b*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[9],n>0?(size_t)n*2u:1u);cpu->gpr[2]=(d&&gc&&s)?(uint32_t)XDrawString16(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],s,n):0;}
static void bridge_common_XDrawImageString16(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);int n=(int32_t)cpu->gpr[10];XChar2b*s=(XChar2b*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[9],n>0?(size_t)n*2u:1u);cpu->gpr[2]=(d&&gc&&s)?(uint32_t)XDrawImageString16(d,(Drawable)(uint32_t)cpu->gpr[5],gc,(int)cpu->gpr[7],(int)cpu->gpr[8],s,n):0;}

static void bridge_common_XmbDrawString(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    Display *d=irix_x11_window_font_display_from_guest(rt,(uint32_t)cpu->gpr[4]);
    Drawable drawable=(Drawable)(uint32_t)cpu->gpr[5];
    XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[6]);
    GC owned_gc=0;
    GC gc=common_gc_from_guest(rt,d,drawable,(uint32_t)cpu->gpr[7],&owned_gc);
    const char *s=common_cstr(rt,(uint32_t)cpu->gpr[10]);
    int n=(int32_t)cpu->gpr[11];

    /* Flush SGI Xlib's private connection before drawing through host Xlib. */
    (void)irix_x11_window_sync_guest_display(rt,cpu,(uint32_t)cpu->gpr[4]);
    if(d&&fs&&gc&&s)
        XmbDrawString(d,drawable,fs,gc,(int)cpu->gpr[8],
                      (int)cpu->gpr[9],s,n);
    if (d) XSync(d, False);
    if(owned_gc) XFreeGC(d,owned_gc);
    cpu->gpr[2]=0;
}

static void bridge_common_XmbDrawImageString(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    Display *d=irix_x11_window_font_display_from_guest(rt,(uint32_t)cpu->gpr[4]);
    Drawable drawable=(Drawable)(uint32_t)cpu->gpr[5];
    XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[6]);
    GC owned_gc=0;
    GC gc=common_gc_from_guest(rt,d,drawable,(uint32_t)cpu->gpr[7],&owned_gc);
    const char *s=common_cstr(rt,(uint32_t)cpu->gpr[10]);
    int n=(int32_t)cpu->gpr[11];

    (void)irix_x11_window_sync_guest_display(rt,cpu,(uint32_t)cpu->gpr[4]);
    if(d&&fs&&gc&&s)
        XmbDrawImageString(d,drawable,fs,gc,(int)cpu->gpr[8],
                           (int)cpu->gpr[9],s,n);
    if (d) XSync(d, False);
    if(owned_gc) XFreeGC(d,owned_gc);
    cpu->gpr[2]=0;
}
static void bridge_common_XmbTextEscapement(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[4]);const char*s=common_cstr(rt,(uint32_t)cpu->gpr[5]);cpu->gpr[2]=(uint64_t)(int64_t)((fs&&s)?XmbTextEscapement(fs,s,(int)cpu->gpr[6]):0);}

struct lock_grab_ctx {
    Display *d; unsigned int a; Window w; Bool owner; unsigned int mask;
    int pm, km; Window confine; Cursor cursor; int result; int kind;
};
static void lock_grab_cb(void *p, unsigned int st)
{
    struct lock_grab_ctx *c = (struct lock_grab_ctx *)p;
    switch (c->kind) {
    case 0: c->result = XGrabButton(c->d, c->a, st, c->w, c->owner, c->mask, c->pm, c->km, c->confine, c->cursor); break;
    case 1: c->result = XUngrabButton(c->d, c->a, st, c->w); break;
    case 2: c->result = XGrabKey(c->d, (int)c->a, st, c->w, c->owner, c->pm, c->km); break;
    default: c->result = XUngrabKey(c->d, (int)c->a, st, c->w); break;
    }
}
static void bridge_common_XGrabButton(IrixGuestRuntime *rt,IrixGuestCpu *cpu){
    struct lock_grab_ctx c; memset(&c, 0, sizeof(c));
    c.d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    c.confine=(Window)common_stack32(rt,cpu,0u);
    c.cursor=(Cursor)common_stack32(rt,cpu,1u);
    c.a=(unsigned)cpu->gpr[5]; c.w=(Window)(uint32_t)cpu->gpr[7]; c.owner=(Bool)cpu->gpr[8];
    c.mask=(unsigned)cpu->gpr[9]; c.pm=(int)cpu->gpr[10]; c.km=(int)cpu->gpr[11]; c.kind=0;
    if (c.d) irix_x11_for_each_lock_combo(c.d,(unsigned)cpu->gpr[6],lock_grab_cb,&c);
    cpu->gpr[2]=(uint32_t)c.result;
}
static void bridge_common_XUngrabButton(IrixGuestRuntime *rt,IrixGuestCpu *cpu){
    struct lock_grab_ctx c; memset(&c, 0, sizeof(c)); (void)rt;
    c.d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    c.a=(unsigned)cpu->gpr[5]; c.w=(Window)(uint32_t)cpu->gpr[7]; c.kind=1;
    if (c.d) irix_x11_for_each_lock_combo(c.d,(unsigned)cpu->gpr[6],lock_grab_cb,&c);
    cpu->gpr[2]=(uint32_t)c.result;
}
static void bridge_common_XGrabKey(IrixGuestRuntime *rt,IrixGuestCpu *cpu){
    struct lock_grab_ctx c; memset(&c, 0, sizeof(c)); (void)rt;
    c.d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    c.a=(unsigned)cpu->gpr[5]; c.w=(Window)(uint32_t)cpu->gpr[7]; c.owner=(Bool)cpu->gpr[8];
    c.pm=(int)cpu->gpr[9]; c.km=(int)cpu->gpr[10]; c.kind=2;
    if (c.d) irix_x11_for_each_lock_combo(c.d,(unsigned)cpu->gpr[6],lock_grab_cb,&c);
    cpu->gpr[2]=(uint32_t)c.result;
}
static void bridge_common_XUngrabKey(IrixGuestRuntime *rt,IrixGuestCpu *cpu){
    struct lock_grab_ctx c; memset(&c, 0, sizeof(c)); (void)rt;
    c.d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    c.a=(unsigned)cpu->gpr[5]; c.w=(Window)(uint32_t)cpu->gpr[7]; c.kind=3;
    if (c.d) irix_x11_for_each_lock_combo(c.d,(unsigned)cpu->gpr[6],lock_grab_cb,&c);
    cpu->gpr[2]=(uint32_t)c.result;
}
static void bridge_common_XGrabKeyboard(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XGrabKeyboard(d,(Window)(uint32_t)cpu->gpr[5],(Bool)cpu->gpr[6],(int)cpu->gpr[7],(int)cpu->gpr[8],(Time)(uint32_t)cpu->gpr[9]):GrabNotViewable;(void)rt;}
static void bridge_common_XUngrabKeyboard(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XUngrabKeyboard(d,(Time)(uint32_t)cpu->gpr[5]):0;(void)rt;}
static void bridge_common_XGrabPointer(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Window conf=(Window)(uint32_t)cpu->gpr[10];Cursor cur=(Cursor)(uint32_t)cpu->gpr[11];Time t=(Time)common_stack32(rt,cpu,0u);cpu->gpr[2]=d?(uint32_t)XGrabPointer(d,(Window)(uint32_t)cpu->gpr[5],(Bool)cpu->gpr[6],(unsigned)cpu->gpr[7],(int)cpu->gpr[8],(int)cpu->gpr[9],conf,cur,t):GrabNotViewable;}
static void bridge_common_XUngrabPointer(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XUngrabPointer(d,(Time)(uint32_t)cpu->gpr[5]):0;(void)rt;}
static void bridge_common_XAllowEvents(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XAllowEvents(d,(int)cpu->gpr[5],(Time)(uint32_t)cpu->gpr[6]):0;(void)rt;}
static void bridge_common_XChangeActivePointerGrab(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XChangeActivePointerGrab(d,(unsigned)cpu->gpr[5],(Cursor)(uint32_t)cpu->gpr[6],(Time)(uint32_t)cpu->gpr[7]):0;(void)rt;}

static void bridge_common_XConvertSelection(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XConvertSelection(d,(Atom)(uint32_t)cpu->gpr[5],(Atom)(uint32_t)cpu->gpr[6],(Atom)(uint32_t)cpu->gpr[7],(Window)(uint32_t)cpu->gpr[8],(Time)(uint32_t)cpu->gpr[9]):0;(void)rt;}
static void bridge_common_XIconifyWindow(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XIconifyWindow(d,(Window)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6]):0;(void)rt;}
static void bridge_common_XWithdrawWindow(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XWithdrawWindow(d,(Window)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6]):0;(void)rt;}
static void bridge_common_XRotateBuffers(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=d?(uint32_t)XRotateBuffers(d,(int)cpu->gpr[5]):0;(void)rt;}

static void bridge_common_XStoreBuffer(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);int n=(int32_t)cpu->gpr[6];char*s=(char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[5],n>0?(size_t)n:1u);cpu->gpr[2]=(d&&s)?(uint32_t)XStoreBuffer(d,s,n,(int)cpu->gpr[7]):0;}
static void bridge_common_XFetchBuffer(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);int n=0;char*s=d?XFetchBuffer(d,&n,(int)cpu->gpr[6]):0;IrixGuestAddr out=0;if(s&&n>=0){out=bulk_alloc((uint32_t)n+1u);if(out){void*dst=irix_guest_ptr(rt,out,(size_t)n+1u);if(dst){memcpy(dst,s,(size_t)n);((char*)dst)[n]=0;}}}if(s)XFree(s);if((uint32_t)cpu->gpr[5])irix_guest_write32be(rt,(uint32_t)cpu->gpr[5],(uint32_t)n);cpu->gpr[2]=out;}

static void bridge_common_XConvertCase(IrixGuestRuntime *rt,IrixGuestCpu *cpu){KeySym lo=0,up=0;XConvertCase((KeySym)(uint32_t)cpu->gpr[4],&lo,&up);if((uint32_t)cpu->gpr[5])irix_guest_write32be(rt,(uint32_t)cpu->gpr[5],(uint32_t)lo);if((uint32_t)cpu->gpr[6])irix_guest_write32be(rt,(uint32_t)cpu->gpr[6],(uint32_t)up);cpu->gpr[2]=0;}
static void bridge_common_XDisplayName(IrixGuestRuntime *rt,IrixGuestCpu *cpu){const char*s=common_cstr(rt,(uint32_t)cpu->gpr[4]);cpu->gpr[2]=common_copy_string(rt,XDisplayName(s));}
static void bridge_common_XSupportsLocale(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint32_t)XSupportsLocale();}
static void bridge_common_XSetLocaleModifiers(IrixGuestRuntime *rt,IrixGuestCpu *cpu){const char*s=common_cstr(rt,(uint32_t)cpu->gpr[4]);cpu->gpr[2]=common_copy_string(rt,XSetLocaleModifiers(s));}

static void bridge_common_XAllocClassHint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr a=bulk_alloc(8u);if(a){irix_guest_write32be(rt,a,0);irix_guest_write32be(rt,a+4u,0);}cpu->gpr[2]=a;}
static void bridge_common_XAllocWMHints(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr a=bulk_alloc(36u);if(a){void*p=irix_guest_ptr(rt,a,36u);if(p)memset(p,0,36u);}cpu->gpr[2]=a;}

IrixGuestFunction irix_x11_bulk_lookup(const char *name)
{
    IrixGuestFunction fn = irix_x11_bulk_lookup_base(name);
    if (fn || !name) return fn;
#define MAP(n) if (!strcmp(name, #n)) return bridge_common_##n
    MAP(XCreateColormap); MAP(XCreatePixmapFromBitmapData);
    MAP(XDrawArc); MAP(XFillArc); MAP(XDrawPoint); MAP(XDrawLines); MAP(XDrawPoints); MAP(XDrawSegments); MAP(XDrawRectangles); MAP(XFillRectangles); MAP(XFillPolygon); MAP(XCopyPlane);
    MAP(XSetClipRectangles); MAP(XSetStipple); MAP(XSetTSOrigin); MAP(XSetDashes);
    MAP(XStoreColor); MAP(XLookupColor); MAP(XAllocNamedColor); MAP(XQueryColors); MAP(XGetErrorDatabaseText);
    MAP(XGetClassHint); MAP(XGetTextProperty); MAP(XSetTextProperty); MAP(XGetWMName); MAP(XGetWMIconName); MAP(XSetWMIconName); MAP(XGetTransientForHint); MAP(XSetTransientForHint); MAP(XGetWMColormapWindows);
    MAP(XStringListToTextProperty); MAP(XTextPropertyToStringList); MAP(XmbTextListToTextProperty); MAP(XmbTextPropertyToTextList);
    MAP(XMatchVisualInfo); MAP(XListDepths); MAP(XLoadFont); MAP(XUnloadFont); MAP(XTextWidth); MAP(XTextWidth16); MAP(XTextExtents); MAP(XTextExtents16);
    MAP(XDrawString); MAP(XDrawImageString); MAP(XDrawString16); MAP(XDrawImageString16); MAP(XmbDrawString); MAP(XmbDrawImageString); MAP(XmbTextEscapement);
    MAP(XGrabButton); MAP(XUngrabButton); MAP(XGrabKey); MAP(XUngrabKey); MAP(XGrabKeyboard); MAP(XUngrabKeyboard); MAP(XGrabPointer); MAP(XUngrabPointer); MAP(XAllowEvents); MAP(XChangeActivePointerGrab);
    MAP(XConvertSelection); MAP(XIconifyWindow); MAP(XWithdrawWindow); MAP(XRotateBuffers); MAP(XStoreBuffer); MAP(XFetchBuffer); MAP(XConvertCase); MAP(XDisplayName); MAP(XSupportsLocale); MAP(XSetLocaleModifiers); MAP(XAllocClassHint); MAP(XAllocWMHints);
#undef MAP
    return 0;
}
