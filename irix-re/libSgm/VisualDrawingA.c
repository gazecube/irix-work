/*
 * SgVisualDrawingArea reconstruction.
 *
 * Binary evidence from IRIX 6.5.0 libSgm.so.1:
 *   sgVisualDrawingAreaClassRec: 0x0b254388, n32 size 208
 *   widget_size:                0x124
 *   superclass:                 xmDrawingAreaClassRec
 *   resources:                  3
 *   resource instance offsets:  0x104, 0x108, 0x109
 *
 * The host structure below preserves recovered semantics rather than n32 byte
 * offsets. Host pointer/alignment rules are intentionally allowed to differ.
 */
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stddef.h>
#include <string.h>

#if defined(SGM_HAVE_DRAWINGAP_H)
#include <Xm/DrawingAP.h>

typedef struct
{
    /* Original public resources: n32 +0x104 .. +0x109. */
    Visual *visual;
    Boolean install_colormap;
    Boolean dither_background;

    /* Original private state: n32 +0x10c .. +0x120. */
    Widget colormap_shell;
    Boolean save_installed_colormaps;
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
 * IRIX has a 56-byte Sg class part after the inherited XmDrawingArea class
 * parts. Its hooks are not yet recovered, and all observed inherited behavior
 * needed by this widget lives in the standard Motif class parts. Reserve the
 * host equivalent without inventing semantics for those slots.
 */
typedef struct
{
    XmDrawingAreaClassRec drawing_area_class;
    XtPointer sg_class_part[14];
} SgVisualDrawingAreaClassRecCompat;

static XtResource visualDrawingAreaResources[] =
{
    {
        "visual", "Visual", XtRVisual, sizeof(Visual *),
        XtOffsetOf(SgVisualDrawingAreaRecCompat,
                   sg_visual_drawing_area.visual),
        XtRImmediate, (XtPointer)0
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
    SgVisualDrawingAreaRecCompat *rec;
    SgVisualDrawingAreaPartCompat *sg;
    Widget shell;
    Display *display;
    Window *windows = NULL;
    Window *updated = NULL;
    Window widget_window;
    Window shell_window;
    int count = 0;
    int i;

    if (!w || !XtIsRealized(w))
        return;

    rec = (SgVisualDrawingAreaRecCompat *)w;
    sg = &rec->sg_visual_drawing_area;
    shell = sg->colormap_shell ? sg->colormap_shell : findShell(w);
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
                sg->colormap_installed = True;
                return;
            }
        }
    }
    else
    {
        windows = NULL;
        count = 0;
    }

    updated = (Window *)XtMalloc((Cardinal)(count + 1) * sizeof(Window));
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
    XtFree((char *)updated);
    if (windows)
        XFree(windows);

    /*
     * Original +0x110 controls preservation/restoration of the server's
     * currently installed colormap set. Its resource name is not yet proven,
     * so the reconstruction keeps the state false unless later binary-backed
     * work identifies and populates it.
     */
    if (sg->save_installed_colormaps)
    {
        sg->saved_colormaps =
            XListInstalledColormaps(display, widget_window,
                                    &sg->saved_colormap_count);
        XInstallColormap(display, rec->drawing_area.core.colormap);
    }

    sg->colormap_installed = True;
}

void SgVisualDrawingAreaUninstallColormap(Widget w)
{
    SgVisualDrawingAreaRecCompat *rec;
    SgVisualDrawingAreaPartCompat *sg;
    Widget shell;
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
    shell = sg->colormap_shell ? sg->colormap_shell : findShell(w);

    if (XtIsRealized(w) && shell && XtIsRealized(shell))
    {
        display = XtDisplay(w);
        widget_window = XtWindow(w);
        shell_window = XtWindow(shell);

        if (XGetWMColormapWindows(display, shell_window, &windows, &count))
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
                for (i = found; i + 1 < count; ++i)
                    windows[i] = windows[i + 1];
                XSetWMColormapWindows(display, shell_window,
                                      windows, count - 1);
            }
            XFree(windows);
        }

        if (sg->save_installed_colormaps && sg->saved_colormaps)
        {
            for (i = 0; i < sg->saved_colormap_count; ++i)
                XInstallColormap(display, sg->saved_colormaps[i]);
            XFree(sg->saved_colormaps);
        }
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
    (void)args;
    (void)num_args;

    sg->colormap_shell = NULL;
    sg->save_installed_colormaps = False;
    sg->saved_colormaps = NULL;
    sg->saved_colormap_count = 0;
    sg->colormap_installed = False;
    sg->dither_color_name = NULL;

    if (!sg->visual)
        sg->visual = DefaultVisualOfScreen(XtScreen(new_widget));

    /*
     * The original dither path uses XtGetSubresources, XAllocNamedColor,
     * XAllocColor and XmGetPixmapByDepth("50_foreground"). The private
     * subresource name feeding +0x120 is not yet recovered, so deliberately
     * do not invent it. False is the original resource default and is safe.
     */
}

static void visualDrawingAreaRealize(Widget w,
                                     XtValueMask *value_mask,
                                     XSetWindowAttributes *attributes)
{
    SgVisualDrawingAreaRecCompat *rec =
        (SgVisualDrawingAreaRecCompat *)w;
    SgVisualDrawingAreaPartCompat *sg = &rec->sg_visual_drawing_area;

    if (rec->drawing_area.core.width == 0)
        rec->drawing_area.core.width = 1;
    if (rec->drawing_area.core.height == 0)
        rec->drawing_area.core.height = 1;

    /* Original code calls XtCreateWindow directly with the configured Visual. */
    XtCreateWindow(w, (unsigned int)InputOutput,
                   sg->visual ? sg->visual : CopyFromParent,
                   *value_mask, attributes);

    sg->colormap_shell = findShell(w);

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
    klass->core_class.class_initialize = NULL;
    klass->core_class.class_part_initialize = NULL;
    klass->core_class.class_inited = False;
    klass->core_class.initialize = visualDrawingAreaInitialize;
    klass->core_class.initialize_hook = NULL;
    klass->core_class.realize = visualDrawingAreaRealize;
    klass->core_class.actions = NULL;
    klass->core_class.num_actions = 0;
    klass->core_class.resources = visualDrawingAreaResources;
    klass->core_class.num_resources = XtNumber(visualDrawingAreaResources);
    klass->core_class.xrm_class = NULLQUARK;
    klass->core_class.destroy = visualDrawingAreaDestroy;

    /* Binary: set_values is NULL. Do not add policy the original did not have. */
    klass->core_class.set_values = NULL;
    klass->core_class.set_values_hook = NULL;
    klass->core_class.get_values_hook = NULL;
    klass->core_class.accept_focus = NULL;
    klass->core_class.display_accelerator = NULL;

    /*
     * CRITICAL: never share XmDrawingArea's private Motif class-extension
     * object by copying its pointer into this subclass. The IRIX class owns a
     * distinct 0x78-byte extension at 0x0b254310. Until that host ABI record is
     * reconstructed, NULL is safer and semantically honest than aliasing the
     * superclass's mutable private extension state.
     */
    klass->core_class.extension = NULL;

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

void SgVisualDrawingAreaInstallColormap(Widget w) { (void)w; }
void SgVisualDrawingAreaUninstallColormap(Widget w) { (void)w; }

#endif
