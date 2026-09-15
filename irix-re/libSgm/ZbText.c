/*
 * SgZbText reconstruction.
 *
 * Source structure and private record names are taken from the original SGI
 * ZbText.h/ZbTextP.h. Function behavior is reconstructed from the N32
 * libSgm.a(ZbText.o) object and debug libSgm.so.1.
 */

#include <Sgm/ZbTextP.h>
#include <Sgm/ZoomBar.h>

#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Resize(Widget);
static void ChangeManaged(Widget);
static void DoLayout(SgZbTextWidget);

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
        XtInheritExpose,
        NULL,
        NULL,
        XtInheritSetValuesAlmost,
        NULL,
        NULL,
        XtVersion,
        NULL,
        XtInheritTranslations,
        XtInheritQueryGeometry,
        NULL,
        NULL
    },
    {
        XtInheritGeometryManager,
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
    Dimension text_h = 0;
    Dimension zoom_h = 0;
    Dimension shadow = 0;
    Dimension need_h;

    (void)request;
    (void)args;
    (void)num_args;

    zb->zbtext.zoomBar = SgCreateZoomBar(new_widget, "zoomBar", NULL, 0);
    if (zb->zbtext.zoomBar)
        XtManageChild(zb->zbtext.zoomBar);

    zb->zbtext.text = XtCreateManagedWidget(
        "text",
        xmTextFieldWidgetClass,
        new_widget, NULL, 0);

    if (zb->zbtext.zoomBar)
        XtVaGetValues(zb->zbtext.zoomBar, XmNheight, &zoom_h, NULL);
    if (zb->zbtext.text)
        XtVaGetValues(zb->zbtext.text, XmNheight, &text_h, NULL);
    XtVaGetValues(new_widget, XmNshadowThickness, &shadow, NULL);

    if (zoom_h < 6)
        zoom_h = 6;

    need_h = (Dimension)(zoom_h + text_h + 2 * shadow);
    if (need_h && zb->core.height < need_h)
        zb->core.height = need_h;
}

static void DoLayout(SgZbTextWidget zb)
{
    Dimension shadow = 0;
    Dimension inset;
    Dimension inner_w;
    Dimension zoom_h = 0;
    Dimension text_h = 0;
    Dimension zoom_bw = 0;
    Dimension text_bw = 0;

    if (!zb)
        return;

    XtVaGetValues((Widget)zb, XmNshadowThickness, &shadow, NULL);

    /*
     * core.border_width is outside the widget's window. Counting it again as
     * an internal inset shifts both children inward and narrows them twice.
     * The reconstructed Initialize() height calculation already uses only the
     * manager shadow as the internal frame, so keep Resize consistent with it.
     */
    inset = shadow;

    if (zb->core.width > (Dimension)(2 * inset))
        inner_w = (Dimension)(zb->core.width - 2 * inset);
    else
        inner_w = 1;

    if (zb->zbtext.zoomBar && XtIsManaged(zb->zbtext.zoomBar))
    {
        XtVaGetValues(zb->zbtext.zoomBar,
                      XmNheight, &zoom_h,
                      XmNborderWidth, &zoom_bw,
                      NULL);
        if (zoom_h < 6)
            zoom_h = 6;

        XtConfigureWidget(zb->zbtext.zoomBar,
                          (Position)inset, (Position)inset,
                          inner_w, zoom_h, zoom_bw);
    }

    if (zb->zbtext.text && XtIsManaged(zb->zbtext.text))
    {
        XtVaGetValues(zb->zbtext.text,
                      XmNheight, &text_h,
                      XmNborderWidth, &text_bw,
                      NULL);

        XtConfigureWidget(zb->zbtext.text,
                          (Position)inset,
                          (Position)(inset + zoom_h),
                          inner_w, text_h, text_bw);
    }
}

static void Resize(Widget w)
{
    DoLayout((SgZbTextWidget)w);
}

static void ChangeManaged(Widget w)
{
    DoLayout((SgZbTextWidget)w);
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
