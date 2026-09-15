/*
 * SgZbText reconstruction.
 *
 * Source structure and private record names are taken from the original SGI
 * ZbText.h/ZbTextP.h. Behaviour below is reconstructed from the N32
 * libSgm.a(ZbText.o) object and debug libSgm.so.1.
 */

#include <Sgm/ZbTextP.h>
#include <Sgm/ZoomBar.h>

#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <dlfcn.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

/* Motif-private entry point used by the original ZbText.o fallback path. */
typedef void (*XmDrawShadowsProc)(Display *, Drawable, GC, GC,
                                  Position, Position,
                                  Dimension, Dimension,
                                  Dimension, unsigned int);

static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Resize(Widget);
static void DoLayout(SgZbTextWidget);
static void PreferredSize(SgZbTextWidget, Dimension *, Dimension *);
static XtGeometryResult QueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static XtGeometryResult GeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static void ChangeManaged(Widget);
static void Redisplay(Widget, XEvent *, Region);

SgZbTextClassRec sgZbTextClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgZbText",
        sizeof(SgZbTextRec),
        NULL,
        NULL,
        False,
        Initialize,
        NULL,
        XtInheritRealize,
        NULL,
        0,
        NULL,
        0,
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        NULL,
        Resize,
        Redisplay,
        NULL,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        XtInheritTranslations,
        QueryGeometry,
        NULL,
        NULL
    },
    {
        GeometryManager,
        ChangeManaged,
        XtInheritInsertChild,
        XtInheritDeleteChild,
        NULL
    },
    {
        NULL,
        0,
        sizeof(XmManagerConstraintRec),
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
        XmInheritParentProcess,
        NULL
    },
    { 0 }
};

WidgetClass sgZbTextWidgetClass = (WidgetClass)&sgZbTextClassRec;

static void Initialize(Widget request, Widget new_widget,
                       ArgList args, Cardinal *num_args)
{
    SgZbTextWidget zb = (SgZbTextWidget)new_widget;
    Widget children[2];

    (void)args;
    (void)num_args;

    /* Exact ZbText.o constructor geometry: unspecified dimensions become 5. */
    if (request->core.width == 0)
        zb->core.width = 5;
    if (request->core.height == 0)
        zb->core.height = 5;

    /*
     * ZbText.o creates both children with XtVaCreateWidget() and manages them
     * together.  The object passes zero border/shadow values to the ZoomBar,
     * and zero internal decoration values to the text field; these are the
     * host spellings of the FAST_QUARK resource arguments recovered from the
     * call sites.  postMotionCall is the literal third text-field resource in
     * the original object.
     */
    zb->zbtext.zoomBar = XtVaCreateWidget(
        "zoomBar", sgZoomBarWidgetClass, new_widget,
        XtNborderWidth, 0,
        XmNshadowThickness, 0,
        NULL);

    zb->zbtext.text = XtVaCreateWidget(
        "text", xmTextFieldWidgetClass, new_widget,
        XmNmarginHeight, 0,
        XmNshadowThickness, 0,
        "postMotionCall", True,
        NULL);

    children[0] = zb->zbtext.zoomBar;
    children[1] = zb->zbtext.text;
    if (children[0] && children[1])
        XtManageChildren(children, 2);
    else
    {
        if (children[0]) XtManageChild(children[0]);
        if (children[1]) XtManageChild(children[1]);
    }
}

static void DoLayout(SgZbTextWidget zb)
{
    Dimension zoom_h;
    Dimension inset;
    Dimension width;

    if (!zb || !zb->zbtext.zoomBar || !zb->zbtext.text)
        return;

    /*
     * Exact ZbText.o DoLayout math:
     *   inset = manager.shadow_thickness + core.border_width
     *   child width = core.width - 2*shadow - 2*border
     *   ZoomBar height is clamped to at least six pixels.
     */
    zoom_h = zb->zbtext.zoomBar->core.height;
    if (zoom_h < 6)
        zoom_h = 6;

    inset = (Dimension)(zb->manager.shadow_thickness + zb->core.border_width);

    if (zb->core.width > (Dimension)(2 * zb->manager.shadow_thickness +
                                     2 * zb->core.border_width))
        width = (Dimension)(zb->core.width -
                            2 * zb->manager.shadow_thickness -
                            2 * zb->core.border_width);
    else
        width = 1;

    XtConfigureWidget(zb->zbtext.zoomBar,
                      (Position)inset,
                      (Position)inset,
                      width,
                      zoom_h,
                      zb->zbtext.zoomBar->core.border_width);

    XtConfigureWidget(zb->zbtext.text,
                      (Position)inset,
                      (Position)(inset + zoom_h),
                      width,
                      zb->zbtext.text->core.height,
                      zb->zbtext.text->core.border_width);
}

static void Resize(Widget w)
{
    DoLayout((SgZbTextWidget)w);
}

static void PreferredSize(SgZbTextWidget zb,
                          Dimension *width_return,
                          Dimension *height_return)
{
    Dimension width = 0;
    Dimension height = 0;
    Dimension margin_h = 0;
    Cardinal i;

    if (width_return) *width_return = 0;
    if (height_return) *height_return = 0;
    if (!zb)
        return;

    /* Exact object structure: maximum child width, sum of child heights. */
    for (i = 0; i < zb->composite.num_children; ++i)
    {
        Widget child = zb->composite.children[i];
        if (!child)
            continue;
        if (child->core.width > width)
            width = child->core.width;
        height = (Dimension)(height + child->core.height);
    }

    /* ZbText.o reads one vertical text-field margin before adding the frame. */
    if (zb->zbtext.text)
        XtVaGetValues(zb->zbtext.text, XmNmarginHeight, &margin_h, NULL);

    height = (Dimension)(height +
                         2 * (zb->manager.shadow_thickness + margin_h));

    if (width_return) *width_return = width;
    if (height_return) *height_return = height;
}

static XtGeometryResult QueryGeometry(Widget widget,
                                      XtWidgetGeometry *intended,
                                      XtWidgetGeometry *preferred)
{
    Dimension width = 0;
    Dimension height = 0;

    if (!preferred)
        return XtGeometryNo;

    PreferredSize((SgZbTextWidget)widget, &width, &height);
    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = width;
    preferred->height = height;

    if (!intended)
        return XtGeometryAlmost;

    if ((intended->request_mode & (CWWidth | CWHeight)) ==
        (CWWidth | CWHeight) &&
        intended->width == width && intended->height == height)
        return XtGeometryYes;

    if (widget->core.width == width && widget->core.height == height)
        return XtGeometryNo;

    return XtGeometryAlmost;
}

static XtGeometryResult GeometryManager(Widget child,
                                        XtWidgetGeometry *request,
                                        XtWidgetGeometry *reply)
{
    SgZbTextWidget zb;
    (void)reply;

    if (!child || !request)
        return XtGeometryNo;

    zb = (SgZbTextWidget)XtParent(child);

    /* Original rejects child x/y moves outright. */
    if (request->request_mode & (CWX | CWY))
        return XtGeometryNo;

    if (request->request_mode & CWWidth)
        child->core.width = request->width;
    if (request->request_mode & CWHeight)
        child->core.height = request->height;

    if (request->request_mode & (CWWidth | CWHeight))
        ChangeManaged((Widget)zb);

    return XtGeometryYes;
}

static void ChangeManaged(Widget w)
{
    SgZbTextWidget zb = (SgZbTextWidget)w;
    Dimension width = 0;
    Dimension height = 0;

    PreferredSize(zb, &width, &height);

    if (zb->core.width < width || zb->core.height < height)
    {
        Dimension reply_w = 0;
        Dimension reply_h = 0;
        XtGeometryResult result =
            XtMakeResizeRequest(w, width, height, &reply_w, &reply_h);

        if (result == XtGeometryAlmost)
            (void)XtMakeResizeRequest(w, reply_w, reply_h, NULL, NULL);
    }

    DoLayout(zb);
}

static void Redisplay(Widget widget, XEvent *event, Region region)
{
    SgZbTextWidget zb = (SgZbTextWidget)widget;
    Dimension child_w = 0;
    Dimension child_h = 0;
    Dimension draw_w;
    Dimension draw_h;
    Cardinal i;
    static XmDrawShadowsProc draw_shadows;
    static Boolean looked_up;

    (void)event;
    (void)region;

    if (!zb || !XtIsRealized(widget) || !XtWindow(widget))
        return;

    /* Redisplay computes its frame from the children, not core.width/height. */
    for (i = 0; i < zb->composite.num_children; ++i)
    {
        Widget child = zb->composite.children[i];
        if (!child)
            continue;
        if (child->core.width > child_w)
            child_w = child->core.width;
        child_h = (Dimension)(child_h + child->core.height);
    }

    draw_w = (Dimension)(child_w +
                         2 * zb->manager.shadow_thickness +
                         2 * zb->core.border_width);
    draw_h = (Dimension)(child_h +
                         2 * zb->manager.shadow_thickness +
                         2 * zb->core.border_width);

    if (!zb->manager.shadow_thickness)
        return;

    if (!looked_up)
    {
        draw_shadows = (XmDrawShadowsProc)dlsym(RTLD_DEFAULT, "_XmDrawShadows");
        looked_up = True;
    }

    if (draw_shadows)
    {
        /*
         * Exact non-SGImode call shape from ZbText.o: the two manager shadow
         * GCs are intentionally reversed and shadow style 8 is passed.
         */
        draw_shadows(XtDisplay(widget), XtWindow(widget),
                     zb->manager.bottom_shadow_GC,
                     zb->manager.top_shadow_GC,
                     0, 0,
                     draw_w, draw_h,
                     zb->manager.shadow_thickness,
                     8U);
    }
}

Widget SgCreateZbText(Widget parent, char *name,
                      ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgZbTextWidgetClass,
                          parent, args, count);
}

Widget SgZbTextGetChild(Widget zb_text, int which)
{
    SgZbTextWidget zb = (SgZbTextWidget)zb_text;

    if (!zb_text || !XtIsSubclass(zb_text, sgZbTextWidgetClass))
        return NULL;

    if (which == SgZBTEXT_TEXT)
        return zb->zbtext.text;
    if (which == SgZBTEXT_ZOOM_BAR)
        return zb->zbtext.zoomBar;
    return NULL;
}
