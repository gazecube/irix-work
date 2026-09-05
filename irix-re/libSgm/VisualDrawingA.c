/*
 * SgVisualDrawingArea reconstruction.
 *
 * Binary evidence from IRIX 6.5.0 libSgm.so.1:
 *   sgVisualDrawingAreaClassRec: 0x0b254388, n32 size 208
 *   widget_size:                0x124
 *   resources:                  3
 *   resource instance offsets:  0x104, 0x108, 0x109
 *   exported entry points:
 *     SgCreateVisualDrawingArea
 *     SgVisualDrawingAreaInstallColormap
 *     SgVisualDrawingAreaUninstallColormap
 *
 * The first resource uses Motif/Xt shared resource strings and is consistent
 * with the Visual pointer used throughout the class. The other two names are
 * present literally in the original binary: installColormap and
 * ditherBackground.
 */
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(SGM_HAVE_DRAWINGAP_H)
#include <Xm/DrawingAP.h>

typedef struct
{
    Visual *visual;
    Boolean install_colormap;
    Boolean dither_background;
} SgVisualDrawingAreaPartCompat;

typedef struct
{
    XmDrawingAreaRec drawing_area;
    SgVisualDrawingAreaPartCompat sg_visual_drawing_area;
} SgVisualDrawingAreaRecCompat;

/*
 * The original n32 class record is 56 bytes larger than XmDrawingArea's
 * exported class record. Preserve an extension-sized tail while the private
 * Sg class-part fields are recovered.
 */
typedef struct
{
    XmDrawingAreaClassRec drawing_area_class;
    XtPointer sg_class_part[14];
} SgVisualDrawingAreaClassRecCompat;

static XtResource visualDrawingAreaResources[] =
{
    {
        "visual",
        "Visual",
        XtRVisual,
        sizeof(Visual *),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.visual),
        XtRImmediate,
        (XtPointer)0
    },
    {
        "installColormap",
        "InstallColormap",
        XtRBoolean,
        sizeof(Boolean),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.install_colormap),
        XtRImmediate,
        (XtPointer)False
    },
    {
        "ditherBackground",
        "DitherBackground",
        XtRBoolean,
        sizeof(Boolean),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.dither_background),
        XtRImmediate,
        (XtPointer)False
    }
};

SgVisualDrawingAreaClassRecCompat sgVisualDrawingAreaClassRec;
WidgetClass sgVisualDrawingAreaWidgetClass =
    (WidgetClass)&sgVisualDrawingAreaClassRec;

static Boolean visualDrawingAreaClassPrepared;

static Widget findShell(Widget w)
{
    Widget shell = w;

    while (shell && !XtIsShell(shell))
        shell = XtParent(shell);

    return shell;
}

void SgVisualDrawingAreaInstallColormap(Widget w)
{
    Widget shell;
    Display *display;
    Window *windows = 0;
    int count = 0;
    int i;
    Window widget_window;
    Window shell_window;
    Window *updated;

    if (!w || !XtIsRealized(w))
        return;

    shell = findShell(w);
    if (!shell || !XtIsRealized(shell))
        return;

    display = XtDisplay(w);
    widget_window = XtWindow(w);
    shell_window = XtWindow(shell);

    if (XGetWMColormapWindows(display, shell_window, &windows, &count))
    {
        for (i = 0; i < count; ++i)
        {
            if (windows[i] == widget_window)
            {
                XFree(windows);
                return;
            }
        }
    }
    else
    {
        windows = 0;
        count = 0;
    }

    updated = (Window *)malloc((size_t)(count + 1) * sizeof(Window));
    if (!updated)
    {
        if (windows)
            XFree(windows);
        return;
    }

    updated[0] = widget_window;
    for (i = 0; i < count; ++i)
        updated[i + 1] = windows[i];

    XSetWMColormapWindows(display, shell_window, updated, count + 1);

    free(updated);
    if (windows)
        XFree(windows);
}

void SgVisualDrawingAreaUninstallColormap(Widget w)
{
    Widget shell;
    Display *display;
    Window *windows = 0;
    int count = 0;
    int i;
    int out = 0;
    Window widget_window;
    Window shell_window;
    Window *updated;

    if (!w || !XtIsRealized(w))
        return;

    shell = findShell(w);
    if (!shell || !XtIsRealized(shell))
        return;

    display = XtDisplay(w);
    widget_window = XtWindow(w);
    shell_window = XtWindow(shell);

    if (!XGetWMColormapWindows(display, shell_window, &windows, &count))
        return;

    updated = (Window *)malloc((size_t)count * sizeof(Window));
    if (!updated)
    {
        XFree(windows);
        return;
    }

    for (i = 0; i < count; ++i)
        if (windows[i] != widget_window)
            updated[out++] = windows[i];

    XSetWMColormapWindows(display, shell_window, updated, out);

    free(updated);
    XFree(windows);
}

static void visualDrawingAreaInitialize(Widget request,
                                        Widget new_widget,
                                        ArgList args,
                                        Cardinal *num_args)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)new_widget;

    (void)request;
    (void)args;
    (void)num_args;

    if (!rec->sg_visual_drawing_area.visual)
        rec->sg_visual_drawing_area.visual =
            DefaultVisualOfScreen(XtScreen(new_widget));
}

static void visualDrawingAreaRealize(Widget w,
                                     XtValueMask *value_mask,
                                     XSetWindowAttributes *attributes)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)w;
    XtRealizeProc superclass_realize =
        xmDrawingAreaClassRec.core_class.realize;

    if (superclass_realize)
        superclass_realize(w, value_mask, attributes);

    if (rec->sg_visual_drawing_area.install_colormap)
        SgVisualDrawingAreaInstallColormap(w);
}

static void visualDrawingAreaDestroy(Widget w)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)w;

    if (rec->sg_visual_drawing_area.install_colormap)
        SgVisualDrawingAreaUninstallColormap(w);
}

static Boolean visualDrawingAreaSetValues(Widget old_widget,
                                          Widget request_widget,
                                          Widget new_widget,
                                          ArgList args,
                                          Cardinal *num_args)
{
    SgVisualDrawingAreaRecCompat *old_rec =
        (SgVisualDrawingAreaRecCompat *)old_widget;
    SgVisualDrawingAreaRecCompat *new_rec =
        (SgVisualDrawingAreaRecCompat *)new_widget;

    (void)request_widget;
    (void)args;
    (void)num_args;

    if (XtIsRealized(new_widget) &&
        old_rec->sg_visual_drawing_area.install_colormap !=
        new_rec->sg_visual_drawing_area.install_colormap)
    {
        if (new_rec->sg_visual_drawing_area.install_colormap)
            SgVisualDrawingAreaInstallColormap(new_widget);
        else
            SgVisualDrawingAreaUninstallColormap(new_widget);
    }

    return False;
}

static void prepareVisualDrawingAreaClass(void)
{
    XmDrawingAreaClassRec *klass;

    if (visualDrawingAreaClassPrepared)
        return;

    memset(&sgVisualDrawingAreaClassRec, 0,
           sizeof(sgVisualDrawingAreaClassRec));

    klass = &sgVisualDrawingAreaClassRec.drawing_area_class;
    memcpy(klass, &xmDrawingAreaClassRec, sizeof(XmDrawingAreaClassRec));

    klass->core_class.superclass = (WidgetClass)&xmDrawingAreaClassRec;
    klass->core_class.class_name = "SgVisualDrawingArea";
    klass->core_class.widget_size = sizeof(SgVisualDrawingAreaRecCompat);
    klass->core_class.class_initialize = 0;
    klass->core_class.class_part_initialize = 0;
    klass->core_class.class_inited = False;
    klass->core_class.initialize = visualDrawingAreaInitialize;
    klass->core_class.initialize_hook = 0;
    klass->core_class.realize = visualDrawingAreaRealize;
    klass->core_class.actions = 0;
    klass->core_class.num_actions = 0;
    klass->core_class.resources = visualDrawingAreaResources;
    klass->core_class.num_resources = XtNumber(visualDrawingAreaResources);
    klass->core_class.xrm_class = NULLQUARK;
    klass->core_class.destroy = visualDrawingAreaDestroy;
    klass->core_class.set_values = visualDrawingAreaSetValues;

    visualDrawingAreaClassPrepared = True;
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeVisualDrawingAreaLibraryClass(void)
{
    prepareVisualDrawingAreaClass();
}

Widget SgCreateVisualDrawingArea(Widget parent,
                                 char *name,
                                 ArgList args,
                                 Cardinal count)
{
    prepareVisualDrawingAreaClass();
    return XtCreateWidget(name, sgVisualDrawingAreaWidgetClass,
                          parent, args, count);
}

#else

/* Build fallback for Motif installations without private DrawingArea headers. */
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
    return XtCreateWidget(name, sgVisualDrawingAreaWidgetClass,
                          parent, args, count);
}

void SgVisualDrawingAreaInstallColormap(Widget w)
{
    (void)w;
}

void SgVisualDrawingAreaUninstallColormap(Widget w)
{
    (void)w;
}

#endif
