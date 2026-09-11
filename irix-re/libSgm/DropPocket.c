/*
 * SgDropPocket reconstruction.
 *
 * Private widget identity and method layout are recovered from the original
 * IRIX N32 libSgm.so.1 DropPocket.c compilation unit.  The binary is
 * unstripped and carries SGI DWARF/source-line information.
 *
 * Binary-backed facts used here:
 *   source unit:             DropPocket.c
 *   native superclass:      XmPrimitive
 *   original instance size: 0xf8 (N32)
 *   exported class record:  sgDropPocketClassRec
 *   exported class handle:  sgDropPocketWidgetClass
 *   public resources:       activePixel, iconDataBasePath, name,
 *                           iconUpdateCallback
 *   default icon DB:        /usr/lib/filetype/desktop.otr
 *   drag action:            StartDrag
 *   original dependency:    libdesktopFileicon.so
 *   original methods:       ClassInitialize, Initialize, Destroy, Resize,
 *                           Redisplay, SetValues, StartDragProc,
 *                           DoConvertProc, DragDropFinish
 *
 * The SGI fileicon renderer ABI is intentionally not guessed here.  We probe
 * for libdesktopFileicon.so exactly as the original widget did, but retain a
 * host-side fallback drawing path until the fi* signatures are recovered.
 */

#include <Xm/PrimitiveP.h>
#include <Xm/Xm.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#ifndef XmInheritBorderHighlight
#define XmInheritBorderHighlight ((XtWidgetProc)_XtInherit)
#endif
#ifndef XmInheritBorderUnhighlight
#define XmInheritBorderUnhighlight ((XtWidgetProc)_XtInherit)
#endif

#define SG_DP_DEFAULT_DB "/usr/lib/filetype/desktop.otr"
#define SG_DP_DEFAULT_SIZE 28

#define SgNactivePixel "activePixel"
#define SgCActivePixel "ActivePixel"
#define SgNiconDataBasePath "iconDataBasePath"
#define SgCIconDataBasePath "IconDataBasePath"
#define SgNname "name"
#define SgCName "Name"
#define SgNiconUpdateCallback "iconUpdateCallback"
#define SgCIconUpdateCallback "IconUpdateCallback"

typedef struct
{
    Pixel active_pixel;
    String icon_database_path;
    String name;
    XtCallbackList icon_update_callback;

    /* Host reconstruction state.  Not claimed to match N32 offsets yet. */
    GC gc;
    Boolean armed;
    Boolean fileicon_available;
    void *fileicon_handle;
} SgDropPocketPartCompat;

typedef struct _SgDropPocketRec
{
    CorePart core;
    XmPrimitivePart primitive;
    SgDropPocketPartCompat drop_pocket;
} SgDropPocketRecCompat;

typedef struct
{
    XtPointer extension;
} SgDropPocketClassPartCompat;

typedef struct _SgDropPocketClassRec
{
    CoreClassPart core_class;
    XmPrimitiveClassPart primitive_class;
    SgDropPocketClassPartCompat drop_pocket_class;
} SgDropPocketClassRecCompat;

static void dpClassInitialize(void);
static void dpInitialize(Widget, Widget, ArgList, Cardinal *);
static void dpDestroy(Widget);
static void dpResize(Widget);
static void dpRedisplay(Widget, XEvent *, Region);
static Boolean dpSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult dpQueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static void StartDragProc(Widget, XEvent *, String *, Cardinal *);
static void dpArmReleaseEH(Widget, XtPointer, XEvent *, Boolean *);
static void drawBackground(SgDropPocketRecCompat *);
static void drawIcon(SgDropPocketRecCompat *);
static void loadFileiconLibrary(SgDropPocketRecCompat *);

static XtResource dpResources[] =
{
    {
        SgNactivePixel, SgCActivePixel, XtRPixel, sizeof(Pixel),
        XtOffsetOf(SgDropPocketRecCompat, drop_pocket.active_pixel),
        XtRString, (XtPointer)"black"
    },
    {
        SgNiconDataBasePath, SgCIconDataBasePath, XtRString, sizeof(String),
        XtOffsetOf(SgDropPocketRecCompat, drop_pocket.icon_database_path),
        XtRString, (XtPointer)SG_DP_DEFAULT_DB
    },
    {
        SgNname, SgCName, XtRString, sizeof(String),
        XtOffsetOf(SgDropPocketRecCompat, drop_pocket.name),
        XtRString, (XtPointer)""
    },
    {
        SgNiconUpdateCallback, SgCIconUpdateCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgDropPocketRecCompat, drop_pocket.icon_update_callback),
        XtRCallback, (XtPointer)NULL
    }
};

static XtActionsRec dpActions[] =
{
    { "StartDrag", StartDragProc }
};

/* Recovered verbatim from the original string table. */
static char dpTranslations[] =
    "<Btn2Down>: StartDrag()\n"
    "<Btn1Down>: StartDrag()";

SgDropPocketClassRecCompat sgDropPocketClassRec =
{
    {
        (WidgetClass)&xmPrimitiveClassRec,
        "SgDropPocket",
        sizeof(SgDropPocketRecCompat),
        dpClassInitialize,
        NULL,
        False,
        dpInitialize,
        NULL,
        XtInheritRealize,
        dpActions,
        XtNumber(dpActions),
        dpResources,
        XtNumber(dpResources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        dpDestroy,
        dpResize,
        dpRedisplay,
        dpSetValues,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        dpTranslations,
        dpQueryGeometry,
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

WidgetClass sgDropPocketNativeWidgetClass =
    (WidgetClass)&sgDropPocketClassRec;

/*
 * SgmCompat.c owns the compatibility ABI variable today.  Keep that exported
 * object stable and replace its XmDrawingArea placeholder after its own
 * constructor has initialized the compatibility table.  DropPocket.c is linked
 * after SgmCompat.c, so this constructor follows it in the init array.
 */
extern WidgetClass sgDropPocketWidgetClass;

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void dpInstallNativeClass(void)
{
    sgDropPocketWidgetClass = sgDropPocketNativeWidgetClass;
}

static void dpClassInitialize(void)
{
    /* The original ClassInitialize is intentionally tiny. */
}

static char *dpDupString(const char *s)
{
    if (!s)
        s = "";
    return XtNewString(s);
}

static void loadFileiconLibrary(SgDropPocketRecCompat *rec)
{
    if (!rec || rec->drop_pocket.fileicon_handle)
        return;

    rec->drop_pocket.fileicon_handle =
        dlopen("libdesktopFileicon.so", RTLD_LAZY | RTLD_LOCAL);
    rec->drop_pocket.fileicon_available =
        rec->drop_pocket.fileicon_handle ? True : False;
}

Boolean SgHasFileIconLibrary(void)
{
    void *handle = dlopen("libdesktopFileicon.so", RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        return False;
    dlclose(handle);
    return True;
}

static void dpInitialize(Widget request, Widget new_widget,
                         ArgList args, Cardinal *num_args)
{
    SgDropPocketRecCompat *rec = (SgDropPocketRecCompat *)new_widget;

    (void)request;
    (void)args;
    (void)num_args;

    rec->drop_pocket.icon_database_path =
        dpDupString(rec->drop_pocket.icon_database_path);
    rec->drop_pocket.name = dpDupString(rec->drop_pocket.name);
    rec->drop_pocket.gc = NULL;
    rec->drop_pocket.armed = False;
    rec->drop_pocket.fileicon_available = False;
    rec->drop_pocket.fileicon_handle = NULL;

    if (rec->core.width == 0)
        rec->core.width = SG_DP_DEFAULT_SIZE;
    if (rec->core.height == 0)
        rec->core.height = SG_DP_DEFAULT_SIZE;

    loadFileiconLibrary(rec);

    XtAddEventHandler(new_widget,
                      ButtonReleaseMask | LeaveWindowMask,
                      False,
                      dpArmReleaseEH,
                      NULL);
}

static void dpDestroy(Widget w)
{
    SgDropPocketRecCompat *rec = (SgDropPocketRecCompat *)w;

    if (rec->drop_pocket.gc)
    {
        XtReleaseGC(w, rec->drop_pocket.gc);
        rec->drop_pocket.gc = NULL;
    }

    if (rec->drop_pocket.icon_database_path)
    {
        XtFree(rec->drop_pocket.icon_database_path);
        rec->drop_pocket.icon_database_path = NULL;
    }

    if (rec->drop_pocket.name)
    {
        XtFree(rec->drop_pocket.name);
        rec->drop_pocket.name = NULL;
    }

    if (rec->drop_pocket.fileicon_handle)
    {
        dlclose(rec->drop_pocket.fileicon_handle);
        rec->drop_pocket.fileicon_handle = NULL;
    }
}

static void dpResize(Widget w)
{
    (void)w;
}

static void ensureGC(SgDropPocketRecCompat *rec)
{
    XGCValues values;
    XtGCMask mask;

    if (!rec || rec->drop_pocket.gc)
        return;

    values.foreground = rec->primitive.foreground;
    values.background = rec->core.background_pixel;
    mask = GCForeground | GCBackground;
    rec->drop_pocket.gc = XtGetGC((Widget)rec, mask, &values);
}

static void drawBackground(SgDropPocketRecCompat *rec)
{
    Display *dpy;
    Window win;
    GC gc;
    Dimension w;
    Dimension h;
    unsigned int shadow;

    if (!rec || !XtIsRealized((Widget)rec))
        return;

    ensureGC(rec);
    gc = rec->drop_pocket.gc;
    if (!gc)
        return;

    dpy = XtDisplay((Widget)rec);
    win = XtWindow((Widget)rec);
    w = rec->core.width;
    h = rec->core.height;

    XSetForeground(dpy, gc, rec->core.background_pixel);
    XFillRectangle(dpy, win, gc, 0, 0, w, h);

    shadow = rec->primitive.shadow_thickness;
    if (shadow < 1)
        shadow = 1;

    /* Pocket recess: Motif shadow pixels preserve the SGI resource palette. */
    XSetForeground(dpy, gc, rec->primitive.bottom_shadow_color);
    XFillRectangle(dpy, win, gc, 1, 1,
                   w > 2 ? w - 2 : 1,
                   shadow);
    XFillRectangle(dpy, win, gc, 1, 1,
                   shadow,
                   h > 2 ? h - 2 : 1);

    XSetForeground(dpy, gc, rec->primitive.top_shadow_color);
    if (w > shadow + 1)
        XFillRectangle(dpy, win, gc,
                       1, h > shadow ? h - shadow - 1 : 0,
                       w > 2 ? w - 2 : 1, shadow);
    if (h > shadow + 1)
        XFillRectangle(dpy, win, gc,
                       w > shadow ? w - shadow - 1 : 0, 1,
                       shadow, h > 2 ? h - 2 : 1);
}

static void drawIcon(SgDropPocketRecCompat *rec)
{
    Display *dpy;
    Window win;
    GC gc;
    int x;
    int y;
    unsigned int iw;
    unsigned int ih;
    XPoint page[5];

    if (!rec || !XtIsRealized((Widget)rec))
        return;

    ensureGC(rec);
    gc = rec->drop_pocket.gc;
    if (!gc)
        return;

    dpy = XtDisplay((Widget)rec);
    win = XtWindow((Widget)rec);

    iw = rec->core.width > 16 ? 14 : (rec->core.width > 4 ? rec->core.width - 4 : 1);
    ih = rec->core.height > 16 ? 16 : (rec->core.height > 4 ? rec->core.height - 4 : 1);
    x = ((int)rec->core.width - (int)iw) / 2;
    y = ((int)rec->core.height - (int)ih) / 2;

    /*
     * Temporary host fallback for the original file_bits/filemask_bits and
     * fiDrawIcon path.  It deliberately renders as an icon-shaped pocket,
     * never as the old featureless teal rectangle.  Replace with the exact
     * recovered bitmap/fileicon renderer when the fi* ABI pass is complete.
     */
    page[0].x = (short)x;          page[0].y = (short)y;
    page[1].x = (short)(x + iw - 4); page[1].y = (short)y;
    page[2].x = (short)(x + iw);   page[2].y = (short)(y + 4);
    page[3].x = (short)(x + iw);   page[3].y = (short)(y + ih);
    page[4].x = (short)x;          page[4].y = (short)(y + ih);

    XSetForeground(dpy, gc,
                   rec->drop_pocket.armed
                       ? rec->drop_pocket.active_pixel
                       : rec->primitive.foreground);
    XDrawLines(dpy, win, gc, page, 5, CoordModeOrigin);
    XDrawLine(dpy, win, gc,
              x, y + ih, x, y);
    XDrawLine(dpy, win, gc,
              x + (int)iw - 4, y,
              x + (int)iw - 4, y + 4);
    XDrawLine(dpy, win, gc,
              x + (int)iw - 4, y + 4,
              x + (int)iw, y + 4);
}

static void dpRedisplay(Widget w, XEvent *event, Region region)
{
    SgDropPocketRecCompat *rec = (SgDropPocketRecCompat *)w;

    (void)event;
    (void)region;

    drawBackground(rec);
    drawIcon(rec);
}

static Boolean dpSetValues(Widget old_widget, Widget request_widget,
                           Widget new_widget, ArgList args,
                           Cardinal *num_args)
{
    SgDropPocketRecCompat *oldrec = (SgDropPocketRecCompat *)old_widget;
    SgDropPocketRecCompat *newrec = (SgDropPocketRecCompat *)new_widget;
    Boolean redraw = False;

    (void)request_widget;
    (void)args;
    (void)num_args;

    if (oldrec->drop_pocket.active_pixel != newrec->drop_pocket.active_pixel)
        redraw = True;

    if (oldrec->drop_pocket.icon_database_path !=
        newrec->drop_pocket.icon_database_path)
    {
        String s = dpDupString(newrec->drop_pocket.icon_database_path);
        XtFree(oldrec->drop_pocket.icon_database_path);
        newrec->drop_pocket.icon_database_path = s;
        redraw = True;
    }

    if (oldrec->drop_pocket.name != newrec->drop_pocket.name)
    {
        String s = dpDupString(newrec->drop_pocket.name);
        XtFree(oldrec->drop_pocket.name);
        newrec->drop_pocket.name = s;
        redraw = True;
    }

    if (oldrec->primitive.foreground != newrec->primitive.foreground ||
        oldrec->core.background_pixel != newrec->core.background_pixel)
    {
        if (newrec->drop_pocket.gc)
        {
            XtReleaseGC(new_widget, newrec->drop_pocket.gc);
            newrec->drop_pocket.gc = NULL;
        }
        redraw = True;
    }

    return redraw;
}

static XtGeometryResult dpQueryGeometry(Widget w,
                                        XtWidgetGeometry *intended,
                                        XtWidgetGeometry *preferred)
{
    (void)w;
    (void)intended;

    if (!preferred)
        return XtGeometryYes;

    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = SG_DP_DEFAULT_SIZE;
    preferred->height = SG_DP_DEFAULT_SIZE;
    return XtGeometryAlmost;
}

static void StartDragProc(Widget w, XEvent *event,
                          String *params, Cardinal *num_params)
{
    SgDropPocketRecCompat *rec = (SgDropPocketRecCompat *)w;

    (void)params;
    (void)num_params;

    /*
     * Original DropPocket.c enters the Motif drag/drop path here and emits a
     * Desktop object payload of the form:
     *
     * Category:File Name:%s Type:%s Host:%s ViewX:0 ViewY:0 ViewGallery:0
     *
     * Keep the interaction state and rendering faithful while the exact
     * DoConvertProc/fiTypeName ABI is being recovered.
     */
    rec->drop_pocket.armed = True;
    if (XtIsRealized(w))
        XClearArea(XtDisplay(w), XtWindow(w), 0, 0, 0, 0, True);

    (void)event;
}

static void dpArmReleaseEH(Widget w, XtPointer client_data,
                           XEvent *event, Boolean *continue_dispatch)
{
    SgDropPocketRecCompat *rec = (SgDropPocketRecCompat *)w;

    (void)client_data;
    (void)continue_dispatch;

    if (!rec->drop_pocket.armed)
        return;

    if (event->type == ButtonRelease || event->type == LeaveNotify)
    {
        rec->drop_pocket.armed = False;
        if (XtIsRealized(w))
            XClearArea(XtDisplay(w), XtWindow(w), 0, 0, 0, 0, True);
    }
}
