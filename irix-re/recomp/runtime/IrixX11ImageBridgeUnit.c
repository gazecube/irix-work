#define irix_x11_image_bridge_lookup irix_x11_image_bridge_lookup_base
#include "IrixX11ImageBridge.c"
#undef irix_x11_image_bridge_lookup

#include "IrixX11BridgeExtraAccess.h"
#include "IrixX11WindowBridgeAccess.h"

#include <stdio.h>
#include <wchar.h>

#define IRIX_N32_XEVENT_SIZE 96u

static IrixGuestAddr g_predicate_event;

static void ew32(IrixGuestRuntime *rt, IrixGuestAddr a, uint32_t v)
{
    irix_guest_write32be(rt, a, v);
}

static uint32_t er32(IrixGuestRuntime *rt, IrixGuestAddr a)
{
    return irix_guest_read32be(rt, a);
}

static int write_n32_event(IrixGuestRuntime *rt, IrixGuestAddr g,
                           IrixGuestAddr guest_display, const XEvent *e)
{
    void *p;
    if (!g || !e) return 0;
    p = irix_guest_ptr(rt, g, IRIX_N32_XEVENT_SIZE);
    if (!p) return 0;
    memset(p, 0, IRIX_N32_XEVENT_SIZE);

    ew32(rt,g+0u,(uint32_t)e->type);
    if (e->type == 0) return !rt->trapped;
    if (e->type == KeyPress || e->type == KeyRelease || e->type == ButtonPress ||
        e->type == ButtonRelease || e->type == MotionNotify || e->type == EnterNotify ||
        e->type == LeaveNotify || e->type == FocusIn || e->type == FocusOut ||
        e->type == KeymapNotify || e->type == Expose || e->type == GraphicsExpose ||
        e->type == NoExpose || e->type == VisibilityNotify || e->type == CreateNotify ||
        e->type == DestroyNotify || e->type == UnmapNotify || e->type == MapNotify ||
        e->type == MapRequest || e->type == ReparentNotify || e->type == ConfigureNotify ||
        e->type == GravityNotify || e->type == ResizeRequest || e->type == ConfigureRequest ||
        e->type == CirculateNotify || e->type == CirculateRequest || e->type == PropertyNotify ||
        e->type == SelectionClear || e->type == SelectionRequest || e->type == SelectionNotify ||
        e->type == ColormapNotify || e->type == ClientMessage || e->type == MappingNotify) {
        ew32(rt,g+4u,(uint32_t)e->xany.serial);
        ew32(rt,g+8u,(uint32_t)e->xany.send_event);
        ew32(rt,g+12u,guest_display);
        ew32(rt,g+16u,(uint32_t)e->xany.window);
    }

    switch (e->type) {
    case KeyPress: case KeyRelease:
        ew32(rt,g+20u,(uint32_t)e->xkey.root); ew32(rt,g+24u,(uint32_t)e->xkey.subwindow); ew32(rt,g+28u,(uint32_t)e->xkey.time);
        ew32(rt,g+32u,(uint32_t)e->xkey.x); ew32(rt,g+36u,(uint32_t)e->xkey.y); ew32(rt,g+40u,(uint32_t)e->xkey.x_root); ew32(rt,g+44u,(uint32_t)e->xkey.y_root);
        ew32(rt,g+48u,(uint32_t)e->xkey.state); ew32(rt,g+52u,(uint32_t)e->xkey.keycode); ew32(rt,g+56u,(uint32_t)e->xkey.same_screen); break;
    case ButtonPress: case ButtonRelease:
        ew32(rt,g+20u,(uint32_t)e->xbutton.root); ew32(rt,g+24u,(uint32_t)e->xbutton.subwindow); ew32(rt,g+28u,(uint32_t)e->xbutton.time);
        ew32(rt,g+32u,(uint32_t)e->xbutton.x); ew32(rt,g+36u,(uint32_t)e->xbutton.y); ew32(rt,g+40u,(uint32_t)e->xbutton.x_root); ew32(rt,g+44u,(uint32_t)e->xbutton.y_root);
        ew32(rt,g+48u,(uint32_t)e->xbutton.state); ew32(rt,g+52u,(uint32_t)e->xbutton.button); ew32(rt,g+56u,(uint32_t)e->xbutton.same_screen); break;
    case MotionNotify:
        ew32(rt,g+20u,(uint32_t)e->xmotion.root); ew32(rt,g+24u,(uint32_t)e->xmotion.subwindow); ew32(rt,g+28u,(uint32_t)e->xmotion.time);
        ew32(rt,g+32u,(uint32_t)e->xmotion.x); ew32(rt,g+36u,(uint32_t)e->xmotion.y); ew32(rt,g+40u,(uint32_t)e->xmotion.x_root); ew32(rt,g+44u,(uint32_t)e->xmotion.y_root);
        ew32(rt,g+48u,(uint32_t)e->xmotion.state); irix_guest_write8(rt,g+52u,(uint8_t)e->xmotion.is_hint); ew32(rt,g+56u,(uint32_t)e->xmotion.same_screen); break;
    case EnterNotify: case LeaveNotify:
        ew32(rt,g+20u,(uint32_t)e->xcrossing.root); ew32(rt,g+24u,(uint32_t)e->xcrossing.subwindow); ew32(rt,g+28u,(uint32_t)e->xcrossing.time);
        ew32(rt,g+32u,(uint32_t)e->xcrossing.x); ew32(rt,g+36u,(uint32_t)e->xcrossing.y); ew32(rt,g+40u,(uint32_t)e->xcrossing.x_root); ew32(rt,g+44u,(uint32_t)e->xcrossing.y_root);
        ew32(rt,g+48u,(uint32_t)e->xcrossing.mode); ew32(rt,g+52u,(uint32_t)e->xcrossing.detail); ew32(rt,g+56u,(uint32_t)e->xcrossing.same_screen);
        ew32(rt,g+60u,(uint32_t)e->xcrossing.focus); ew32(rt,g+64u,(uint32_t)e->xcrossing.state); break;
    case FocusIn: case FocusOut: ew32(rt,g+20u,(uint32_t)e->xfocus.mode); ew32(rt,g+24u,(uint32_t)e->xfocus.detail); break;
    case KeymapNotify: memcpy((char *)p + 20, e->xkeymap.key_vector, 32u); break;
    case Expose: ew32(rt,g+20u,e->xexpose.x); ew32(rt,g+24u,e->xexpose.y); ew32(rt,g+28u,e->xexpose.width); ew32(rt,g+32u,e->xexpose.height); ew32(rt,g+36u,e->xexpose.count); break;
    case GraphicsExpose: ew32(rt,g+20u,e->xgraphicsexpose.x); ew32(rt,g+24u,e->xgraphicsexpose.y); ew32(rt,g+28u,e->xgraphicsexpose.width); ew32(rt,g+32u,e->xgraphicsexpose.height); ew32(rt,g+36u,e->xgraphicsexpose.count); ew32(rt,g+40u,e->xgraphicsexpose.major_code); ew32(rt,g+44u,e->xgraphicsexpose.minor_code); break;
    case NoExpose: ew32(rt,g+20u,e->xnoexpose.major_code); ew32(rt,g+24u,e->xnoexpose.minor_code); break;
    case VisibilityNotify: ew32(rt,g+20u,e->xvisibility.state); break;
    case CreateNotify: ew32(rt,g+16u,(uint32_t)e->xcreatewindow.parent); ew32(rt,g+20u,(uint32_t)e->xcreatewindow.window); ew32(rt,g+24u,e->xcreatewindow.x); ew32(rt,g+28u,e->xcreatewindow.y); ew32(rt,g+32u,e->xcreatewindow.width); ew32(rt,g+36u,e->xcreatewindow.height); ew32(rt,g+40u,e->xcreatewindow.border_width); ew32(rt,g+44u,e->xcreatewindow.override_redirect); break;
    case DestroyNotify: ew32(rt,g+16u,(uint32_t)e->xdestroywindow.event); ew32(rt,g+20u,(uint32_t)e->xdestroywindow.window); break;
    case UnmapNotify: ew32(rt,g+16u,(uint32_t)e->xunmap.event); ew32(rt,g+20u,(uint32_t)e->xunmap.window); ew32(rt,g+24u,e->xunmap.from_configure); break;
    case MapNotify: ew32(rt,g+16u,(uint32_t)e->xmap.event); ew32(rt,g+20u,(uint32_t)e->xmap.window); ew32(rt,g+24u,e->xmap.override_redirect); break;
    case MapRequest: ew32(rt,g+16u,(uint32_t)e->xmaprequest.parent); ew32(rt,g+20u,(uint32_t)e->xmaprequest.window); break;
    case ReparentNotify: ew32(rt,g+16u,(uint32_t)e->xreparent.event); ew32(rt,g+20u,(uint32_t)e->xreparent.window); ew32(rt,g+24u,(uint32_t)e->xreparent.parent); ew32(rt,g+28u,e->xreparent.x); ew32(rt,g+32u,e->xreparent.y); ew32(rt,g+36u,e->xreparent.override_redirect); break;
    case ConfigureNotify: ew32(rt,g+16u,(uint32_t)e->xconfigure.event); ew32(rt,g+20u,(uint32_t)e->xconfigure.window); ew32(rt,g+24u,e->xconfigure.x); ew32(rt,g+28u,e->xconfigure.y); ew32(rt,g+32u,e->xconfigure.width); ew32(rt,g+36u,e->xconfigure.height); ew32(rt,g+40u,e->xconfigure.border_width); ew32(rt,g+44u,(uint32_t)e->xconfigure.above); ew32(rt,g+48u,e->xconfigure.override_redirect); break;
    case GravityNotify: ew32(rt,g+16u,(uint32_t)e->xgravity.event); ew32(rt,g+20u,(uint32_t)e->xgravity.window); ew32(rt,g+24u,e->xgravity.x); ew32(rt,g+28u,e->xgravity.y); break;
    case ResizeRequest: ew32(rt,g+20u,e->xresizerequest.width); ew32(rt,g+24u,e->xresizerequest.height); break;
    case ConfigureRequest: ew32(rt,g+16u,(uint32_t)e->xconfigurerequest.parent); ew32(rt,g+20u,(uint32_t)e->xconfigurerequest.window); ew32(rt,g+24u,e->xconfigurerequest.x); ew32(rt,g+28u,e->xconfigurerequest.y); ew32(rt,g+32u,e->xconfigurerequest.width); ew32(rt,g+36u,e->xconfigurerequest.height); ew32(rt,g+40u,e->xconfigurerequest.border_width); ew32(rt,g+44u,(uint32_t)e->xconfigurerequest.above); ew32(rt,g+48u,e->xconfigurerequest.detail); ew32(rt,g+52u,(uint32_t)e->xconfigurerequest.value_mask); break;
    case CirculateNotify: ew32(rt,g+16u,(uint32_t)e->xcirculate.event); ew32(rt,g+20u,(uint32_t)e->xcirculate.window); ew32(rt,g+24u,e->xcirculate.place); break;
    case CirculateRequest: ew32(rt,g+16u,(uint32_t)e->xcirculaterequest.parent); ew32(rt,g+20u,(uint32_t)e->xcirculaterequest.window); ew32(rt,g+24u,e->xcirculaterequest.place); break;
    case PropertyNotify: ew32(rt,g+20u,(uint32_t)e->xproperty.atom); ew32(rt,g+24u,(uint32_t)e->xproperty.time); ew32(rt,g+28u,e->xproperty.state); break;
    case SelectionClear: ew32(rt,g+20u,(uint32_t)e->xselectionclear.selection); ew32(rt,g+24u,(uint32_t)e->xselectionclear.time); break;
    case SelectionRequest: ew32(rt,g+16u,(uint32_t)e->xselectionrequest.owner); ew32(rt,g+20u,(uint32_t)e->xselectionrequest.requestor); ew32(rt,g+24u,(uint32_t)e->xselectionrequest.selection); ew32(rt,g+28u,(uint32_t)e->xselectionrequest.target); ew32(rt,g+32u,(uint32_t)e->xselectionrequest.property); ew32(rt,g+36u,(uint32_t)e->xselectionrequest.time); break;
    case SelectionNotify: ew32(rt,g+16u,(uint32_t)e->xselection.requestor); ew32(rt,g+20u,(uint32_t)e->xselection.selection); ew32(rt,g+24u,(uint32_t)e->xselection.target); ew32(rt,g+28u,(uint32_t)e->xselection.property); ew32(rt,g+32u,(uint32_t)e->xselection.time); break;
    case ColormapNotify: ew32(rt,g+20u,(uint32_t)e->xcolormap.colormap); ew32(rt,g+24u,(uint32_t)e->xcolormap.new); ew32(rt,g+28u,e->xcolormap.state); break;
    case ClientMessage:
        ew32(rt,g+20u,(uint32_t)e->xclient.message_type); ew32(rt,g+24u,(uint32_t)e->xclient.format);
        if(e->xclient.format==8) memcpy((char*)p+28,e->xclient.data.b,20u);
        else if(e->xclient.format==16){int i;for(i=0;i<10;++i)irix_guest_write16be(rt,g+28u+(IrixGuestAddr)i*2u,(uint16_t)e->xclient.data.s[i]);}
        else {int i;for(i=0;i<5;++i)ew32(rt,g+28u+(IrixGuestAddr)i*4u,(uint32_t)e->xclient.data.l[i]);}
        break;
    case MappingNotify: ew32(rt,g+20u,e->xmapping.request); ew32(rt,g+24u,e->xmapping.first_keycode); ew32(rt,g+28u,e->xmapping.count); break;
    default: break;
    }
    return !rt->trapped;
}

static int read_n32_event(IrixGuestRuntime *rt, IrixGuestAddr g, Display *dpy, XEvent *e)
{
    int type;
    if(!g||!e||!irix_guest_ptr(rt,g,IRIX_N32_XEVENT_SIZE))return 0;
    memset(e,0,sizeof(*e));
    type=(int32_t)er32(rt,g+0u); e->type=type;
    e->xany.serial=(unsigned long)er32(rt,g+4u); e->xany.send_event=(Bool)(int32_t)er32(rt,g+8u); e->xany.display=dpy; e->xany.window=(Window)er32(rt,g+16u);
    switch(type){
    case KeyPress: case KeyRelease: e->xkey.root=(Window)er32(rt,g+20u);e->xkey.subwindow=(Window)er32(rt,g+24u);e->xkey.time=(Time)er32(rt,g+28u);e->xkey.x=(int32_t)er32(rt,g+32u);e->xkey.y=(int32_t)er32(rt,g+36u);e->xkey.x_root=(int32_t)er32(rt,g+40u);e->xkey.y_root=(int32_t)er32(rt,g+44u);e->xkey.state=er32(rt,g+48u);e->xkey.keycode=er32(rt,g+52u);e->xkey.same_screen=(Bool)(int32_t)er32(rt,g+56u);break;
    case ButtonPress: case ButtonRelease: e->xbutton.root=(Window)er32(rt,g+20u);e->xbutton.subwindow=(Window)er32(rt,g+24u);e->xbutton.time=(Time)er32(rt,g+28u);e->xbutton.x=(int32_t)er32(rt,g+32u);e->xbutton.y=(int32_t)er32(rt,g+36u);e->xbutton.x_root=(int32_t)er32(rt,g+40u);e->xbutton.y_root=(int32_t)er32(rt,g+44u);e->xbutton.state=er32(rt,g+48u);e->xbutton.button=er32(rt,g+52u);e->xbutton.same_screen=(Bool)(int32_t)er32(rt,g+56u);break;
    case MotionNotify:e->xmotion.root=(Window)er32(rt,g+20u);e->xmotion.subwindow=(Window)er32(rt,g+24u);e->xmotion.time=(Time)er32(rt,g+28u);e->xmotion.x=(int32_t)er32(rt,g+32u);e->xmotion.y=(int32_t)er32(rt,g+36u);e->xmotion.x_root=(int32_t)er32(rt,g+40u);e->xmotion.y_root=(int32_t)er32(rt,g+44u);e->xmotion.state=er32(rt,g+48u);e->xmotion.is_hint=(char)irix_guest_read8(rt,g+52u);e->xmotion.same_screen=(Bool)(int32_t)er32(rt,g+56u);break;
    case ConfigureNotify:
        /* ConfigureNotify has both event and window before its geometry. */
        e->xconfigure.event=(Window)er32(rt,g+16u);
        e->xconfigure.window=(Window)er32(rt,g+20u);
        e->xconfigure.x=(int32_t)er32(rt,g+24u);
        e->xconfigure.y=(int32_t)er32(rt,g+28u);
        e->xconfigure.width=(int32_t)er32(rt,g+32u);
        e->xconfigure.height=(int32_t)er32(rt,g+36u);
        e->xconfigure.border_width=(int32_t)er32(rt,g+40u);
        e->xconfigure.above=(Window)er32(rt,g+44u);
        e->xconfigure.override_redirect=(Bool)(int32_t)er32(rt,g+48u);
        break;
    case ClientMessage:{int i;e->xclient.message_type=(Atom)er32(rt,g+20u);e->xclient.format=(int32_t)er32(rt,g+24u);if(e->xclient.format==8){void*p=irix_guest_ptr(rt,g+28u,20u);if(p)memcpy(e->xclient.data.b,p,20u);}else if(e->xclient.format==16)for(i=0;i<10;++i)e->xclient.data.s[i]=(short)irix_guest_read16be(rt,g+28u+(IrixGuestAddr)i*2u);else for(i=0;i<5;++i)e->xclient.data.l[i]=(long)(int32_t)er32(rt,g+28u+(IrixGuestAddr)i*4u);break;}
    case SelectionNotify:e->xselection.requestor=(Window)er32(rt,g+16u);e->xselection.selection=(Atom)er32(rt,g+20u);e->xselection.target=(Atom)er32(rt,g+24u);e->xselection.property=(Atom)er32(rt,g+28u);e->xselection.time=(Time)er32(rt,g+32u);break;
    case SelectionRequest:e->xselectionrequest.owner=(Window)er32(rt,g+16u);e->xselectionrequest.requestor=(Window)er32(rt,g+20u);e->xselectionrequest.selection=(Atom)er32(rt,g+24u);e->xselectionrequest.target=(Atom)er32(rt,g+28u);e->xselectionrequest.property=(Atom)er32(rt,g+32u);e->xselectionrequest.time=(Time)er32(rt,g+36u);break;
    case PropertyNotify:e->xproperty.atom=(Atom)er32(rt,g+20u);e->xproperty.time=(Time)er32(rt,g+24u);e->xproperty.state=(int32_t)er32(rt,g+28u);break;
    case MappingNotify:e->xmapping.request=(int32_t)er32(rt,g+20u);e->xmapping.first_keycode=(int32_t)er32(rt,g+24u);e->xmapping.count=(int32_t)er32(rt,g+28u);break;
    default: break;
    }
    return !rt->trapped;
}

static void bridge_XPutImage(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);Drawable dr=(Drawable)(uint32_t)cpu->gpr[5];GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[6]);XImage*im=sync_image_from_guest(rt,(uint32_t)cpu->gpr[7]);int sx=(int32_t)cpu->gpr[8],sy=(int32_t)cpu->gpr[9],dx=(int32_t)cpu->gpr[10],dy=(int32_t)cpu->gpr[11];unsigned w=n32_stack_arg32(rt,cpu,0u),h=n32_stack_arg32(rt,cpu,1u);cpu->gpr[2]=(d&&gc&&im&&!rt->trapped)?(uint32_t)XPutImage(d,dr,gc,im,sx,sy,dx,dy,w,h):0;}
static void bridge_XCreateBitmapFromData(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);unsigned w=(uint32_t)cpu->gpr[7],h=(uint32_t)cpu->gpr[8];uint64_t bytes=(((uint64_t)w+7u)>>3)*(uint64_t)h;const char*p=(bytes<=SIZE_MAX)?(const char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[6],(size_t)bytes):0;cpu->gpr[2]=(d&&p&&!rt->trapped)?(uint32_t)XCreateBitmapFromData(d,(Drawable)(uint32_t)cpu->gpr[5],p,w,h):0;}

static void bridge_XReadBitmapFile(IrixGuestRuntime *rt,IrixGuestCpu *cpu)
{
    Display *display = irix_x11_primary_display_from_guest(
        (uint32_t)cpu->gpr[4]);
    const char *guest_path = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    char host_path[4096];
    const char *path = guest_path ? irix_guest_resolve_path(
        rt, guest_path, host_path, sizeof(host_path),
        "resource:XReadBitmapFile", 1) : 0;
    unsigned width = 0, height = 0;
    Pixmap bitmap = None;
    int x_hot = -1, y_hot = -1;
    int status = BitmapOpenFailed;

    if (display && path)
        status = XReadBitmapFile(display, (Drawable)(uint32_t)cpu->gpr[5],
                                 path, &width, &height, &bitmap,
                                 &x_hot, &y_hot);
    if (status == BitmapSuccess) {
        IrixGuestAddr out = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
        if (out) irix_guest_write32be(rt, out, width);
        out = (IrixGuestAddr)(uint32_t)cpu->gpr[8];
        if (out) irix_guest_write32be(rt, out, height);
        out = (IrixGuestAddr)(uint32_t)cpu->gpr[9];
        if (out) irix_guest_write32be(rt, out, (uint32_t)bitmap);
        out = (IrixGuestAddr)(uint32_t)cpu->gpr[10];
        if (out) irix_guest_write32be(rt, out, (uint32_t)x_hot);
        out = (IrixGuestAddr)(uint32_t)cpu->gpr[11];
        if (out) irix_guest_write32be(rt, out, (uint32_t)y_hot);
    }
    cpu->gpr[2] = (uint32_t)status;
    
}

static void bridge_XGetImage(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XImage*im=d?XGetImage(d,(Drawable)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6],(int)cpu->gpr[7],(unsigned)cpu->gpr[8],(unsigned)cpu->gpr[9],(unsigned long)(uint32_t)cpu->gpr[10],(int)cpu->gpr[11]):0;IrixGuestAddr gd=0,gi=0;if(im){uint64_t n=image_data_size(im);if(n&&n<=UINT32_MAX){gd=tmp_alloc((uint32_t)n);if(gd&&im->data){void*dst=irix_guest_ptr(rt,gd,(size_t)n);if(dst)memcpy(dst,im->data,(size_t)n);}}if(im->data)XFree(im->data);im->data=gd?(char*)irix_guest_ptr(rt,gd,1u):0;gi=register_image(rt,im,gd);if(!gi){im->data=0;XDestroyImage(im);}}cpu->gpr[2]=gi;}
static void bridge_XQueryFont(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XFontStruct*f=d?XQueryFont(d,(XID)(uint32_t)cpu->gpr[5]):0;cpu->gpr[2]=irix_x11_primary_font_to_guest(rt,f);}
static void bridge_XFreeFont(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);IrixGuestAddr g=(uint32_t)cpu->gpr[5];XFontStruct*f=irix_x11_primary_font_from_guest(g);int rc=(d&&f)?XFreeFont(d,f):0;if(f)irix_x11_primary_forget_font(g);cpu->gpr[2]=(uint32_t)rc;(void)rt;}
static void bridge_XCreatePixmapCursor(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XColor fg,bg;IrixGuestAddr fga=(uint32_t)cpu->gpr[7],bga=(uint32_t)cpu->gpr[8];memset(&fg,0,sizeof(fg));memset(&bg,0,sizeof(bg));if(fga){fg.pixel=er32(rt,fga);fg.red=irix_guest_read16be(rt,fga+4u);fg.green=irix_guest_read16be(rt,fga+6u);fg.blue=irix_guest_read16be(rt,fga+8u);fg.flags=(char)irix_guest_read8(rt,fga+10u);}if(bga){bg.pixel=er32(rt,bga);bg.red=irix_guest_read16be(rt,bga+4u);bg.green=irix_guest_read16be(rt,bga+6u);bg.blue=irix_guest_read16be(rt,bga+8u);bg.flags=(char)irix_guest_read8(rt,bga+10u);}cpu->gpr[2]=(d&&!rt->trapped)?(uint32_t)XCreatePixmapCursor(d,(Pixmap)(uint32_t)cpu->gpr[5],(Pixmap)(uint32_t)cpu->gpr[6],&fg,&bg,(unsigned)cpu->gpr[9],(unsigned)cpu->gpr[10]):0;}
static void bridge_XDefaultScreenOfDisplay(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);cpu->gpr[2]=(uint64_t)(int64_t)(d?DefaultScreen(d):0);(void)rt;}

static void bridge_XAllocColorCells(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);unsigned np=(unsigned)cpu->gpr[8],nx=(unsigned)cpu->gpr[10],i;unsigned long*planes=np?calloc(np,sizeof(*planes)):0;unsigned long*pixels=nx?calloc(nx,sizeof(*pixels)):0;Status ok=(d&&(!np||planes)&&(!nx||pixels))?XAllocColorCells(d,(Colormap)(uint32_t)cpu->gpr[5],(Bool)cpu->gpr[6],planes,np,pixels,nx):0;if(ok){for(i=0;i<np;++i)ew32(rt,(uint32_t)cpu->gpr[7]+i*4u,(uint32_t)planes[i]);for(i=0;i<nx;++i)ew32(rt,(uint32_t)cpu->gpr[9]+i*4u,(uint32_t)pixels[i]);}free(planes);free(pixels);cpu->gpr[2]=(uint32_t)ok;}

static void bridge_XNextEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4],ge=(uint32_t)cpu->gpr[5];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;int rc=d?XNextEvent(d,&e):0;if(d)write_n32_event(rt,ge,gd,&e);cpu->gpr[2]=(uint32_t)rc;}
static void bridge_XPeekEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4],ge=(uint32_t)cpu->gpr[5];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;int rc=d?XPeekEvent(d,&e):0;if(d)write_n32_event(rt,ge,gd,&e);cpu->gpr[2]=(uint32_t)rc;}
static void bridge_XPutBackEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XEvent e;cpu->gpr[2]=(d&&read_n32_event(rt,(uint32_t)cpu->gpr[5],d,&e))?(uint32_t)XPutBackEvent(d,&e):0;}
static void bridge_XWindowEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;int rc=d?XWindowEvent(d,(Window)(uint32_t)cpu->gpr[5],(long)(uint32_t)cpu->gpr[6],&e):0;if(d)write_n32_event(rt,(uint32_t)cpu->gpr[7],gd,&e);cpu->gpr[2]=(uint32_t)rc;}
static void bridge_XCheckWindowEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;Bool ok=d?XCheckWindowEvent(d,(Window)(uint32_t)cpu->gpr[5],(long)(uint32_t)cpu->gpr[6],&e):False;if(ok)write_n32_event(rt,(uint32_t)cpu->gpr[7],gd,&e);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_XCheckTypedWindowEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;Bool ok=d?XCheckTypedWindowEvent(d,(Window)(uint32_t)cpu->gpr[5],(int)cpu->gpr[6],&e):False;if(ok)write_n32_event(rt,(uint32_t)cpu->gpr[7],gd,&e);cpu->gpr[2]=(uint32_t)ok;}
static void bridge_XCheckMaskEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr gd=(uint32_t)cpu->gpr[4];Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;Bool ok=d?XCheckMaskEvent(d,(long)(uint32_t)cpu->gpr[5],&e):False;if(ok)write_n32_event(rt,(uint32_t)cpu->gpr[6],gd,&e);cpu->gpr[2]=(uint32_t)ok;}

static int guest_predicate(IrixGuestRuntime *rt,const IrixGuestCpu *caller,IrixGuestAddr pred,IrixGuestAddr gd,const XEvent*e,IrixGuestAddr arg){IrixGuestCpu c;IrixGuestAddr saved;if(!pred)return 0;if(!g_predicate_event)g_predicate_event=tmp_alloc(IRIX_N32_XEVENT_SIZE);if(!g_predicate_event||!write_n32_event(rt,g_predicate_event,gd,e))return 0;c=*caller;c.gpr[4]=gd;c.gpr[5]=g_predicate_event;c.gpr[6]=arg;saved=rt->current_pc;if(irix_guest_call(rt,&c,pred)!=0){rt->current_pc=saved;return 0;}rt->current_pc=saved;return c.gpr[2]?1:0;}
extern int _XEventsQueued(Display *dpy, int mode);
static int scan_if_event(IrixGuestRuntime *rt,IrixGuestCpu *cpu,int block){IrixGuestAddr gd=(uint32_t)cpu->gpr[4],out=(uint32_t)cpu->gpr[5],pred=(uint32_t)cpu->gpr[6],arg=(uint32_t)cpu->gpr[7];Display*d=irix_x11_primary_display_from_guest(gd);XEvent*held=0,e;size_t n=0,cap=0,i;int limit,ok=0;if(!d||!pred)return 0;if(!block){XLockDisplay(d);(void)_XEventsQueued(d,QueuedAfterReading);XUnlockDisplay(d);}limit=block?-1:XPending(d);while(block||limit-->0){if(XNextEvent(d,&e)!=0&&rt->trapped)break;if(guest_predicate(rt,cpu,pred,gd,&e,arg)){ok=1;write_n32_event(rt,out,gd,&e);break;}if(n==cap){size_t nc=cap?cap*2u:16u;XEvent*p=(XEvent*)realloc(held,nc*sizeof(*p));if(!p)break;held=p;cap=nc;}held[n++]=e;if(rt->trapped)break;}for(i=n;i>0;--i)XPutBackEvent(d,&held[i-1u]);free(held);return ok;}
static void bridge_XCheckIfEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){cpu->gpr[2]=(uint32_t)scan_if_event(rt,cpu,0);}
static void bridge_XIfEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){cpu->gpr[2]=(uint32_t)scan_if_event(rt,cpu,1);}

static void bridge_XSendEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);XEvent e;cpu->gpr[2]=(d&&read_n32_event(rt,(uint32_t)cpu->gpr[8],d,&e))?(uint32_t)XSendEvent(d,(Window)(uint32_t)cpu->gpr[5],(Bool)cpu->gpr[6],(long)(uint32_t)cpu->gpr[7],&e):0;}
static void bridge_XFilterEvent(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr ge=(uint32_t)cpu->gpr[4];IrixGuestAddr gd=ge?er32(rt,ge+12u):0;Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;Bool ok=(d&&read_n32_event(rt,ge,d,&e))?XFilterEvent(&e,(Window)(uint32_t)cpu->gpr[5]):False;cpu->gpr[2]=(uint32_t)ok;}

static void bridge_XLookupString(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr ge=(uint32_t)cpu->gpr[4],gd=ge?er32(rt,ge+12u):0;Display*d=irix_x11_primary_display_from_guest(gd);XEvent ev;char*buf=(char*)irix_guest_ptr(rt,(uint32_t)cpu->gpr[5],(size_t)(uint32_t)cpu->gpr[6]);KeySym ks=0;XComposeStatus cs,*csp=0;IrixGuestAddr csa=(uint32_t)cpu->gpr[8];memset(&cs,0,sizeof(cs));if(csa){cs.chars_matched=(int32_t)er32(rt,csa+4u);csp=&cs;}int n=(d&&buf&&read_n32_event(rt,ge,d,&ev))?XLookupString(&ev.xkey,buf,(int)cpu->gpr[6],&ks,csp):0;if((uint32_t)cpu->gpr[7])ew32(rt,(uint32_t)cpu->gpr[7],(uint32_t)ks);if(csa){ew32(rt,csa+0u,0u);ew32(rt,csa+4u,(uint32_t)cs.chars_matched);}cpu->gpr[2]=(uint64_t)(int64_t)n;}
static void bridge_XRefreshKeyboardMapping(IrixGuestRuntime *rt,IrixGuestCpu *cpu){IrixGuestAddr ge=(uint32_t)cpu->gpr[4],gd=ge?er32(rt,ge+12u):0;Display*d=irix_x11_primary_display_from_guest(gd);XEvent e;cpu->gpr[2]=(d&&read_n32_event(rt,ge,d,&e))?(uint32_t)XRefreshKeyboardMapping(&e.xmapping):0;}

static void bridge_XSetCommand(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);IrixGuestAddr vec=(uint32_t)cpu->gpr[6];int n=(int32_t)cpu->gpr[7],i;char**argv=n>=0?(char**)calloc((size_t)n+1u,sizeof(*argv)):0;if(argv)for(i=0;i<n;++i){IrixGuestAddr a=er32(rt,vec+(IrixGuestAddr)i*4u);argv[i]=(char*)guest_cstr(rt,a);}cpu->gpr[2]=(d&&argv)?(uint32_t)XSetCommand(d,(Window)(uint32_t)cpu->gpr[5],argv,n):0;free(argv);}

static wchar_t *read_guest_wchars(IrixGuestRuntime *rt,IrixGuestAddr a,int n){wchar_t*w;int i;if(n<0||(!a&&n))return 0;w=(wchar_t*)calloc((size_t)n+1u,sizeof(*w));if(!w)return 0;for(i=0;i<n;++i)w[i]=(wchar_t)(int32_t)er32(rt,a+(IrixGuestAddr)i*4u);return w;}
static void bridge_XwcDrawString(IrixGuestRuntime *rt,IrixGuestCpu *cpu){Display*d=irix_x11_window_font_display_from_guest(rt,(uint32_t)cpu->gpr[4]);XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[6]);GC gc=irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[7]);int n=(int32_t)cpu->gpr[11];wchar_t*w=read_guest_wchars(rt,(uint32_t)cpu->gpr[10],n);(void)irix_x11_window_sync_guest_display(rt,cpu,(uint32_t)cpu->gpr[4]);if(d&&fs&&gc&&w)XwcDrawString(d,(Drawable)(uint32_t)cpu->gpr[5],fs,gc,(int)cpu->gpr[8],(int)cpu->gpr[9],w,n);if(d)XSync(d,False);free(w);cpu->gpr[2]=0;}
static void bridge_XwcTextEscapement(IrixGuestRuntime *rt,IrixGuestCpu *cpu){XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[4]);int n=(int32_t)cpu->gpr[6];wchar_t*w=read_guest_wchars(rt,(uint32_t)cpu->gpr[5],n);int rc=(fs&&w)?XwcTextEscapement(fs,w,n):0;free(w);cpu->gpr[2]=(uint64_t)(int64_t)rc;}

/* XwcTextExtents takes an XFontSet, wchar_t string and count.  SGI libX11's
 * implementation dereferences the font set's guest-ABI-private methods, so
 * translate the arguments and execute the equivalent operation with host
 * Xlib, just as the multibyte bridge does for XmbTextExtents. */
static void bridge_XwcTextExtents(IrixGuestRuntime *rt,IrixGuestCpu *cpu){
    XFontSet fs=irix_x11_window_fontset_from_guest((uint32_t)cpu->gpr[4]);
    int n=(int32_t)cpu->gpr[6];
    wchar_t*w=read_guest_wchars(rt,(uint32_t)cpu->gpr[5],n);
    XRectangle ink,logical;
    int rc=0;
    memset(&ink,0,sizeof(ink)); memset(&logical,0,sizeof(logical));
    if(fs&&w) rc=XwcTextExtents(fs,w,n,&ink,&logical);
    if(cpu->gpr[7]){IrixGuestAddr a=(uint32_t)cpu->gpr[7];ew32(rt,a,(uint32_t)(int32_t)ink.x);ew32(rt,a+4u,(uint32_t)(int32_t)ink.y);ew32(rt,a+8u,(uint32_t)ink.width|((uint32_t)ink.height<<16));}
    if(cpu->gpr[8]){IrixGuestAddr a=(uint32_t)cpu->gpr[8];ew32(rt,a,(uint32_t)(int32_t)logical.x);ew32(rt,a+4u,(uint32_t)(int32_t)logical.y);ew32(rt,a+8u,(uint32_t)logical.width|((uint32_t)logical.height<<16));}
    free(w); cpu->gpr[2]=(uint32_t)rc;
}

IrixGuestFunction irix_x11_image_bridge_lookup(const char *name)
{
    if(!name)return irix_x11_image_bridge_lookup_base(name);
#define MAP(n) if(!strcmp(name,#n))return bridge_##n
    MAP(XPutImage); MAP(XCreateBitmapFromData); MAP(XReadBitmapFile); MAP(XGetImage); MAP(XQueryFont); MAP(XFreeFont); MAP(XCreatePixmapCursor); MAP(XDefaultScreenOfDisplay); MAP(XAllocColorCells);
    MAP(XNextEvent); MAP(XPeekEvent); MAP(XPutBackEvent); MAP(XWindowEvent); MAP(XCheckWindowEvent); MAP(XCheckTypedWindowEvent); MAP(XCheckMaskEvent); MAP(XCheckIfEvent); MAP(XIfEvent); MAP(XSendEvent); MAP(XFilterEvent); MAP(XLookupString); MAP(XRefreshKeyboardMapping);
    MAP(XSetCommand); MAP(XwcDrawString); MAP(XwcTextEscapement); MAP(XwcTextExtents);
#undef MAP
    return irix_x11_image_bridge_lookup_base(name);
}
