/*
 * SgThumbWheel reconstruction.
 *
 * Public ABI is taken from the original SGI ThumbWheel.h. Private widget
 * layout and behavior are recovered from IRIX libSgm.so.1.
 *
 * Binary-backed n32 facts:
 *   superclass:       xmPrimitiveClassRec
 *   widget size:      0x150
 *   resources:        11
 *   actions:          21
 *   minimum:          +0xb8, default 0
 *   maximum:          +0xbc, default 100
 *   homePosition:     +0xc0, default 50
 *   angleRange:       +0xc4, default 150
 *   unitsPerRotation: +0xc8, default 240
 *   value:            +0xcc, default from homePosition
 *   orientation:      +0xd0, default XmVERTICAL
 *   animate:          +0xd1, default False
 *   showHomeButton:   +0xd2, default True
 *   dragCallback:     +0xd4
 *   valueChanged:     +0xd8
 *
 * The original private drawing state extends through +0x14f. Host-only
 * fields below preserve the recovered semantics but do not claim n32 ABI
 * offsets. Drawing is a portable reconstruction of the original ribbed
 * wheel until every private raster helper is recovered.
 */
#include <Sgm/ThumbWheel.h>

#include <Xm/PrimitiveP.h>
#include <Xm/Xm.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

#ifndef XmInheritBorderHighlight
#define XmInheritBorderHighlight ((XtWidgetProc)_XtInherit)
#endif
#ifndef XmInheritBorderUnhighlight
#define XmInheritBorderUnhighlight ((XtWidgetProc)_XtInherit)
#endif

#define TW_DEFAULT_WIDTH 22
#define TW_DEFAULT_LENGTH 104
#define TW_RIB_SPACING 5
#define TW_VALUE_STEP 1
#define TW_PAGE_STEP 10

typedef struct
{
    int minimum;
    int maximum;
    int home_position;
    int angle_range;
    int units_per_rotation;
    int value;
    unsigned char orientation;
    Boolean animate;
    Boolean show_home_button;
    XtCallbackList drag_callback;
    XtCallbackList value_changed_callback;

    /* RE: private host state, names reconstructed from behavior. */
    Boolean button_down;
    int press_coordinate;
    int press_value;
    GC gc;
} SgThumbWheelPartCompat;

typedef struct _SgThumbWheelRec
{
    CorePart core;
    XmPrimitivePart primitive;
    SgThumbWheelPartCompat thumbwheel;
} SgThumbWheelRecCompat;

typedef struct
{
    XtPointer extension;
} SgThumbWheelClassPartCompat;

typedef struct _SgThumbWheelClassRec
{
    CoreClassPart core_class;
    XmPrimitiveClassPart primitive_class;
    SgThumbWheelClassPartCompat thumbwheel_class;
} SgThumbWheelClassRecCompat;

static void thumbInitialize(Widget, Widget, ArgList, Cardinal *);
static void thumbDestroy(Widget);
static void thumbResize(Widget);
static void thumbExpose(Widget, XEvent *, Region);
static Boolean thumbSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult thumbQueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);

static void actionMotion(Widget, XEvent *, String *, Cardinal *);
static void actionBtn1Down(Widget, XEvent *, String *, Cardinal *);
static void actionBtn2Down(Widget, XEvent *, String *, Cardinal *);
static void actionBtn3Down(Widget, XEvent *, String *, Cardinal *);
static void actionBtn1Motion(Widget, XEvent *, String *, Cardinal *);
static void actionBtn2Motion(Widget, XEvent *, String *, Cardinal *);
static void actionBtn3Motion(Widget, XEvent *, String *, Cardinal *);
static void actionBtn1Up(Widget, XEvent *, String *, Cardinal *);
static void actionBtn2Up(Widget, XEvent *, String *, Cardinal *);
static void actionBtn3Up(Widget, XEvent *, String *, Cardinal *);
static void actionEnter(Widget, XEvent *, String *, Cardinal *);
static void actionLeave(Widget, XEvent *, String *, Cardinal *);
static void actionPageUp(Widget, XEvent *, String *, Cardinal *);
static void actionPageDown(Widget, XEvent *, String *, Cardinal *);
static void actionUp(Widget, XEvent *, String *, Cardinal *);
static void actionDown(Widget, XEvent *, String *, Cardinal *);
static void actionLeft(Widget, XEvent *, String *, Cardinal *);
static void actionRight(Widget, XEvent *, String *, Cardinal *);
static void actionHelp(Widget, XEvent *, String *, Cardinal *);
static void actionBeginLine(Widget, XEvent *, String *, Cardinal *);
static void actionEndLine(Widget, XEvent *, String *, Cardinal *);

static void setValueInternal(SgThumbWheelRecCompat *, int, int, XEvent *, Boolean);
static void drawThumbWheel(SgThumbWheelRecCompat *);
static int eventCoordinate(const SgThumbWheelRecCompat *, const XEvent *);

/*
 * Original resource table at 0x0b243f20, 11 XtResource entries.
 * The value resource uses an XtRCallProc in IRIX; its callback copies
 * homePosition into value. Initialize performs the same operation portably.
 */
static XtResource thumbResources[] =
{
    {
        XmNminimum, XmCMinimum, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.minimum),
        XmRImmediate, (XtPointer)0
    },
    {
        XmNmaximum, XmCMaximum, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.maximum),
        XmRImmediate, (XtPointer)100
    },
    {
        SgNhomePosition, SgCHomePosition, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.home_position),
        XmRImmediate, (XtPointer)50
    },
    {
        SgNangleRange, SgCAngleRange, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.angle_range),
        XmRImmediate, (XtPointer)150
    },
    {
        SgNunitsPerRotation, SgCUnitsPerRotation, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.units_per_rotation),
        XmRImmediate, (XtPointer)240
    },
    {
        XmNvalue, XmCValue, XmRInt, sizeof(int),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.value),
        XmRImmediate, (XtPointer)50
    },
    {
        XmNorientation, XmCOrientation, XmROrientation, sizeof(unsigned char),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.orientation),
        XmRImmediate, (XtPointer)XmVERTICAL
    },
    {
        SgNanimate, SgCAnimate, XmRBoolean, sizeof(Boolean),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.animate),
        XmRImmediate, (XtPointer)False
    },
    {
        XmNdragCallback, XmCCallback, XmRCallback, sizeof(XtCallbackList),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.drag_callback),
        XmRCallback, (XtPointer)NULL
    },
    {
        XmNvalueChangedCallback, XmCCallback, XmRCallback, sizeof(XtCallbackList),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.value_changed_callback),
        XmRCallback, (XtPointer)NULL
    },
    {
        SgNshowHomeButton, SgCShowHomeButton, XmRBoolean, sizeof(Boolean),
        XtOffsetOf(SgThumbWheelRecCompat, thumbwheel.show_home_button),
        XmRImmediate, (XtPointer)True
    }
};

/* Exact 21-entry action identity and order recovered from 0x0b244264. */
static XtActionsRec thumbActions[] =
{
    { "Motion", actionMotion },
    { "Btn1Down", actionBtn1Down },
    { "Btn2Down", actionBtn2Down },
    { "Btn3Down", actionBtn3Down },
    { "Btn1Motion", actionBtn1Motion },
    { "Btn2Motion", actionBtn2Motion },
    { "Btn3Motion", actionBtn3Motion },
    { "Btn1Up", actionBtn1Up },
    { "Btn2Up", actionBtn2Up },
    { "Btn3Up", actionBtn3Up },
    { "Enter", actionEnter },
    { "Leave", actionLeave },
    { "PageUp", actionPageUp },
    { "PageDown", actionPageDown },
    { "Up", actionUp },
    { "Down", actionDown },
    { "Left", actionLeft },
    { "Right", actionRight },
    { "Help", actionHelp },
    { "BeginLine", actionBeginLine },
    { "EndLine", actionEndLine }
};

/* Exact translation string recovered from 0x0b244054. */
static char thumbTranslations[] =
    "<Motion>:\tMotion() \n"
    " <Btn1Down>:\tBtn1Down() \n"
    " <Btn2Down>:\tBtn2Down() \n"
    " <Btn3Down>:\tBtn3Down() \n"
    " <Btn1Motion>:\tBtn1Motion() \n"
    " <Btn2Motion>:\tBtn2Motion() \n"
    " <Btn3Motion>:\tBtn3Motion() \n"
    " <Btn1Up>:\tBtn1Up() \n"
    " <Btn2Up>:\tBtn2Up() \n"
    " <Btn3Up>:\tBtn3Up() \n"
    " <EnterWindow>:\tEnter() \n"
    " <LeaveWindow>:\tLeave() \n"
    " <Key>osfUp:\tUp() \n"
    " <Key>osfDown:\tDown() \n"
    " <Key>osfLeft:\tLeft() \n"
    " <Key>osfRight:\tRight() \n"
    " <Key>osfPageUp:\tPageUp() \n"
    " <Key>osfPageDown:\tPageDown() \n"
    " <Key>osfBeginLine:\tBeginLine() \n"
    " <Key>osfEndLine:\tEndLine() \n"
    " <Key>osfHelp:\tHelp()";

SgThumbWheelClassRecCompat sgThumbWheelClassRec =
{
    {
        (WidgetClass)&xmPrimitiveClassRec,
        "ThumbWheel",
        sizeof(SgThumbWheelRecCompat),
        NULL,
        NULL,
        False,
        thumbInitialize,
        NULL,
        XtInheritRealize,
        thumbActions,
        XtNumber(thumbActions),
        thumbResources,
        XtNumber(thumbResources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        thumbDestroy,
        thumbResize,
        thumbExpose,
        thumbSetValues,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        thumbTranslations,
        thumbQueryGeometry,
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
    { NULL }
};

WidgetClass sgThumbWheelWidgetClass = (WidgetClass)&sgThumbWheelClassRec;

static int
clampValue(const SgThumbWheelRecCompat *tw, int value)
{
    if (value < tw->thumbwheel.minimum)
        return tw->thumbwheel.minimum;
    if (value > tw->thumbwheel.maximum)
        return tw->thumbwheel.maximum;
    return value;
}

static void
thumbInitialize(Widget request, Widget newWidget, ArgList args, Cardinal *count)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)newWidget;
    XGCValues values;

    (void)request;
    (void)args;
    (void)count;

    if (tw->thumbwheel.maximum <= tw->thumbwheel.minimum)
        tw->thumbwheel.maximum = tw->thumbwheel.minimum + 1;

    /* Original XtRCallProc default makes value equal homePosition. */
    if (tw->thumbwheel.value < tw->thumbwheel.minimum ||
        tw->thumbwheel.value > tw->thumbwheel.maximum)
        tw->thumbwheel.value = clampValue(tw, tw->thumbwheel.home_position);

    tw->thumbwheel.home_position = clampValue(tw, tw->thumbwheel.home_position);
    tw->thumbwheel.button_down = False;
    tw->thumbwheel.press_coordinate = 0;
    tw->thumbwheel.press_value = tw->thumbwheel.value;

    /*
     * RE: MaXX's native SgThumbWheel relies entirely on its class
     * translations for pointer interaction. OpenMotif does not always
     * compile inherited subclass tm_table data the same way as IRIX Motif,
     * so install the binary-recovered table explicitly on the host widget.
     */
    XtOverrideTranslations(newWidget,
                           XtParseTranslationTable(thumbTranslations));

    if (tw->core.width == 0)
        tw->core.width = (tw->thumbwheel.orientation == XmVERTICAL)
            ? TW_DEFAULT_WIDTH : TW_DEFAULT_LENGTH;
    if (tw->core.height == 0)
        tw->core.height = (tw->thumbwheel.orientation == XmVERTICAL)
            ? TW_DEFAULT_LENGTH : TW_DEFAULT_WIDTH;

    values.foreground = tw->primitive.foreground;
    values.background = tw->core.background_pixel;
    tw->thumbwheel.gc = XtGetGC(newWidget,
                                GCForeground | GCBackground,
                                &values);
}

static void
thumbDestroy(Widget widget)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;
    if (tw->thumbwheel.gc)
        XtReleaseGC(widget, tw->thumbwheel.gc);
    tw->thumbwheel.gc = 0;
}

static void
thumbResize(Widget widget)
{
    drawThumbWheel((SgThumbWheelRecCompat *)widget);
}

static void
thumbExpose(Widget widget, XEvent *event, Region region)
{
    (void)event;
    (void)region;
    drawThumbWheel((SgThumbWheelRecCompat *)widget);
}

static Boolean
thumbSetValues(Widget oldWidget, Widget requestWidget, Widget newWidget,
               ArgList args, Cardinal *count)
{
    SgThumbWheelRecCompat *oldTw = (SgThumbWheelRecCompat *)oldWidget;
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)newWidget;
    Boolean redraw = False;

    (void)requestWidget;
    (void)args;
    (void)count;

    if (tw->thumbwheel.maximum <= tw->thumbwheel.minimum)
        tw->thumbwheel.maximum = tw->thumbwheel.minimum + 1;

    tw->thumbwheel.home_position = clampValue(tw, tw->thumbwheel.home_position);
    tw->thumbwheel.value = clampValue(tw, tw->thumbwheel.value);

    if (oldTw->thumbwheel.value != tw->thumbwheel.value ||
        oldTw->thumbwheel.minimum != tw->thumbwheel.minimum ||
        oldTw->thumbwheel.maximum != tw->thumbwheel.maximum ||
        oldTw->thumbwheel.orientation != tw->thumbwheel.orientation ||
        oldTw->thumbwheel.show_home_button != tw->thumbwheel.show_home_button)
        redraw = True;

    return redraw;
}

static XtGeometryResult
thumbQueryGeometry(Widget widget, XtWidgetGeometry *intended,
                   XtWidgetGeometry *preferred)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;
    Dimension pw = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_DEFAULT_WIDTH : TW_DEFAULT_LENGTH;
    Dimension ph = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_DEFAULT_LENGTH : TW_DEFAULT_WIDTH;

    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = pw;
    preferred->height = ph;

    if (intended &&
        (intended->request_mode & (CWWidth | CWHeight)) == (CWWidth | CWHeight) &&
        intended->width == pw && intended->height == ph)
        return XtGeometryYes;
    if (widget->core.width == pw && widget->core.height == ph)
        return XtGeometryNo;
    return XtGeometryAlmost;
}

static void
drawThumbWheel(SgThumbWheelRecCompat *tw)
{
    Display *dpy;
    Window win;
    GC gc;
    int width, height;
    int i;
    int center;

    if (!XtIsRealized((Widget)tw))
        return;

    dpy = XtDisplay((Widget)tw);
    win = XtWindow((Widget)tw);
    gc = tw->thumbwheel.gc;
    width = (int)tw->core.width;
    height = (int)tw->core.height;

    XSetForeground(dpy, gc, tw->core.background_pixel);
    XFillRectangle(dpy, win, gc, 0, 0,
                   (unsigned int)width, (unsigned int)height);

    XSetForeground(dpy, gc, tw->primitive.foreground);
    XDrawRectangle(dpy, win, gc, 1, 1,
                   (unsigned int)(width > 3 ? width - 3 : 1),
                   (unsigned int)(height > 3 ? height - 3 : 1));

    if (tw->thumbwheel.orientation == XmVERTICAL)
    {
        center = height / 2;
        for (i = 7; i < height - 7; i += TW_RIB_SPACING)
        {
            int phase = (tw->thumbwheel.value % TW_RIB_SPACING + TW_RIB_SPACING) % TW_RIB_SPACING;
            int y = i + phase - (TW_RIB_SPACING / 2);
            if (y > 4 && y < height - 4)
                XDrawLine(dpy, win, gc, 4, y, width - 5, y);
        }
        XDrawLine(dpy, win, gc, 3, center, width - 4, center);

        if (tw->thumbwheel.show_home_button)
        {
            int y = center - 3;
            XFillRectangle(dpy, win, gc,
                           width / 2 - 2, y, 4, 6);
        }
    }
    else
    {
        center = width / 2;
        for (i = 7; i < width - 7; i += TW_RIB_SPACING)
        {
            int phase = (tw->thumbwheel.value % TW_RIB_SPACING + TW_RIB_SPACING) % TW_RIB_SPACING;
            int x = i + phase - (TW_RIB_SPACING / 2);
            if (x > 4 && x < width - 4)
                XDrawLine(dpy, win, gc, x, 4, x, height - 5);
        }
        XDrawLine(dpy, win, gc, center, 3, center, height - 4);

        if (tw->thumbwheel.show_home_button)
            XFillRectangle(dpy, win, gc,
                           center - 3, height / 2 - 2, 6, 4);
    }
}

static void
callThumbCallback(SgThumbWheelRecCompat *tw, XtCallbackList list,
                  int reason, XEvent *event)
{
    SgThumbWheelCallbackStruct cbs;
    if (!list)
        return;
    cbs.reason = reason;
    cbs.event = event;
    cbs.value = tw->thumbwheel.value;
    XtCallCallbackList((Widget)tw, list, &cbs);
}

static void
setValueInternal(SgThumbWheelRecCompat *tw, int value, int reason,
                 XEvent *event, Boolean callback)
{
    int old = tw->thumbwheel.value;
    value = clampValue(tw, value);
    if (old == value)
        return;

    tw->thumbwheel.value = value;
    drawThumbWheel(tw);

    if (callback)
    {
        if (reason == XmCR_DRAG)
            callThumbCallback(tw, tw->thumbwheel.drag_callback,
                              XmCR_DRAG, event);
        else
            callThumbCallback(tw, tw->thumbwheel.value_changed_callback,
                              XmCR_VALUE_CHANGED, event);
    }
}

static int
eventCoordinate(const SgThumbWheelRecCompat *tw, const XEvent *event)
{
    if (!event)
        return 0;
    if (event->type == ButtonPress || event->type == ButtonRelease)
        return tw->thumbwheel.orientation == XmVERTICAL
            ? event->xbutton.y : event->xbutton.x;
    if (event->type == MotionNotify)
        return tw->thumbwheel.orientation == XmVERTICAL
            ? event->xmotion.y : event->xmotion.x;
    return 0;
}

static void
actionMotion(Widget w, XEvent *e, String *p, Cardinal *n)
{
    (void)w; (void)e; (void)p; (void)n;
}

static void
actionBtn1Down(Widget w, XEvent *e, String *p, Cardinal *n)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    (void)p; (void)n;
    tw->thumbwheel.button_down = True;
    tw->thumbwheel.press_coordinate = eventCoordinate(tw, e);
    tw->thumbwheel.press_value = tw->thumbwheel.value;
}

/* Binary proves button 2/3 down, motion and up action bodies are no-ops. */
static void actionBtn2Down(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionBtn3Down(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }

static void
actionBtn1Motion(Widget w, XEvent *e, String *p, Cardinal *n)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    int coordinate;
    int delta;
    int value;

    (void)p; (void)n;
    if (!tw->thumbwheel.button_down)
        return;

    coordinate = eventCoordinate(tw, e);
    delta = coordinate - tw->thumbwheel.press_coordinate;
    if (tw->thumbwheel.orientation == XmVERTICAL)
        delta = -delta;

    value = tw->thumbwheel.press_value + delta;
    setValueInternal(tw, value, XmCR_DRAG, e, True);
}

static void actionBtn2Motion(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionBtn3Motion(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }

static void
actionBtn1Up(Widget w, XEvent *e, String *p, Cardinal *n)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    int before = tw->thumbwheel.press_value;
    (void)p; (void)n;
    if (!tw->thumbwheel.button_down)
        return;
    tw->thumbwheel.button_down = False;
    if (tw->thumbwheel.value != before)
        callThumbCallback(tw, tw->thumbwheel.value_changed_callback,
                          XmCR_VALUE_CHANGED, e);
}

static void actionBtn2Up(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionBtn3Up(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionEnter(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionLeave(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }

static void keyDelta(Widget w, XEvent *e, int delta)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    setValueInternal(tw, tw->thumbwheel.value + delta,
                     XmCR_VALUE_CHANGED, e, True);
}
static void actionPageUp(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, TW_PAGE_STEP); }
static void actionPageDown(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, -TW_PAGE_STEP); }
static void actionUp(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, TW_VALUE_STEP); }
static void actionDown(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, -TW_VALUE_STEP); }
static void actionLeft(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, -TW_VALUE_STEP); }
static void actionRight(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)p; (void)n; keyDelta(w, e, TW_VALUE_STEP); }
static void actionHelp(Widget w, XEvent *e, String *p, Cardinal *n)
{ (void)w; (void)e; (void)p; (void)n; }
static void actionBeginLine(Widget w, XEvent *e, String *p, Cardinal *n)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    (void)p; (void)n;
    setValueInternal(tw, tw->thumbwheel.minimum,
                     XmCR_VALUE_CHANGED, e, True);
}
static void actionEndLine(Widget w, XEvent *e, String *p, Cardinal *n)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)w;
    (void)p; (void)n;
    setValueInternal(tw, tw->thumbwheel.maximum,
                     XmCR_VALUE_CHANGED, e, True);
}

Widget
SgCreateThumbWheel(Widget parent, char *name, ArgList arglist, Cardinal argcount)
{
    return XtCreateWidget(name, sgThumbWheelWidgetClass,
                          parent, arglist, argcount);
}

void
SgThumbWheelGetValue(Widget widget, int *value)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;
    if (value)
        *value = tw->thumbwheel.value;
}

void
SgThumbWheelSetValue(Widget widget, int value)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;
    setValueInternal(tw, value, XmCR_VALUE_CHANGED, NULL, False);
}
