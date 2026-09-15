/*
 * SgZoomBar reconstruction.
 *
 * Recovered from the original IRIX N32 libSgm.so.1 ZoomBar.c unit.
 * The original widget is a single XmPrimitive. sectionWidths contains
 * cumulative X boundaries and sectionCount is the number of boundaries,
 * yielding sectionCount + 1 rendered spans.
 *
 * The five-state shader matrix and rail geometry below are recovered directly
 * from the original N32 code. Shader GCs are supplied by SgiShader.c, which
 * reconstructs the normal-depth _sgFindShader/_sgFindGC behavior from IRIX
 * libXm.so.1 instead of depending on host Motif private symbols.
 */

#include <Sgm/Finder.h>
#include "SgiShader.h"

#include <Xm/PrimitiveP.h>
#include <Xm/Xm.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <string.h>

#ifndef XmInheritBorderHighlight
#define XmInheritBorderHighlight ((XtWidgetProc)_XtInherit)
#endif
#ifndef XmInheritBorderUnhighlight
#define XmInheritBorderUnhighlight ((XtWidgetProc)_XtInherit)
#endif

/* Original callback payload: reason, event, section. */
typedef struct
{
    int reason;
    XEvent *event;
    int section;
} SgZoomBarCallbackStruct;

typedef struct
{
    XtCallbackList activate_callback;
    XtCallbackList arm_callback;
    XtCallbackList disarm_callback;
    Boolean armed;
    XtPointer section_widths;
    int section_count;
    int armed_section;
    int pointer_section;
} SgZoomBarPartCompat;

typedef struct _SgZoomBarRec
{
    CorePart core;
    XmPrimitivePart primitive;
    SgZoomBarPartCompat zoom_bar;
} SgZoomBarRecCompat;

typedef struct
{
    XtPointer extension;
} SgZoomBarClassPartCompat;

typedef struct _SgZoomBarClassRec
{
    CoreClassPart core_class;
    XmPrimitiveClassPart primitive_class;
    SgZoomBarClassPartCompat zoom_bar_class;
} SgZoomBarClassRecCompat;

static void zoomBarInitialize(Widget, Widget, ArgList, Cardinal *);
static void zoomBarResize(Widget);
static void zoomBarRedisplay(Widget, XEvent *, Region);
static Boolean zoomBarSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult zoomBarQueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);

static void Arm(Widget, XEvent *, String *, Cardinal *);
static void Activate(Widget, XEvent *, String *, Cardinal *);
static void Disarm(Widget, XEvent *, String *, Cardinal *);
static void Enter(Widget, XEvent *, String *, Cardinal *);
static void Leave(Widget, XEvent *, String *, Cardinal *);
static void Motion(Widget, XEvent *, String *, Cardinal *);

static int zoomBarHitSection(SgZoomBarRecCompat *, Position);
static Boolean zoomBarSectionGeometry(SgZoomBarRecCompat *, int, XRectangle *);
static void zoomBarDrawSection(SgZoomBarRecCompat *, int, int);
static void zoomBarDrawAll(SgZoomBarRecCompat *);
static Position eventX(const XEvent *);
static Boolean eventInside(const SgZoomBarRecCompat *, const XEvent *);
static void callZoomCallback(Widget, XtCallbackList, int, XEvent *, int);

/*
 * State table at original N32 address 0x0b250488.
 * Entries 8 and 9 exist in the original table but the recovered renderer
 * does not reference them.
 */
static const unsigned char zoomBarShaderTable[5][10] =
{
    { 1, 2, 3, 4, 6, 6, 4, 4, 4, 0 },
    { 1, 4, 4, 4, 4, 4, 4, 4, 4, 0 },
    { 1, 2, 3, 6, 6, 6, 6, 6, 6, 0 },
    { 1, 6, 2, 3, 3, 6, 6, 6, 6, 0 },
    { 1, 6, 2, 3, 3, 6, 6, 4, 4, 0 }
};

static XtResource zoomBarResources[] =
{
    {
        XmNactivateCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.activate_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        XmNarmCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.arm_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        XmNdisarmCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.disarm_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        SgNsectionWidths, "SectionWidths", XtRPointer,
        sizeof(XtPointer),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.section_widths),
        XtRImmediate, (XtPointer)NULL
    },
    {
        SgNsectionCount, "SectionCount", XtRInt,
        sizeof(int),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.section_count),
        XtRImmediate, (XtPointer)0
    }
};

static XtActionsRec zoomBarActions[] =
{
    { "Arm", Arm },
    { "Activate", Activate },
    { "Disarm", Disarm },
    { "Enter", Enter },
    { "Leave", Leave },
    { "Motion", Motion }
};

/* Exported by the original library as a 108-byte data object. */
char _SgZoomBar_defaultTranslations[108] =
    "<Btn1Down>:Arm()\n"
    "<Btn1Up>:Activate() Disarm()\n"
    "<EnterWindow>:Enter()\n"
    "<LeaveWindow>:Leave()\n"
    "<Motion>:Motion()";

SgZoomBarClassRecCompat sgZoomBarClassRec =
{
    {
        (WidgetClass)&xmPrimitiveClassRec,
        "SgZoomBar",
        sizeof(SgZoomBarRecCompat),
        NULL,
        NULL,
        False,
        zoomBarInitialize,
        NULL,
        XtInheritRealize,
        zoomBarActions,
        XtNumber(zoomBarActions),
        zoomBarResources,
        XtNumber(zoomBarResources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        NULL,
        zoomBarResize,
        zoomBarRedisplay,
        zoomBarSetValues,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        _SgZoomBar_defaultTranslations,
        zoomBarQueryGeometry,
        NULL,
        NULL
    },
    {
        XmInheritBorderHighlight,
        XmInheritBorderUnhighlight,
        XtInheritTranslations,
        NULL,
        NULL,
        0,
        NULL
    },
    {
        NULL
    }
};

WidgetClass sgZoomBarWidgetClass = (WidgetClass)&sgZoomBarClassRec;

static void zoomBarInitialize(Widget request, Widget new_widget,
                              ArgList args, Cardinal *num_args)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)new_widget;
    (void)request;
    (void)args;
    (void)num_args;

    rec->zoom_bar.armed = False;
    rec->zoom_bar.armed_section = -1;
    rec->zoom_bar.pointer_section = -1;

    if (rec->core.width == 0)
        rec->core.width = 1;
    if (rec->core.height == 0)
        rec->core.height = 6;
}

static void zoomBarResize(Widget widget)
{
    if (XtIsRealized(widget))
        zoomBarDrawAll((SgZoomBarRecCompat *)widget);
}

static void zoomBarRedisplay(Widget widget, XEvent *event, Region region)
{
    (void)event;
    (void)region;
    if (XtIsRealized(widget))
        zoomBarDrawAll((SgZoomBarRecCompat *)widget);
}

/* Original SetValues resets all interaction state and returns True. */
static Boolean zoomBarSetValues(Widget old_widget,
                                Widget request_widget,
                                Widget new_widget,
                                ArgList args,
                                Cardinal *num_args)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)new_widget;
    (void)old_widget;
    (void)request_widget;
    (void)args;
    (void)num_args;

    rec->zoom_bar.armed = False;
    rec->zoom_bar.armed_section = -1;
    rec->zoom_bar.pointer_section = -1;
    return True;
}

static XtGeometryResult zoomBarQueryGeometry(Widget widget,
                                              XtWidgetGeometry *intended,
                                              XtWidgetGeometry *preferred)
{
    (void)widget;
    preferred->request_mode = CWHeight;
    preferred->height = 6;

    if (intended && (intended->request_mode & CWHeight) &&
        intended->height == preferred->height)
        return XtGeometryYes;
    return XtGeometryAlmost;
}

/* Horizontal geometry helper recovered at 0x0b1dcb30. */
static Boolean zoomBarSectionGeometry(SgZoomBarRecCompat *rec,
                                      int section,
                                      XRectangle *rect)
{
    unsigned short *bounds;
    int x, width;

    if (!rec || !rect || section < 0)
        return False;

    bounds = (unsigned short *)rec->zoom_bar.section_widths;
    if (rec->zoom_bar.section_count <= 0 || !bounds)
    {
        if (section != 0)
            return False;
        x = 0;
        width = rec->core.width > 1 ? (int)rec->core.width - 1 : 1;
    }
    else if (section == 0)
    {
        x = 0;
        width = (int)bounds[0];
    }
    else if (section < rec->zoom_bar.section_count)
    {
        x = (int)bounds[section - 1] + 1;
        width = (int)bounds[section] - x;
    }
    else if (section == rec->zoom_bar.section_count)
    {
        x = (int)bounds[section - 1] + 1;
        width = (int)rec->core.width - x - 2;
    }
    else
        return False;

    if (x < 0)
        x = 0;
    if (width < 1)
        width = 1;

    rect->x = (short)x;
    rect->y = 0;
    rect->width = (unsigned short)width;
    rect->height = rec->core.height > 1 ?
                   (unsigned short)(rec->core.height - 1) : 1;
    return True;
}

/* Exact hit-test recovered at 0x0b1dcd7c. */
static int zoomBarHitSection(SgZoomBarRecCompat *rec, Position x)
{
    unsigned short *bounds;
    int i;

    if (!rec || rec->zoom_bar.section_count <= 0 ||
        !rec->zoom_bar.section_widths)
        return -1;

    bounds = (unsigned short *)rec->zoom_bar.section_widths;
    if (x < (Position)bounds[0])
        return 0;
    if (x >= (Position)bounds[rec->zoom_bar.section_count - 1])
        return rec->zoom_bar.section_count;

    for (i = 1; i < rec->zoom_bar.section_count; ++i)
        if (x <= (Position)bounds[i])
            return i;
    return -1;
}

/*
 * Section renderer recovered from 0x0b1dc450. The original requires at least
 * five pixels, resolves eight SGI shader slots, draws three nested top/left
 * rails, fills the center, then draws the bottom/right rails.
 */
static void zoomBarDrawSection(SgZoomBarRecCompat *rec, int section, int state)
{
    XRectangle r;
    Display *dpy;
    Window win;
    GC g0, g1, g2, g3, g4, g5, g6, g7;
    int x, y, w, h;

    if (!rec || !XtIsRealized((Widget)rec) ||
        state < 0 || state > 4 ||
        !zoomBarSectionGeometry(rec, section, &r))
        return;

    x = r.x;
    y = r.y;
    w = r.width;
    h = r.height;
    if (w < 5 || h < 5)
        return;

    dpy = XtDisplay((Widget)rec);
    win = XtWindow((Widget)rec);

    g0 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][0]);
    g1 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][1]);
    g2 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][2]);
    g3 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][3]);
    g4 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][4]);
    g5 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][5]);
    g6 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][6]);
    g7 = SgShaderGetGC((Widget)rec, rec->core.background_pixel,
                       zoomBarShaderTable[state][7]);

    if (!g0 || !g1 || !g2 || !g3 || !g4 || !g5 || !g6 || !g7)
        return;

    XDrawLine(dpy, win, g4, x,     y,     x + w,     y);
    XDrawLine(dpy, win, g4, x,     y + 1, x,         y + h);

    XDrawLine(dpy, win, g5, x + 1, y + 1, x + w - 1, y + 1);
    XDrawLine(dpy, win, g5, x + 1, y + 2, x + 1,     y + h - 1);

    XDrawLine(dpy, win, g6, x + 2, y + 2, x + w - 2, y + 2);
    XDrawLine(dpy, win, g6, x + 2, y + 3, x + 2,     y + h - 2);

    XFillRectangle(dpy, win, g7,
                   x + 3, y + 3,
                   (unsigned int)(w - 5), (unsigned int)(h - 5));

    XDrawLine(dpy, win, g3, x + 3,     y + h - 2,
                            x + w - 2, y + h - 2);
    XDrawLine(dpy, win, g3, x + w - 2, y + 3,
                            x + w - 2, y + h - 3);

    XDrawLine(dpy, win, g2, x + 2,     y + h - 1,
                            x + w - 1, y + h - 1);
    XDrawLine(dpy, win, g2, x + w - 1, y + 2,
                            x + w - 1, y + h - 2);

    XDrawLine(dpy, win, g1, x + 1, y + h, x + w, y + h);
    XDrawLine(dpy, win, g1, x + w, y + 1, x + w, y + 2);

    XDrawLine(dpy, win, g0, x + w + 1, y, x + w + 1, y + h);
}

static void zoomBarDrawAll(SgZoomBarRecCompat *rec)
{
    int i;
    int last;

    if (!rec || !XtIsRealized((Widget)rec))
        return;

    last = rec->zoom_bar.section_count > 0 ? rec->zoom_bar.section_count : 0;
    for (i = 0; i <= last; ++i)
    {
        int state = 0;
        if (i == rec->zoom_bar.pointer_section)
            state = rec->zoom_bar.armed ? 4 : 2;
        zoomBarDrawSection(rec, i, state);
    }
}

static Position eventX(const XEvent *event)
{
    if (!event)
        return -1;
    switch (event->type)
    {
        case ButtonPress:
        case ButtonRelease: return (Position)event->xbutton.x;
        case MotionNotify:  return (Position)event->xmotion.x;
        case EnterNotify:
        case LeaveNotify:   return (Position)event->xcrossing.x;
        default:            return -1;
    }
}

static Boolean eventInside(const SgZoomBarRecCompat *rec, const XEvent *event)
{
    int x, y;
    int bw;

    if (!rec || !event)
        return False;

    switch (event->type)
    {
        case ButtonPress:
        case ButtonRelease:
            x = event->xbutton.x;
            y = event->xbutton.y;
            break;
        case MotionNotify:
            x = event->xmotion.x;
            y = event->xmotion.y;
            break;
        case EnterNotify:
        case LeaveNotify:
            x = event->xcrossing.x;
            y = event->xcrossing.y;
            break;
        default:
            return False;
    }

    bw = (int)rec->core.border_width;
    return x >= -bw && y >= -bw &&
           x < (int)rec->core.width + bw &&
           y < (int)rec->core.height + bw;
}

static void callZoomCallback(Widget widget, XtCallbackList list,
                             int reason, XEvent *event, int section)
{
    SgZoomBarCallbackStruct cb;
    if (!list)
        return;
    cb.reason = reason;
    cb.event = event;
    cb.section = section;
    XtCallCallbackList(widget, list, &cb);
}

static void Arm(Widget widget, XEvent *event,
                String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int section;
    (void)params;
    (void)num_params;

    rec->zoom_bar.armed = True;
    section = zoomBarHitSection(rec, eventX(event));
    rec->zoom_bar.armed_section = section;
    rec->zoom_bar.pointer_section = section;
    zoomBarDrawAll(rec);
    callZoomCallback(widget, rec->zoom_bar.arm_callback,
                     XmCR_ARM, event, section);
}

static void Activate(Widget widget, XEvent *event,
                     String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int section;
    (void)params;
    (void)num_params;

    if (!rec->zoom_bar.armed)
        return;

    section = rec->zoom_bar.armed_section;
    rec->zoom_bar.armed_section = -1;
    rec->zoom_bar.armed = False;
    zoomBarDrawAll(rec);

    if (eventInside(rec, event))
        callZoomCallback(widget, rec->zoom_bar.activate_callback,
                         XmCR_ACTIVATE, event, section);
}

static void Disarm(Widget widget, XEvent *event,
                   String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    (void)params;
    (void)num_params;

    callZoomCallback(widget, rec->zoom_bar.disarm_callback,
                     XmCR_DISARM, event, rec->zoom_bar.armed_section);
}

static void Enter(Widget widget, XEvent *event,
                  String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int old_section = rec->zoom_bar.pointer_section;
    int new_section = zoomBarHitSection(rec, eventX(event));
    (void)params;
    (void)num_params;

    rec->zoom_bar.pointer_section = new_section;
    if (old_section != new_section)
    {
        if (old_section >= 0)
            zoomBarDrawSection(rec, old_section, 0);
        if (new_section >= 0)
            zoomBarDrawSection(rec, new_section,
                               rec->zoom_bar.armed ? 4 : 2);
    }
}

static void Leave(Widget widget, XEvent *event,
                  String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    (void)event;
    (void)params;
    (void)num_params;

    rec->zoom_bar.armed = False;
    rec->zoom_bar.armed_section = -1;
    rec->zoom_bar.pointer_section = -1;
    zoomBarDrawAll(rec);
}

static void Motion(Widget widget, XEvent *event,
                   String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int old_section = rec->zoom_bar.pointer_section;
    int new_section = zoomBarHitSection(rec, eventX(event));
    (void)params;
    (void)num_params;

    if (new_section == old_section)
        return;

    rec->zoom_bar.pointer_section = new_section;
    if (old_section >= 0)
        zoomBarDrawSection(rec, old_section, 0);
    if (new_section >= 0)
        zoomBarDrawSection(rec, new_section,
                           rec->zoom_bar.armed ? 4 : 2);
}

void _SgZoomBarGetSectionInfo(Widget widget, Position x,
                              int *section_return,
                              XRectangle *rect_return)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int section = zoomBarHitSection(rec, x);

    if (section_return)
        *section_return = section;

    if (!rect_return)
        return;
    if (section < 0 || !zoomBarSectionGeometry(rec, section, rect_return))
        memset(rect_return, 0, sizeof(*rect_return));
}

Widget SgCreateZoomBar(Widget parent, char *name,
                       ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgZoomBarWidgetClass,
                          parent, args, count);
}
