/*
 * SgVisualDrawingArea reconstruction.
 *
 * Primary evidence: IRIX 6.5.0 libSgm.so.1, n32 MIPS, unstripped.
 *
 * Binary-backed facts used here:
 *   sgVisualDrawingAreaClassRec: 0x0b254388, n32 size 0xd0
 *   widget_size:                0x124
 *   superclass:                 xmDrawingAreaClassRec
 *   resources:                  3
 *   resource instance offsets:  0x104, 0x108, 0x109
 *   base class extension:       XmBaseClassExtRec v2, size 0x78
 *
 * The host structures preserve recovered semantics. Host pointer sizes and
 * alignment are intentionally allowed to differ from the original n32 ABI.
 */
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stddef.h>

#if defined(SGM_HAVE_DRAWINGAP_H) && defined(SGM_HAVE_BASECLASSP_H)
#include <Xm/DrawingAP.h>
#include <Xm/BaseClassP.h>

#ifndef XmInheritWidgetNavigable
#define XmInheritWidgetNavigable ((XmWidgetNavigableProc)_XtInherit)
#endif

typedef struct
{
    /* Original public resources: n32 +0x104 .. +0x109. */
    Visual *visual;
    Boolean install_colormap;
    Boolean dither_background;

    /* Original private state: n32 +0x10c .. +0x120. */
    Widget colormap_shell;
    Boolean shell_override_redirect;
    Colormap *saved_colormaps;
    int saved_colormap_count;
    Boolean colormap_installed;
    String dither_color_name;
} SgVisualDrawingAreaPartCompat;

typedef struct
{
    XmDrawingAreaRec drawing_area;
    SgVisualDrawingAreaPartCompat sg_visual_drawing_area;
} SgVisualDrawingAreaRecCompat;

/*
 * The original n32 XmDrawingArea class prefix ends at +0xc4. The remaining
 * 12 bytes of sgVisualDrawingAreaClassRec are all zero. Keep three reserved
 * class-part pointers rather than inventing methods that do not exist.
 */
typedef struct
{
    XtPointer reserved0;
    XtPointer reserved1;
    XtPointer reserved2;
} SgVisualDrawingAreaClassPartCompat;

typedef struct
{
    XmDrawingAreaClassRec drawing_area_class;
    SgVisualDrawingAreaClassPartCompat sg_visual_drawing_area_class;
} SgVisualDrawingAreaClassRecCompat;

static void visualDrawingAreaInitialize(Widget, Widget, ArgList, Cardinal *);
static void visualDrawingAreaRealize(Widget, XtValueMask *, XSetWindowAttributes *);
static void visualDrawingAreaDestroy(Widget);

void SgVisualDrawingAreaInstallColormap(Widget);
void SgVisualDrawingAreaUninstallColormap(Widget);

static XtResource visualDrawingAreaResources[] =
{
    {
        "visual", "Visual", XtRVisual, sizeof(Visual *),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.visual),
        XtRImmediate, (XtPointer)CopyFromParent
    },
    {
        "installColormap", "InstallColormap", XtRBoolean, sizeof(Boolean),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.install_colormap),
        XtRImmediate, (XtPointer)False
    },
    {
        "ditherBackground", "DitherBackground", XtRBoolean, sizeof(Boolean),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.dither_background),
        XtRImmediate, (XtPointer)False
    }
};

/*
 * VisualDrawingA.o contains a second one-entry XtResource record at
 * 0x0b254460. Its n32 offset is +0x120, size is sizeof(String), default is
 * immediate NULL, and Initialize passes the resulting string to
 * XAllocNamedColor(). The fast-quark resource name is the Core background
 * resource; fetching it as String preserves the original requested color
 * text before normal Pixel conversion.
 */
static XtResource ditherColorResource[] =
{
    {
        XtNbackground, XtCBackground, XtRString, sizeof(String),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.dither_color_name),
        XtRImmediate, (XtPointer)NULL
    }
};

/*
 * Exact Motif v2 base-class extension recovered from 0x0b254310.
 * All hooks are NULL; the only inherited hook is widgetNavigable.
 */
static XmBaseClassExtRec baseClassExtRec =
{
    NULL,                         /* next_extension */
    NULLQUARK,                    /* record_type */
    XmBaseClassExtVersion,        /* version */
    sizeof(XmBaseClassExtRec),    /* record_size */
    NULL,                         /* initializePrehook */
    NULL,                         /* setValuesPrehook */
    NULL,                         /* initializePosthook */
    NULL,                         /* setValuesPosthook */
    NULL,                         /* secondaryObjectClass */
    NULL,                         /* secondaryObjectCreate */
    NULL,                         /* getSecResData */
    { 0 },                        /* flags */
    NULL,                         /* getValuesPrehook */
    NULL,                         /* getValuesPosthook */
    NULL,                         /* classPartInitPrehook */
    NULL,                         /* classPartInitPosthook */
    NULL,                         /* ext_resources */
    NULL,                         /* compiled_ext_resources */
    0,                            /* num_ext_resources */
    False,                        /* use_sub_resources */
    XmInheritWidgetNavigable,     /* widgetNavigable */
    NULL,                         /* focusChange */
    NULL                          /* wrapperData */
};

/*
 * Static class record, matching the original inheritance contract. This is
 * intentionally not constructed by memcpy() from xmDrawingAreaClassRec.
 * Xt/Motif class records contain mutable private state; copying a live
 * superclass record and then pretending the copy is a subclass is unsafe.
 */
SgVisualDrawingAreaClassRecCompat sgVisualDrawingAreaClassRec =
{
    {
        {
            (WidgetClass)&xmDrawingAreaClassRec, /* superclass */
            "SgVisualDrawingArea",               /* class_name */
            sizeof(SgVisualDrawingAreaRecCompat), /* widget_size */
            NULL,                                 /* class_initialize */
            NULL,                                 /* class_part_initialize */
            False,                                /* class_inited */
            visualDrawingAreaInitialize,          /* initialize */
            NULL,                                 /* initialize_hook */
            visualDrawingAreaRealize,             /* realize */
            NULL,                                 /* actions */
            0,                                    /* num_actions */
            visualDrawingAreaResources,           /* resources */
            XtNumber(visualDrawingAreaResources), /* num_resources */
            NULLQUARK,                             /* xrm_class */
            True,                                 /* compress_motion */
            False,                                /* compress_exposure */
            True,                                 /* compress_enterleave */
            False,                                /* visible_interest */
            visualDrawingAreaDestroy,             /* destroy */
            XtInheritResize,                       /* resize */
            XtInheritExpose,                       /* expose */
            NULL,                                 /* set_values */
            NULL,                                 /* set_values_hook */
            XtInheritSetValuesAlmost,              /* set_values_almost */
            NULL,                                 /* get_values_hook */
            NULL,                                 /* accept_focus */
            XtVersion,                            /* version */
            NULL,                                 /* callback_private */
            XtInheritTranslations,                /* tm_table */
            XtInheritQueryGeometry,               /* query_geometry */
            NULL,                                 /* display_accelerator */
            (XtPointer)&baseClassExtRec            /* extension */
        },
        {
            XtInheritGeometryManager,
            XtInheritChangeManaged,
            XtInheritInsertChild,
            XtInheritDeleteChild,
            NULL
        },
        {
            NULL,
            0,
            0,
            NULL,
            NULL,
            NULL,
            NULL
        },
        {
            XtInheritTranslations,
            NULL,
            0,
            NULL,
            0,
            NULL,
            (XtPointer)_XtInherit
        },
        {
            NULL
        }
    },
    {
        NULL,
        NULL,
        NULL
    }
};

WidgetClass sgVisualDrawingAreaWidgetClass =
    (WidgetClass)&sgVisualDrawingAreaClassRec;

static Widget findShell(Widget w)
{
    Widget shell = XtParent(w);

    while (shell && !XtIsShell(shell))
        shell = XtParent(shell);

    return shell;
}

static unsigned short complementingColorComponent(unsigned short exact,
                                                   unsigned short allocated)
{
    long value = ((long)exact * 2L) - (long)allocated;

    if (value < 0)
        value = 0;
    else if (value > 65535L)
        value = 65535L;

    return (unsigned short)value;
}

static void createDitherBackground(SgVisualDrawingAreaRecCompat *rec,
                                   ArgList args,
                                   Cardinal num_args)
{
    SgVisualDrawingAreaPartCompat *sg = &rec->sg_visual_drawing_area;
    XColor allocated;
    XColor exact;
    XColor complement;
    Pixmap pixmap;
    Display *display;

    if (!sg->dither_background)
        return;

    /* Original Initialize clears the flag first and only restores it on success. */
    sg->dither_background = False;

    if (!sg->visual)
        return;

    if (sg->visual->class != TrueColor && sg->visual->class != StaticColor)
        return;

    if (rec->drawing_area.core.background_pixmap != XmUNSPECIFIED_PIXMAP)
        return;

    sg->dither_color_name = NULL;
    XtGetSubresources((Widget)rec,
                      (XtPointer)rec,
                      NULL,
                      NULL,
                      ditherColorResource,
                      XtNumber(ditherColorResource),
                      NULL,
                      0);

    if (!sg->dither_color_name)
        return;

    display = XtDisplay((Widget)rec);

    if (!XAllocNamedColor(display,
                          rec->drawing_area.core.colormap,
                          sg->dither_color_name,
                          &allocated,
                          &exact))
        return;

    if (allocated.pixel == rec->drawing_area.core.background_pixel &&
        allocated.red == exact.red &&
        allocated.green == exact.green &&
        allocated.blue == exact.blue)
        return;

    complement.red = complementingColorComponent(exact.red, allocated.red);
    complement.green = complementingColorComponent(exact.green, allocated.green);
    complement.blue = complementingColorComponent(exact.blue, allocated.blue);
    complement.flags = DoRed | DoGreen | DoBlue;

    if (!XAllocColor(display,
                     rec->drawing_area.core.colormap,
                     &complement))
        return;

    if (complement.pixel == allocated.pixel)
        return;

    pixmap = XmGetPixmapByDepth(rec->drawing_area.core.screen,
                                "50_foreground",
                                complement.pixel,
                                allocated.pixel,
                                rec->drawing_area.core.depth);

    if (pixmap == XmUNSPECIFIED_PIXMAP || pixmap == None)
        return;

    rec->drawing_area.core.background_pixmap = pixmap;
    sg->dither_background = True;

    (void)args;
    (void)num_args;
}

void SgVisualDrawingAreaInstallColormap(Widget w)
{
    SgVisualDrawingAreaRecCompat *rec;
    SgVisualDrawingAreaPartCompat *sg;
    Display *display;
    Window *old_windows = NULL;
    Window *new_windows = NULL;
    Window initial_windows[2];
    Window widget_window;
    Window shell_window;
    int count = 0;
    int i;

    if (!w)
        return;

    rec = (SgVisualDrawingAreaRecCompat *)w;
    sg = &rec->sg_visual_drawing_area;

    if (!sg->colormap_shell || !XtIsRealized(w) ||
        !XtIsRealized(sg->colormap_shell))
        return;

    display = XtDisplay(w);
    widget_window = XtWindow(w);
    shell_window = XtWindow(sg->colormap_shell);

    if (XGetWMColormapWindows(display,
                              shell_window,
                              &old_windows,
                              &count))
    {
        new_windows = (Window *)XtMalloc((Cardinal)(count + 1) *
                                         sizeof(Window));
        if (new_windows)
        {
            new_windows[0] = widget_window;
            for (i = 0; i < count; ++i)
                new_windows[i + 1] = old_windows[i];

            XSetWMColormapWindows(display,
                                  shell_window,
                                  new_windows,
                                  count + 1);
            XtFree((char *)new_windows);
        }

        if (old_windows)
            XFree(old_windows);
    }
    else
    {
        /* Original fallback installs both the drawing-area and shell windows. */
        initial_windows[0] = widget_window;
        initial_windows[1] = shell_window;
        XSetWMColormapWindows(display, shell_window, initial_windows, 2);
    }

    if (sg->shell_override_redirect)
    {
        sg->saved_colormaps =
            XListInstalledColormaps(display,
                                    widget_window,
                                    &sg->saved_colormap_count);
        XInstallColormap(display, rec->drawing_area.core.colormap);
    }

    sg->colormap_installed = True;
}

void SgVisualDrawingAreaUninstallColormap(Widget w)
{
    SgVisualDrawingAreaRecCompat *rec;
    SgVisualDrawingAreaPartCompat *sg;
    Display *display;
    Window *windows = NULL;
    Window widget_window;
    Window shell_window;
    int count = 0;
    int i;
    int found = -1;

    if (!w)
        return;

    rec = (SgVisualDrawingAreaRecCompat *)w;
    sg = &rec->sg_visual_drawing_area;

    if (!sg->colormap_shell || !XtIsRealized(w) ||
        !XtIsRealized(sg->colormap_shell))
    {
        sg->colormap_installed = False;
        return;
    }

    display = XtDisplay(w);
    widget_window = XtWindow(w);
    shell_window = XtWindow(sg->colormap_shell);

    if (XGetWMColormapWindows(display,
                              shell_window,
                              &windows,
                              &count))
    {
        for (i = 0; i < count; ++i)
        {
            if (windows[i] == widget_window)
            {
                found = i;
                break;
            }
        }

        if (found >= 0)
        {
            for (i = found + 1; i < count; ++i)
                windows[i - 1] = windows[i];

            XSetWMColormapWindows(display,
                                  shell_window,
                                  windows,
                                  count - 1);
        }

        XFree(windows);
    }

    if (sg->shell_override_redirect)
    {
        for (i = 0; i < sg->saved_colormap_count; ++i)
            XInstallColormap(display, sg->saved_colormaps[i]);

        if (sg->saved_colormaps)
            XFree(sg->saved_colormaps);
    }

    sg->saved_colormaps = NULL;
    sg->saved_colormap_count = 0;
    sg->colormap_installed = False;
}

static void visualDrawingAreaInitialize(Widget request,
                                        Widget new_widget,
                                        ArgList args,
                                        Cardinal *num_args)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)new_widget;
    SgVisualDrawingAreaPartCompat *sg = &rec->sg_visual_drawing_area;

    (void)request;

    sg->colormap_shell = NULL;
    sg->shell_override_redirect = False;
    sg->saved_colormaps = NULL;
    sg->saved_colormap_count = 0;
    sg->colormap_installed = False;
    sg->dither_color_name = NULL;

    createDitherBackground(rec,
                           args,
                           num_args ? *num_args : 0);
}

static void visualDrawingAreaRealize(Widget w,
                                     XtValueMask *value_mask,
                                     XSetWindowAttributes *attributes)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)w;
    SgVisualDrawingAreaPartCompat *sg = &rec->sg_visual_drawing_area;
    Arg arg;

    if (rec->drawing_area.core.width == 0)
        rec->drawing_area.core.width = 1;
    if (rec->drawing_area.core.height == 0)
        rec->drawing_area.core.height = 1;

    attributes->bit_gravity = NorthWestGravity;
    attributes->do_not_propagate_mask =
        KeyPressMask |
        KeyReleaseMask |
        ButtonPressMask |
        ButtonReleaseMask |
        PointerMotionMask;

    *value_mask |= CWBitGravity | CWDontPropagate;

    XtCreateWindow(w,
                   (unsigned int)InputOutput,
                   sg->visual,
                   *value_mask,
                   attributes);

    sg->colormap_shell = findShell(w);
    sg->shell_override_redirect = False;

    if (sg->colormap_shell)
    {
        XtSetArg(arg, XtNoverrideRedirect, &sg->shell_override_redirect);
        XtGetValues(sg->colormap_shell, &arg, 1);
    }

    if (sg->install_colormap)
        SgVisualDrawingAreaInstallColormap(w);
}

static void visualDrawingAreaDestroy(Widget w)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)w;

    if (rec->sg_visual_drawing_area.colormap_installed)
        SgVisualDrawingAreaUninstallColormap(w);
}

Widget SgCreateVisualDrawingArea(Widget parent,
                                 char *name,
                                 ArgList args,
                                 Cardinal count)
{
    return XtCreateWidget(name,
                          sgVisualDrawingAreaWidgetClass,
                          parent,
                          args,
                          count);
}

#else

/* Build fallback for Motif installations without the required private ABI. */
unsigned char sgVisualDrawingAreaClassRec[208];
WidgetClass sgVisualDrawingAreaWidgetClass = (WidgetClass)0;

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeVisualDrawingAreaFallback(void)
{
    sgVisualDrawingAreaWidgetClass = xmDrawingAreaWidgetClass;
}

Widget SgCreateVisualDrawingArea(Widget parent,
                                 char *name,
                                 ArgList args,
                                 Cardinal count)
{
    return XtCreateWidget(name,
                          sgVisualDrawingAreaWidgetClass,
                          parent,
                          args,
                          count);
}

void SgVisualDrawingAreaInstallColormap(Widget w) { (void)w; }
void SgVisualDrawingAreaUninstallColormap(Widget w) { (void)w; }

#endif
