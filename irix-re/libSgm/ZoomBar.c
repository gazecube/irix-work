/*
 * SgZoomBar reconstruction.
 *
 * Source structure and private record names are taken from the original SGI
 * ZoomBar.h/ZoomBarP.h. Function behavior is reconstructed from the N32
 * libSgm.a(ZoomBar.o) object and debug libSgm.so.1.
 */

#include <Sgm/ZoomBarP.h>
#include "SgiShader.h"

#include <Xm/Xm.h>
#include <Xm/XmP.h>
#include <Xm/BaseClassP.h>
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

/* Original callback payload used by Finder.c. */
typedef struct
{
    int reason;
    XEvent *event;
    int section;
} SgZoomBarCallbackStruct;

static void ClassInitialize(void);
static void InitializePrehook(Widget, Widget, ArgList, Cardinal *);
static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Resize(Widget);
static void Redisplay(Widget, XEvent *, Region);
static Boolean SetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult QueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);

static void Arm(Widget, XEvent *, String *, Cardinal *);
static void Activate(Widget, XEvent *, String *, Cardinal *);
static void Disarm(Widget, XEvent *, String *, Cardinal *);
static void Enter(Widget, XEvent *, String *, Cardinal *);
static void Leave(Widget, XEvent *, String *, Cardinal *);
static void Motion(Widget, XEvent *, String *, Cardinal *);

static int FindSection(SgZoomBarWidget, Position);
static Boolean GetOneButtonW(SgZoomBarWidget, int, Position *, Dimension *);
static void GetOneButtonH(SgZoomBarWidget, Position *, Dimension *);
static Boolean GetOneButtonRect(SgZoomBarWidget, int, XRectangle *);
static void DrawButton(SgZoomBarWidget, int, int);
static void DrawButtons(SgZoomBarWidget);
static Position EventX(const XEvent *);
static Boolean EventInside(SgZoomBarWidget, const XEvent *);
static void CallZoomCallback(Widget, XtCallbackList, int, XEvent *, int);

/* Exact five-state table from original N32 ZoomBar.o data. */
static const unsigned char shaderTable[5][10] =
{
    { 1, 2, 3, 4, 6, 6, 4, 4, 4, 0 },
    { 1, 4, 4, 4, 4, 4, 4, 4, 4, 0 },
    { 1, 2, 3, 6, 6, 6, 6, 6, 6, 0 },
    { 1, 6, 2, 3, 3, 6, 6, 6, 6, 0 },
    { 1, 6, 2, 3, 3, 6, 6, 4, 4, 0 }
};

static XtResource resources[] =
{
    {
        XmNactivateCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRec, zoomBar.activate_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        XmNarmCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRec, zoomBar.arm_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        XmNdisarmCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgZoomBarRec, zoomBar.disarm_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        SgNsectionWidths, SgCSectionWidths, XtRPointer,
        sizeof(Dimension *),
        XtOffsetOf(SgZoomBarRec, zoomBar.section_widths),
        XtRImmediate, (XtPointer)NULL
    },
    {
        SgNsectionCount, SgCSectionCount, XtRInt,
        sizeof(int),
        XtOffsetOf(SgZoomBarRec, zoomBar.section_count),
        XtRImmediate, (XtPointer)0
    }
};

static XtActionsRec actions[] =
{
    { "Arm", Arm },
    { "Activate", Activate },
    { "Disarm", Disarm },
    { "Enter", Enter },
    { "Leave", Leave },
    { "Motion", Motion }
};

char _SgZoomBar_defaultTranslations[108] =
    "<Btn1Down>:Arm()\n"
    "<Btn1Up>:Activate() Disarm()\n"
    "<EnterWindow>:Enter()\n"
    "<LeaveWindow>:Leave()\n"
    "<Motion>:Motion()";

/*
 * ZoomBar.o keeps the parsed translations separately and installs them from
 * an XmBaseClass initialize prehook. This is important: the drawing code can
 * work perfectly while the actions remain dead if this hook is omitted.
 */
static XtTranslations parsedTranslations = NULL;

static XmBaseClassExtRec baseClassExtRec =
{
    .next_extension = NULL,
    .record_type = NULLQUARK,
    .version = XmBaseClassExtVersion,
    .record_size = sizeof(XmBaseClassExtRec),
    .initializePrehook = InitializePrehook
};

SgZoomBarClassRec sgZoomBarClassRec =
{
    {
        (WidgetClass)&xmPrimitiveClassRec,
        "SgZoomBar",
        sizeof(SgZoomBarRec),
        ClassInitialize,
        NULL,
        False,
        Initialize,
        NULL,
        XtInheritRealize,
        actions,
        XtNumber(actions),
        resources,
        XtNumber(resources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        NULL,
        Resize,
        Redisplay,
        SetValues,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        _SgZoomBar_defaultTranslations,
        QueryGeometry,
        NULL,
        (XtPointer)&baseClassExtRec
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
    { 0 }
};

WidgetClass sgZoomBarWidgetClass = (WidgetClass)&sgZoomBarClassRec;

static void ClassInitialize(void)
{
    parsedTranslations = XtParseTranslationTable(_SgZoomBar_defaultTranslations);
    baseClassExtRec.record_type = XmQmotif;
}

static void InitializePrehook(Widget request, Widget new_widget,
                              ArgList args, Cardinal *num_args)
{
    WidgetClass wc;
    (void)request;
    (void)args;
    (void)num_args;

    if (!new_widget || !parsedTranslations)
        return;

    wc = XtClass(new_widget);
    if (wc)
        wc->core_class.tm_table = (String)parsedTranslations;
}

static void Initialize(Widget request, Widget new_widget,
                       ArgList args, Cardinal *num_args)
{
    SgZoomBarWidget w = (SgZoomBarWidget)new_widget;
    (void)request;
    (void)args;
    (void)num_args;

    w->zoomBar.armed = False;
    w->zoomBar.click_count = 0;
    w->zoomBar.pushed_section = -1;
    w->zoomBar.located_section = -1;

    if (w->core.width == 0)
        w->core.width = 1;
    if (w->core.height == 0)
        w->core.height = 6;
}

static void Resize(Widget widget)
{
    if (XtIsRealized(widget))
        DrawButtons((SgZoomBarWidget)widget);
}

static void Redisplay(Widget widget, XEvent *event, Region region)
{
    (void)event;
    (void)region;
    if (XtIsRealized(widget))
        DrawButtons((SgZoomBarWidget)widget);
}

static Boolean SetValues(Widget old_widget, Widget request_widget,
                         Widget new_widget, ArgList args, Cardinal *num_args)
{
    SgZoomBarWidget w = (SgZoomBarWidget)new_widget;
    (void)old_widget;
    (void)request_widget;
    (void)args;
    (void)num_args;

    w->zoomBar.armed = False;
    w->zoomBar.pushed_section = -1;
    w->zoomBar.located_section = -1;
    return True;
}

static XtGeometryResult QueryGeometry(Widget widget,
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

/* Exact GetOneButtonW() geometry from ZoomBar.o. */
static Boolean GetOneButtonW(SgZoomBarWidget w, int section,
                             Position *x_return, Dimension *width_return)
{
    int x;
    int width;

    if (!w || !x_return || !width_return || section < 0)
        return False;

    if (w->zoomBar.section_count <= 0 || !w->zoomBar.section_widths)
    {
        if (section != 0)
            return False;
        /*
         * DrawButtons() has a special unsectioned path in the original:
         * x=0,width=core.width-1,state=1. GetOneButtonW() normally applies
         * the one-pixel section inset, so choose the pre-inset values which
         * yield that exact final rectangle when called for section zero.
         */
        x = -1;
        width = (int)w->core.width + 1;
    }
    else if (section == 0)
    {
        x = 0;
        width = (int)w->zoomBar.section_widths[0];
    }
    else if (section < w->zoomBar.section_count)
    {
        x = (int)w->zoomBar.section_widths[section - 1];
        width = (int)w->zoomBar.section_widths[section] - x;
    }
    else if (section == w->zoomBar.section_count)
    {
        x = (int)w->zoomBar.section_widths[section - 1];
        width = (int)w->core.width - x - 1;
    }
    else
        return False;

    x += 1;
    width -= 2;

    if (width < 1)
        width = 1;

    *x_return = (Position)x;
    *width_return = (Dimension)width;
    return True;
}

static void GetOneButtonH(SgZoomBarWidget w,
                          Position *y_return, Dimension *height_return)
{
    int height = (int)w->core.height - 1;
    if (height < 1)
        height = 1;
    *y_return = 0;
    *height_return = (Dimension)height;
}

static Boolean GetOneButtonRect(SgZoomBarWidget w, int section,
                                XRectangle *rect)
{
    Position x, y;
    Dimension width, height;

    if (!rect || !GetOneButtonW(w, section, &x, &width))
        return False;
    GetOneButtonH(w, &y, &height);

    rect->x = (short)x;
    rect->y = (short)y;
    rect->width = (unsigned short)width;
    rect->height = (unsigned short)height;
    return True;
}

static int FindSection(SgZoomBarWidget w, Position x)
{
    int i;

    if (!w || w->zoomBar.section_count <= 0 ||
        !w->zoomBar.section_widths)
        return -1;

    if (x < (Position)w->zoomBar.section_widths[0])
        return 0;

    if (x >= (Position)w->zoomBar.section_widths[w->zoomBar.section_count - 1])
        return w->zoomBar.section_count;

    for (i = 1; i < w->zoomBar.section_count; ++i)
        if (x <= (Position)w->zoomBar.section_widths[i])
            return i;

    return -1;
}

static void DrawButton(SgZoomBarWidget w, int section, int state)
{
    XRectangle r;
    Display *dpy;
    Window win;
    GC g0, g1, g2, g3, g4, g5, g6, g7;
    int x, y, width, height;

    if (!w || !XtIsRealized((Widget)w) || state < 0 || state > 4 ||
        !GetOneButtonRect(w, section, &r))
        return;

    x = r.x;
    y = r.y;
    width = r.width;
    height = r.height;

    if (width < 5 || height < 5)
        return;

    dpy = XtDisplay((Widget)w);
    win = XtWindow((Widget)w);

    g0 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][0]);
    g1 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][1]);
    g2 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][2]);
    g3 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][3]);
    g4 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][4]);
    g5 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][5]);
    g6 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][6]);
    g7 = SgShaderGetGC((Widget)w, w->core.background_pixel, shaderTable[state][7]);

    if (!g0 || !g1 || !g2 || !g3 || !g4 || !g5 || !g6 || !g7)
        return;

    XDrawLine(dpy, win, g4, x, y, x + width, y);
    XDrawLine(dpy, win, g4, x, y + 1, x, y + height);

    XDrawLine(dpy, win, g5, x + 1, y + 1, x + width - 1, y + 1);
    XDrawLine(dpy, win, g5, x + 1, y + 2, x + 1, y + height - 1);

    XDrawLine(dpy, win, g6, x + 2, y + 2, x + width - 2, y + 2);
    XDrawLine(dpy, win, g6, x + 2, y + 3, x + 2, y + height - 2);

    XFillRectangle(dpy, win, g7,
                   x + 3, y + 3,
                   (unsigned int)(width - 5),
                   (unsigned int)(height - 5));

    XDrawLine(dpy, win, g3, x + 3, y + height - 2,
                            x + width - 2, y + height - 2);
    XDrawLine(dpy, win, g3, x + width - 2, y + 3,
                            x + width - 2, y + height - 3);

    XDrawLine(dpy, win, g2, x + 2, y + height - 1,
                            x + width - 1, y + height - 1);
    XDrawLine(dpy, win, g2, x + width - 1, y + 2,
                            x + width - 1, y + height - 2);

    XDrawLine(dpy, win, g1, x + 1, y + height,
                            x + width, y + height);
    XDrawLine(dpy, win, g1, x + width, y + 1,
                            x + width, y + 2);

    XDrawLine(dpy, win, g0, x + width + 1, y,
                            x + width + 1, y + height);
}

static void DrawButtons(SgZoomBarWidget w)
{
    int i;

    if (!w || !XtIsRealized((Widget)w))
        return;

    /* Exact special path: one full-width state-1 button, no section inset. */
    if (w->zoomBar.section_count <= 0 || !w->zoomBar.section_widths)
    {
        DrawButton(w, 0, 1);
        return;
    }

    for (i = 0; i <= w->zoomBar.section_count; ++i)
    {
        int state = 0;
        if (i == w->zoomBar.located_section)
            state = w->zoomBar.armed ? 4 : 2;
        DrawButton(w, i, state);
    }
}

static Position EventX(const XEvent *event)
{
    if (!event)
        return -1;

    switch (event->type)
    {
        case ButtonPress:
        case ButtonRelease:
            return (Position)event->xbutton.x;
        case MotionNotify:
            return (Position)event->xmotion.x;
        case EnterNotify:
        case LeaveNotify:
            return (Position)event->xcrossing.x;
        default:
            return -1;
    }
}

static Boolean EventInside(SgZoomBarWidget w, const XEvent *event)
{
    int x, y;
    int bw;

    if (!w || !event)
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

    bw = (int)w->core.border_width;
    return x >= -bw && y >= -bw &&
           x < (int)w->core.width + bw &&
           y < (int)w->core.height + bw;
}

static void CallZoomCallback(Widget widget, XtCallbackList list,
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
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    int section;
    (void)params;
    (void)num_params;

    w->zoomBar.armed = True;
    section = FindSection(w, EventX(event));
    w->zoomBar.pushed_section = section;
    w->zoomBar.located_section = section;
    DrawButtons(w);
    CallZoomCallback(widget, w->zoomBar.arm_callback, XmCR_ARM, event, section);
}

static void Activate(Widget widget, XEvent *event,
                     String *params, Cardinal *num_params)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    int section;
    (void)params;
    (void)num_params;

    if (!w->zoomBar.armed)
        return;

    section = w->zoomBar.pushed_section;
    w->zoomBar.pushed_section = -1;
    w->zoomBar.armed = False;
    DrawButtons(w);

    if (EventInside(w, event))
        CallZoomCallback(widget, w->zoomBar.activate_callback,
                         XmCR_ACTIVATE, event, section);
}

static void Disarm(Widget widget, XEvent *event,
                   String *params, Cardinal *num_params)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    (void)params;
    (void)num_params;

    CallZoomCallback(widget, w->zoomBar.disarm_callback,
                     XmCR_DISARM, event, w->zoomBar.pushed_section);
}

static void Enter(Widget widget, XEvent *event,
                  String *params, Cardinal *num_params)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    int old_section;
    int new_section;

    _XmPrimitiveEnter(widget, event, params, num_params);

    old_section = w->zoomBar.located_section;
    new_section = FindSection(w, EventX(event));
    w->zoomBar.located_section = new_section;

    if (old_section != new_section)
    {
        if (old_section >= 0)
            DrawButton(w, old_section, 0);
        if (new_section >= 0)
            DrawButton(w, new_section, w->zoomBar.armed ? 4 : 2);
    }
}

static void Leave(Widget widget, XEvent *event,
                  String *params, Cardinal *num_params)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;

    _XmPrimitiveLeave(widget, event, params, num_params);

    w->zoomBar.armed = False;
    w->zoomBar.pushed_section = -1;
    w->zoomBar.located_section = -1;
    DrawButtons(w);
}

static void Motion(Widget widget, XEvent *event,
                   String *params, Cardinal *num_params)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    int old_section = w->zoomBar.located_section;
    int new_section = FindSection(w, EventX(event));
    (void)params;
    (void)num_params;

    if (new_section == old_section)
        return;

    w->zoomBar.located_section = new_section;

    if (old_section >= 0)
        DrawButton(w, old_section, 0);
    if (new_section >= 0)
        DrawButton(w, new_section, w->zoomBar.armed ? 4 : 2);
}

void _SgZoomBarGetSectionInfo(Widget widget, Position x,
                              int *section_return, XRectangle *rect_return)
{
    SgZoomBarWidget w = (SgZoomBarWidget)widget;
    int section = FindSection(w, x);

    if (section_return)
        *section_return = section;

    if (!rect_return)
        return;

    if (section < 0 || !GetOneButtonRect(w, section, rect_return))
        memset(rect_return, 0, sizeof(*rect_return));
}

Widget SgCreateZoomBar(Widget parent, char *name,
                       ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgZoomBarWidgetClass, parent, args, count);
}
