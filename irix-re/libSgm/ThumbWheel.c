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
 * MaXX x86-64 libSgm additionally proves that the native renderer keeps
 * eight cached wheel pixmaps in two four-frame banks, two 16x16 home-button
 * pixmaps, copies the active frame with XCopyArea(), and applies Motif
 * top/bottom shadow treatment around the wheel and home button.
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

#define TW_NATIVE_FACE_SHORT 16
#define TW_NATIVE_FACE_LONG 122
#define TW_DEFAULT_WIDTH TW_NATIVE_FACE_SHORT
#define TW_DEFAULT_LENGTH TW_NATIVE_FACE_LONG
#define TW_FRAME_COUNT 8
#define TW_BANK_COUNT 4
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

    /*
     * RE: MaXX keeps eight wheel pixmaps at host offsets +0x1b8..+0x1f0,
     * active forward/reverse frame pointers at +0x1f8/+0x200, two home
     * pixmaps at +0x210/+0x218, and the copy GC at +0x228.
     */
    Pixmap wheel_frames[TW_FRAME_COUNT];
    Pixmap home_frames[2];
    int forward_frame;
    int reverse_frame;
    Boolean forward_motion;
    Dimension render_width;
    Dimension render_height;
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
static void thumbPointerEH(Widget, XtPointer, XEvent *, Boolean *);
static void invalidateRenderAssets(SgThumbWheelRecCompat *);
static void ensureRenderAssets(SgThumbWheelRecCompat *);

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
    int i;

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

    XtOverrideTranslations(newWidget,
                           XtParseTranslationTable(thumbTranslations));

    /* Host glue for OpenMotif legacy Btn1 translation dispatch. */
    XtAddEventHandler(newWidget,
                      ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                      False, thumbPointerEH, NULL);

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

    for (i = 0; i < TW_FRAME_COUNT; ++i)
        tw->thumbwheel.wheel_frames[i] = None;
    tw->thumbwheel.home_frames[0] = None;
    tw->thumbwheel.home_frames[1] = None;
    tw->thumbwheel.forward_frame = 0;
    tw->thumbwheel.reverse_frame = 4;
    tw->thumbwheel.forward_motion = True;
    tw->thumbwheel.render_width = 0;
    tw->thumbwheel.render_height = 0;
}

static void
invalidateRenderAssets(SgThumbWheelRecCompat *tw)
{
    Display *dpy = XtDisplay((Widget)tw);
    int i;

    for (i = 0; i < TW_FRAME_COUNT; ++i)
    {
        if (tw->thumbwheel.wheel_frames[i] != None)
            XFreePixmap(dpy, tw->thumbwheel.wheel_frames[i]);
        tw->thumbwheel.wheel_frames[i] = None;
    }

    for (i = 0; i < 2; ++i)
    {
        if (tw->thumbwheel.home_frames[i] != None)
            XFreePixmap(dpy, tw->thumbwheel.home_frames[i]);
        tw->thumbwheel.home_frames[i] = None;
    }

    tw->thumbwheel.render_width = 0;
    tw->thumbwheel.render_height = 0;
}

static void
thumbDestroy(Widget widget)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;

    invalidateRenderAssets(tw);
    if (tw->thumbwheel.gc)
        XtReleaseGC(widget, tw->thumbwheel.gc);
    tw->thumbwheel.gc = 0;
}

static void
thumbResize(Widget widget)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;
    invalidateRenderAssets(tw);
    drawThumbWheel(tw);
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

    if (oldTw->thumbwheel.orientation != tw->thumbwheel.orientation)
        invalidateRenderAssets(tw);

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
drawShadowBox(Display *dpy, Drawable d, GC top, GC bottom,
              int x, int y, int width, int height, Boolean raised)
{
    GC tl = raised ? top : bottom;
    GC br = raised ? bottom : top;

    if (width < 2 || height < 2)
        return;

    XDrawLine(dpy, d, tl, x, y, x + width - 1, y);
    XDrawLine(dpy, d, tl, x, y, x, y + height - 1);
    XDrawLine(dpy, d, br,
              x, y + height - 1, x + width - 1, y + height - 1);
    XDrawLine(dpy, d, br,
              x + width - 1, y, x + width - 1, y + height - 1);
}

static void
drawWheelFrame(SgThumbWheelRecCompat *tw, Drawable d, int frame)
{
    static const unsigned char nativeRibs[] =
    {
        7, 13, 19, 26, 33, 40, 48, 57, 65, 74, 82, 91, 99, 106, 113
    };
    Display *dpy = XtDisplay((Widget)tw);
    GC gc = tw->thumbwheel.gc;
    GC top = tw->primitive.top_shadow_GC;
    GC bottom = tw->primitive.bottom_shadow_GC;
    int width = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    int height = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;
    int longLength;
    int shortLength;
    int phase;
    int i;

    XSetForeground(dpy, gc, tw->core.background_pixel);
    XFillRectangle(dpy, d, gc, 0, 0,
                   (unsigned int)width, (unsigned int)height);

    drawShadowBox(dpy, d, top, bottom,
                  1, 1, width - 2, height - 2, False);

    longLength = tw->thumbwheel.orientation == XmVERTICAL ? height : width;
    shortLength = tw->thumbwheel.orientation == XmVERTICAL ? width : height;
    phase = (frame & 3) - 1;
    if (frame >= 4)
        phase = -phase;

    for (i = 0; i < (int)(sizeof(nativeRibs) / sizeof(nativeRibs[0])); ++i)
    {
        int nativePos = (int)nativeRibs[i] + phase;
        int pos = nativePos;
        int nativeDistance = abs(nativePos - (TW_NATIVE_FACE_LONG / 2));
        int inset = 2 + (nativeDistance * 3) / (TW_NATIVE_FACE_LONG / 2);
        int near;
        int far;

        if (inset > shortLength / 3)
            inset = shortLength / 3;

        near = inset;
        far = shortLength - 1 - inset;
        if (far <= near)
            continue;

        /*
         * RE: native frames use paired highlight/shadow strokes whose
         * lengths change toward the ends of the wheel, producing the
         * characteristic curved SGI thumbwheel instead of flat ASCII ribs.
         */
        if (tw->thumbwheel.orientation == XmVERTICAL)
        {
            XDrawLine(dpy, d, top, near, pos - 1, far, pos - 1);
            XDrawLine(dpy, d, bottom, near, pos, far, pos);
        }
        else
        {
            XDrawLine(dpy, d, top, pos - 1, near, pos - 1, far);
            XDrawLine(dpy, d, bottom, pos, near, pos, far);
        }
    }
}

static void
drawHomeFrame(SgThumbWheelRecCompat *tw, Drawable d, Boolean pressed)
{
    Display *dpy = XtDisplay((Widget)tw);
    GC gc = tw->thumbwheel.gc;
    GC top = tw->primitive.top_shadow_GC;
    GC bottom = tw->primitive.bottom_shadow_GC;

    XSetForeground(dpy, gc, tw->core.background_pixel);
    XFillRectangle(dpy, d, gc, 0, 0, 16, 16);
    drawShadowBox(dpy, d, top, bottom, 0, 0, 16, 16, !pressed);

    XSetForeground(dpy, gc, tw->primitive.foreground);
    if (tw->thumbwheel.orientation == XmVERTICAL)
        XFillRectangle(dpy, d, gc, 7, 5, 2, 6);
    else
        XFillRectangle(dpy, d, gc, 5, 7, 6, 2);
}

static void
ensureRenderAssets(SgThumbWheelRecCompat *tw)
{
    Display *dpy;
    Window win;
    Dimension faceWidth = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    Dimension faceHeight = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;
    int i;

    if (!XtIsRealized((Widget)tw))
        return;

    if (tw->thumbwheel.render_width == faceWidth &&
        tw->thumbwheel.render_height == faceHeight &&
        tw->thumbwheel.wheel_frames[0] != None)
        return;

    invalidateRenderAssets(tw);

    dpy = XtDisplay((Widget)tw);
    win = XtWindow((Widget)tw);

    for (i = 0; i < TW_FRAME_COUNT; ++i)
    {
        tw->thumbwheel.wheel_frames[i] = XCreatePixmap(
            dpy, win,
            (unsigned int)faceWidth,
            (unsigned int)faceHeight,
            (unsigned int)tw->core.depth);
        drawWheelFrame(tw, tw->thumbwheel.wheel_frames[i], i);
    }

    for (i = 0; i < 2; ++i)
    {
        tw->thumbwheel.home_frames[i] = XCreatePixmap(
            dpy, win, 16, 16, (unsigned int)tw->core.depth);
        drawHomeFrame(tw, tw->thumbwheel.home_frames[i], i != 0);
    }

    tw->thumbwheel.render_width = faceWidth;
    tw->thumbwheel.render_height = faceHeight;
}

static void
drawThumbWheel(SgThumbWheelRecCompat *tw)
{
    Display *dpy;
    Window win;
    Pixmap frame;
    int width;
    int height;
    int faceWidth;
    int faceHeight;
    int faceX;
    int faceY;

    if (!XtIsRealized((Widget)tw))
        return;

    ensureRenderAssets(tw);

    dpy = XtDisplay((Widget)tw);
    win = XtWindow((Widget)tw);
    width = (int)tw->core.width;
    height = (int)tw->core.height;
    faceWidth = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    faceHeight = tw->thumbwheel.orientation == XmVERTICAL
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;
    faceX = (width - faceWidth) / 2;
    faceY = (height - faceHeight) / 2;
    if (faceX < 0)
        faceX = 0;
    if (faceY < 0)
        faceY = 0;

    XSetForeground(dpy, tw->thumbwheel.gc, tw->core.background_pixel);
    XFillRectangle(dpy, win, tw->thumbwheel.gc,
                   0, 0, (unsigned int)width, (unsigned int)height);

    frame = tw->thumbwheel.forward_motion
        ? tw->thumbwheel.wheel_frames[tw->thumbwheel.forward_frame]
        : tw->thumbwheel.wheel_frames[tw->thumbwheel.reverse_frame];

    if (frame != None)
        XCopyArea(dpy, frame, win, tw->thumbwheel.gc,
                  0, 0,
                  (unsigned int)faceWidth, (unsigned int)faceHeight,
                  faceX, faceY);

    if (tw->thumbwheel.show_home_button &&
        tw->thumbwheel.home_frames[0] != None)
    {
        int x = faceX + (faceWidth - 16) / 2;
        int y = faceY + (faceHeight - 16) / 2;
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        XCopyArea(dpy,
                  tw->thumbwheel.home_frames[0], win,
                  tw->thumbwheel.gc,
                  0, 0, 16, 16, x, y);
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
advanceRenderFrame(SgThumbWheelRecCompat *tw, int oldValue, int newValue)
{
    int steps;

    if (newValue == oldValue)
        return;

    steps = abs(newValue - oldValue);
    if (steps < 1)
        steps = 1;
    steps %= TW_BANK_COUNT;
    if (steps == 0)
        steps = 1;

    if (newValue > oldValue)
    {
        tw->thumbwheel.forward_frame =
            (tw->thumbwheel.forward_frame + steps) % TW_BANK_COUNT;
        tw->thumbwheel.forward_motion = True;
    }
    else
    {
        tw->thumbwheel.reverse_frame = 4 +
            ((tw->thumbwheel.reverse_frame - 4 + steps) % TW_BANK_COUNT);
        tw->thumbwheel.forward_motion = False;
    }
}

static void
setValueInternal(SgThumbWheelRecCompat *tw, int value, int reason,
                 XEvent *event, Boolean callback)
{
    int old = tw->thumbwheel.value;
    value = clampValue(tw, value);
    if (old == value)
        return;

    advanceRenderFrame(tw, old, value);
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
thumbPointerEH(Widget w, XtPointer closure, XEvent *event, Boolean *cont)
{
    Cardinal n = 0;

    (void)closure;
    (void)cont;

    switch (event->type)
    {
    case ButtonPress:
        if (event->xbutton.button == Button1)
            actionBtn1Down(w, event, NULL, &n);
        break;
    case MotionNotify:
        if (event->xmotion.state & Button1Mask)
            actionBtn1Motion(w, event, NULL, &n);
        else
            actionMotion(w, event, NULL, &n);
        break;
    case ButtonRelease:
        if (event->xbutton.button == Button1)
            actionBtn1Up(w, event, NULL, &n);
        break;
    default:
        break;
    }
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

    /* MaXX feeds the active-axis coordinate delta into its value mapping. */
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
