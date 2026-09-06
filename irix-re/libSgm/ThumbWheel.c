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
 * pixmaps, copies the active frame with XCopyArea(), and uses a seven-level
 * grayscale ramp while generating the wheel raster.
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
#define TW_HOME_SIZE 16
#define TW_FRAME_COUNT 8
#define TW_BANK_COUNT 4
#define TW_GRAY_COUNT 7
#define TW_VALUE_STEP 1
#define TW_PAGE_STEP 10

#include "ThumbWheelRaster.inc"

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
    GC gray_gc[TW_GRAY_COUNT];
    Pixel gray_pixel[TW_GRAY_COUNT];
    Boolean gray_pixel_owned[TW_GRAY_COUNT];
    int forward_frame;
    int reverse_frame;
    Boolean forward_motion;
    unsigned char render_orientation;
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
preferredSize(const SgThumbWheelRecCompat *tw, Dimension *width, Dimension *height)
{
    Dimension longSize = TW_NATIVE_FACE_LONG;

    if (tw->thumbwheel.show_home_button)
        longSize += TW_HOME_SIZE;

    if (tw->thumbwheel.orientation == XmVERTICAL)
    {
        *width = TW_NATIVE_FACE_SHORT;
        *height = longSize;
    }
    else
    {
        *width = longSize;
        *height = TW_NATIVE_FACE_SHORT;
    }
}

static void
thumbInitialize(Widget request, Widget newWidget, ArgList args, Cardinal *count)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)newWidget;
    XGCValues values;
    Dimension preferredWidth;
    Dimension preferredHeight;
    int i;

    (void)request;
    (void)args;
    (void)count;

    if (tw->thumbwheel.maximum <= tw->thumbwheel.minimum)
        tw->thumbwheel.maximum = tw->thumbwheel.minimum + 1;

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

    preferredSize(tw, &preferredWidth, &preferredHeight);
    if (tw->core.width == 0)
        tw->core.width = preferredWidth;
    if (tw->core.height == 0)
        tw->core.height = preferredHeight;

    values.foreground = tw->primitive.foreground;
    values.background = tw->core.background_pixel;
    tw->thumbwheel.gc = XtGetGC(newWidget,
                                GCForeground | GCBackground,
                                &values);

    for (i = 0; i < TW_FRAME_COUNT; ++i)
        tw->thumbwheel.wheel_frames[i] = None;
    tw->thumbwheel.home_frames[0] = None;
    tw->thumbwheel.home_frames[1] = None;
    for (i = 0; i < TW_GRAY_COUNT; ++i)
    {
        tw->thumbwheel.gray_gc[i] = 0;
        tw->thumbwheel.gray_pixel[i] = 0;
        tw->thumbwheel.gray_pixel_owned[i] = False;
    }
    tw->thumbwheel.forward_frame = 0;
    tw->thumbwheel.reverse_frame = 4;
    tw->thumbwheel.forward_motion = True;
    tw->thumbwheel.render_orientation = 0xff;
}

static void
freeGrayRamp(SgThumbWheelRecCompat *tw)
{
    Display *dpy = XtDisplay((Widget)tw);
    Colormap cmap = tw->core.colormap;
    int i;

    for (i = 0; i < TW_GRAY_COUNT; ++i)
    {
        if (tw->thumbwheel.gray_gc[i])
            XtReleaseGC((Widget)tw, tw->thumbwheel.gray_gc[i]);
        tw->thumbwheel.gray_gc[i] = 0;

        if (tw->thumbwheel.gray_pixel_owned[i])
        {
            unsigned long pixel = tw->thumbwheel.gray_pixel[i];
            XFreeColors(dpy, cmap, &pixel, 1, 0);
        }
        tw->thumbwheel.gray_pixel_owned[i] = False;
    }
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

    tw->thumbwheel.render_orientation = 0xff;
}

static void
thumbDestroy(Widget widget)
{
    SgThumbWheelRecCompat *tw = (SgThumbWheelRecCompat *)widget;

    invalidateRenderAssets(tw);
    freeGrayRamp(tw);
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
    Dimension pw;
    Dimension ph;

    preferredSize(tw, &pw, &ph);

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
ensureGrayRamp(SgThumbWheelRecCompat *tw)
{
    Display *dpy = XtDisplay((Widget)tw);
    Colormap cmap = tw->core.colormap;
    XGCValues values;
    int i;

    if (tw->thumbwheel.gray_gc[0])
        return;

    for (i = 0; i < TW_GRAY_COUNT; ++i)
    {
        XColor color;
        unsigned short level = (unsigned short)(sgThumbWheelGrayRamp[i] * 257u);

        memset(&color, 0, sizeof(color));
        color.red = level;
        color.green = level;
        color.blue = level;
        color.flags = DoRed | DoGreen | DoBlue;

        if (XAllocColor(dpy, cmap, &color))
        {
            tw->thumbwheel.gray_pixel[i] = color.pixel;
            tw->thumbwheel.gray_pixel_owned[i] = True;
        }
        else
        {
            tw->thumbwheel.gray_pixel[i] = (i < (TW_GRAY_COUNT / 2))
                ? tw->primitive.bottom_shadow_color
                : tw->primitive.top_shadow_color;
        }

        values.foreground = tw->thumbwheel.gray_pixel[i];
        values.background = tw->core.background_pixel;
        tw->thumbwheel.gray_gc[i] = XtGetGC((Widget)tw,
                                            GCForeground | GCBackground,
                                            &values);
    }
}

static void
replayNativeFrame(SgThumbWheelRecCompat *tw, Drawable d, int frameIndex)
{
    const SgThumbWheelRasterFrame *frame = &sgThumbWheelVerticalFrames[frameIndex];
    Display *dpy = XtDisplay((Widget)tw);
    GC fill = tw->thumbwheel.gc;
    unsigned int width = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    unsigned int height = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;
    unsigned short i;

    XSetForeground(dpy, fill, tw->core.background_pixel);
    XFillRectangle(dpy, d, fill, 0, 0, width, height);

    for (i = 0; i < frame->count; ++i)
    {
        const unsigned char *op = frame->ops + (i * 5u);
        unsigned int gray = op[0];
        int x1 = op[1];
        int y1 = op[2];
        int x2 = op[3];
        int y2 = op[4];

        if (gray >= TW_GRAY_COUNT)
            continue;

        if (tw->thumbwheel.orientation == XmVERTICAL)
            XDrawLine(dpy, d, tw->thumbwheel.gray_gc[gray],
                      x1, y1, x2, y2);
        else
            XDrawLine(dpy, d, tw->thumbwheel.gray_gc[gray],
                      y1, x1, y2, x2);
    }
}

static void
drawHomeFrame(SgThumbWheelRecCompat *tw, Drawable d, Boolean pressed)
{
    Display *dpy = XtDisplay((Widget)tw);
    GC gc = tw->thumbwheel.gc;
    GC top = pressed ? tw->primitive.bottom_shadow_GC
                     : tw->primitive.top_shadow_GC;
    GC bottom = pressed ? tw->primitive.top_shadow_GC
                        : tw->primitive.bottom_shadow_GC;

    XSetForeground(dpy, gc, tw->core.background_pixel);
    XFillRectangle(dpy, d, gc, 0, 0, TW_HOME_SIZE, TW_HOME_SIZE);

    XDrawLine(dpy, d, top, 0, 0, TW_HOME_SIZE - 1, 0);
    XDrawLine(dpy, d, top, 0, 0, 0, TW_HOME_SIZE - 1);
    XDrawLine(dpy, d, bottom,
              0, TW_HOME_SIZE - 1, TW_HOME_SIZE - 1, TW_HOME_SIZE - 1);
    XDrawLine(dpy, d, bottom,
              TW_HOME_SIZE - 1, 0, TW_HOME_SIZE - 1, TW_HOME_SIZE - 1);

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
    unsigned int width;
    unsigned int height;
    int i;

    if (!XtIsRealized((Widget)tw))
        return;

    if (tw->thumbwheel.wheel_frames[0] != None &&
        tw->thumbwheel.render_orientation == tw->thumbwheel.orientation)
        return;

    invalidateRenderAssets(tw);
    ensureGrayRamp(tw);

    dpy = XtDisplay((Widget)tw);
    win = XtWindow((Widget)tw);
    width = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    height = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;

    for (i = 0; i < TW_FRAME_COUNT; ++i)
    {
        tw->thumbwheel.wheel_frames[i] = XCreatePixmap(
            dpy, win, width, height, (unsigned int)tw->core.depth);
        replayNativeFrame(tw, tw->thumbwheel.wheel_frames[i], i);
    }

    for (i = 0; i < 2; ++i)
    {
        tw->thumbwheel.home_frames[i] = XCreatePixmap(
            dpy, win, TW_HOME_SIZE, TW_HOME_SIZE,
            (unsigned int)tw->core.depth);
        drawHomeFrame(tw, tw->thumbwheel.home_frames[i], i != 0);
    }

    tw->thumbwheel.render_orientation = tw->thumbwheel.orientation;
}

static void
drawThumbWheel(SgThumbWheelRecCompat *tw)
{
    Display *dpy;
    Window win;
    Pixmap frame;
    int faceWidth;
    int faceHeight;
    int controlWidth;
    int controlHeight;
    int controlX;
    int controlY;
    int faceX;
    int faceY;

    if (!XtIsRealized((Widget)tw))
        return;

    ensureRenderAssets(tw);

    dpy = XtDisplay((Widget)tw);
    win = XtWindow((Widget)tw);
    faceWidth = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_SHORT : TW_NATIVE_FACE_LONG;
    faceHeight = (tw->thumbwheel.orientation == XmVERTICAL)
        ? TW_NATIVE_FACE_LONG : TW_NATIVE_FACE_SHORT;

    controlWidth = faceWidth;
    controlHeight = faceHeight;
    if (tw->thumbwheel.show_home_button)
    {
        if (tw->thumbwheel.orientation == XmVERTICAL)
            controlHeight += TW_HOME_SIZE;
        else
            controlWidth += TW_HOME_SIZE;
    }

    controlX = ((int)tw->core.width - controlWidth) / 2;
    controlY = ((int)tw->core.height - controlHeight) / 2;
    if (controlX < 0) controlX = 0;
    if (controlY < 0) controlY = 0;

    faceX = controlX;
    faceY = controlY;

    XSetForeground(dpy, tw->thumbwheel.gc, tw->core.background_pixel);
    XFillRectangle(dpy, win, tw->thumbwheel.gc, 0, 0,
                   (unsigned int)tw->core.width,
                   (unsigned int)tw->core.height);

    frame = tw->thumbwheel.forward_motion
        ? tw->thumbwheel.wheel_frames[tw->thumbwheel.forward_frame]
        : tw->thumbwheel.wheel_frames[tw->thumbwheel.reverse_frame];

    if (frame != None)
        XCopyArea(dpy, frame, win, tw->thumbwheel.gc,
                  0, 0, (unsigned int)faceWidth, (unsigned int)faceHeight,
                  faceX, faceY);

    if (tw->thumbwheel.show_home_button &&
        tw->thumbwheel.home_frames[0] != None)
    {
        int x;
        int y;

        if (tw->thumbwheel.orientation == XmVERTICAL)
        {
            x = controlX;
            y = controlY + faceHeight;
        }
        else
        {
            x = controlX + faceWidth;
            y = controlY;
        }

        XCopyArea(dpy, tw->thumbwheel.home_frames[0], win,
                  tw->thumbwheel.gc,
                  0, 0, TW_HOME_SIZE, TW_HOME_SIZE, x, y);
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

static void
keyDelta(Widget w, XEvent *e, int delta)
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
