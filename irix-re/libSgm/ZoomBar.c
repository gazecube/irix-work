/*
 * SgZoomBar reconstruction.
 *
 * Recovered from the original IRIX N32 libSgm.so.1 ZoomBar.c unit.
 * The original widget is a single XmPrimitive. sectionWidths contains
 * cumulative X boundaries and sectionCount is the number of boundaries,
 * yielding sectionCount + 1 rendered spans.
 *
 * Binary-backed interaction model:
 *   <Btn1Down>:   Arm()
 *   <Btn1Up>:     Activate() Disarm()
 *   <EnterWindow>:Enter()
 *   <LeaveWindow>:Leave()
 *   <Motion>:     Motion()
 *
 * The old host reconstruction used one blank XmPushButton per section.
 * That architecture was only a temporary compatibility scaffold and is
 * intentionally gone from this implementation.
 */

#include <Sgm/Finder.h>

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

/* Original callback payload is reason, event, section. */
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
static Widget findFinderAncestor(Widget);
static void activateFinderSectionFallback(Widget, int);

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

/* Recovered verbatim from the original libSgm string table. */
static char zoomBarTranslations[] =
    "<Btn1Down>:Arm()\n"
    "<Btn1Up>:Activate() Disarm()\n"
    "<EnterWindow>:Enter()\n"
    "<LeaveWindow>:Leave()\n"
    "<Motion>:Motion()";

static SgZoomBarClassRecCompat sgZoomBarClassRec =
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
        zoomBarTranslations,
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
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    if (XtIsRealized(widget))
        zoomBarDrawAll(rec);
}

static void zoomBarRedisplay(Widget widget, XEvent *event, Region region)
{
    (void)event;
    (void)region;
    if (!XtIsRealized(widget))
        return;
    zoomBarDrawAll((SgZoomBarRecCompat *)widget);
}

static Boolean zoomBarSetValues(Widget old_widget,
                                Widget request_widget,
                                Widget new_widget,
                                ArgList args,
                                Cardinal *num_args)
{
    SgZoomBarRecCompat *oldrec = (SgZoomBarRecCompat *)old_widget;
    SgZoomBarRecCompat *newrec = (SgZoomBarRecCompat *)new_widget;
    Boolean changed;
    (void)request_widget;
    (void)args;
    (void)num_args;

    changed = (oldrec->zoom_bar.section_count != newrec->zoom_bar.section_count ||
               oldrec->zoom_bar.section_widths != newrec->zoom_bar.section_widths);

    /* Finder may mutate the same width array in place before XtSetValues. */
    if (!changed && newrec->zoom_bar.section_widths)
        changed = True;

    newrec->zoom_bar.armed = False;
    newrec->zoom_bar.armed_section = -1;
    newrec->zoom_bar.pointer_section = -1;
    return changed;
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

/*
 * Original geometry helper at 0x0b1dcb30:
 *   section 0: x=0, width=boundary[0]
 *   middle:    x=boundary[n-1]+1, width=boundary[n]-x
 *   final:     x=boundary[n-1]+1, width=widgetWidth-x-2
 * Vertical geometry is y=0, height=widgetHeight-1.
 */
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
    {
        return False;
    }

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

/* Exact binary hit-test at 0x0b1dcd7c. */
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

static void zoomBarDrawSection(SgZoomBarRecCompat *rec, int section, int state)
{
    XRectangle r;
    Display *display;
    Window window;
    GC top, bottom;

    if (!rec || !XtIsRealized((Widget)rec) ||
        !zoomBarSectionGeometry(rec, section, &r))
        return;

    display = XtDisplay((Widget)rec);
    window = XtWindow((Widget)rec);
    XClearArea(display, window, r.x, r.y, r.width, r.height, False);

    top = rec->primitive.top_shadow_GC;
    bottom = rec->primitive.bottom_shadow_GC;

    /*
     * The original renderer has several Motif state variants. The binary
     * uses state 0 for normal, 2 for pointer/hover and 4 for armed. Keep
     * those state numbers here so later pixel-level recovery does not alter
     * the action/state model.
     */
    if (r.width > 1 && r.height > 1)
    {
        GC upper = (state == 4) ? bottom : top;
        GC lower = (state == 4) ? top : bottom;
        int x1 = r.x;
        int y1 = r.y;
        int x2 = r.x + (int)r.width - 1;
        int y2 = r.y + (int)r.height - 1;

        XDrawLine(display, window, upper, x1, y1, x2, y1);
        XDrawLine(display, window, upper, x1, y1, x1, y2);
        XDrawLine(display, window, lower, x1, y2, x2, y2);
        XDrawLine(display, window, lower, x2, y1, x2, y2);
    }

    if (state == 2 && r.width > 3 && r.height > 3)
        XDrawRectangle(display, window, top,
                       r.x + 1, r.y + 1,
                       (unsigned int)(r.width - 3),
                       (unsigned int)(r.height - 3));
}

static void zoomBarDrawAll(SgZoomBarRecCompat *rec)
{
    int i, state;
    int last;

    if (!rec || !XtIsRealized((Widget)rec))
        return;

    last = rec->zoom_bar.section_count > 0 ?
           rec->zoom_bar.section_count : 0;
    for (i = 0; i <= last; ++i)
    {
        state = 0;
        if (rec->zoom_bar.armed && i == rec->zoom_bar.pointer_section)
            state = 4;
        else if (!rec->zoom_bar.armed && i == rec->zoom_bar.pointer_section)
            state = 2;
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

static Boolean eventInside(const SgZoomBarRecCompat *rec, const XEvent *event)
{
    int x, y;
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

    return x >= -(int)rec->core.border_width &&
           y >= -(int)rec->core.border_width &&
           x < (int)rec->core.width + (int)rec->core.border_width &&
           y < (int)rec->core.height + (int)rec->core.border_width;
}

static Widget findFinderAncestor(Widget widget)
{
    Widget p = XtParent(widget);
    while (p)
    {
        if (sgFinderWidgetClass && XtIsSubclass(p, sgFinderWidgetClass))
            return p;
        p = XtParent(p);
    }
    return NULL;
}

/*
 * Temporary host wiring compatibility: the original Finder installs an
 * activate callback on the ZoomBar. Finder.c has not yet recovered that
 * callback hookup, so preserve current path-click behavior until Pass 3
 * moves this into Finder where the SGI binary has it.
 */
static void activateFinderSectionFallback(Widget widget, int section)
{
    Widget finder;
    SgSetTextFunc set_section = NULL;

    if (section < 0)
        return;
    finder = findFinderAncestor(widget);
    if (!finder)
        return;
    XtVaGetValues(finder, SgNsetTextSectionFunc, &set_section, NULL);
    if (set_section)
        set_section(finder, section);
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
    rec->zoom_bar.armed = False;
    rec->zoom_bar.armed_section = -1;

    if (!eventInside(rec, event))
        return;

    if (rec->zoom_bar.pointer_section == section && section >= 0)
    {
        if (rec->zoom_bar.activate_callback)
            callZoomCallback(widget, rec->zoom_bar.activate_callback,
                             XmCR_ACTIVATE, event, section);
        else
            activateFinderSectionFallback(widget, section);
    }
}

static void Disarm(Widget widget, XEvent *event,
                   String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int old_section = rec->zoom_bar.armed_section;
    (void)params;
    (void)num_params;

    callZoomCallback(widget, rec->zoom_bar.disarm_callback,
                     XmCR_DISARM, event, old_section);
    rec->zoom_bar.armed = False;
    rec->zoom_bar.armed_section = -1;
    zoomBarDrawAll(rec);
}

static void Enter(Widget widget, XEvent *event,
                  String *params, Cardinal *num_params)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    int old_section, new_section;
    (void)params;
    (void)num_params;

    old_section = rec->zoom_bar.pointer_section;
    new_section = zoomBarHitSection(rec, eventX(event));
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
    int old_section, new_section;
    (void)params;
    (void)num_params;

    old_section = rec->zoom_bar.pointer_section;
    new_section = zoomBarHitSection(rec, eventX(event));
    if (new_section == old_section)
        return;

    rec->zoom_bar.pointer_section = new_section;
    if (old_section >= 0)
        zoomBarDrawSection(rec, old_section, 0);
    if (new_section >= 0)
        zoomBarDrawSection(rec, new_section,
                           rec->zoom_bar.armed ? 4 : 2);
}

/*
 * Original private/public helper. Arguments are widget, pointer X,
 * optional section result, optional XRectangle result.
 */
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
