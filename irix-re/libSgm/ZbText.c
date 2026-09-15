/*
 * SgZbText reconstruction.
 *
 * Binary evidence from IRIX libSgm.so.1:
 *   - sgZbTextWidgetClass and SgCreateZbText are exported.
 *   - SgZbTextGetChild(zb, 0) returns the text child.
 *   - SgZbTextGetChild(zb, 1) returns the SgZoomBar child.
 *   - the private record keeps the ZoomBar before the text child and Resize()
 *     lays the ZoomBar above the text field.
 *
 * The host record layout below is not an n32 ABI claim; the child ownership
 * and geometry are reconstructed from the original MIPS implementation.
 */
#include <Sgm/Finder.h>

#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <string.h>

WidgetClass sgZbTextWidgetClass = NULL;

#if defined(SGM_HAVE_MANAGERP_H)
#include <Xm/ManagerP.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

typedef struct
{
    Widget zoom_bar;
    Widget text;
} SgZbTextPartCompat;

typedef struct
{
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgZbTextPartCompat zb_text;
} SgZbTextRecCompat;

typedef struct
{
    XtPointer extension;
} SgZbTextClassPartCompat;

typedef struct
{
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgZbTextClassPartCompat zb_text_class;
} SgZbTextClassRecCompat;

static void zbTextInitialize(Widget, Widget, ArgList, Cardinal *);
static void zbTextResize(Widget);
static void zbTextChangeManaged(Widget);
static void zbTextLayout(SgZbTextRecCompat *);

SgZbTextClassRecCompat sgZbTextClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgZbText",
        sizeof(SgZbTextRecCompat),
        NULL,
        NULL,
        False,
        zbTextInitialize,
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
        zbTextResize,
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
        zbTextChangeManaged,
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
    {
        NULL
    }
};

static void zbTextInitialize(Widget request, Widget new_widget,
                             ArgList args, Cardinal *num_args)
{
    SgZbTextRecCompat *rec = (SgZbTextRecCompat *)new_widget;
    Dimension text_h = 0;
    Dimension zoom_h = 0;
    Dimension shadow = 0;
    Dimension need_h;

    (void)request;
    (void)args;
    (void)num_args;

    /*
     * Original private order, confirmed by SgZbTextGetChild:
     *   +0xec -> ZoomBar (selector 1)
     *   +0xf0 -> text    (selector 0)
     */
    rec->zb_text.zoom_bar = SgCreateZoomBar(new_widget, "zoomBar", NULL, 0);
    if (rec->zb_text.zoom_bar)
        XtManageChild(rec->zb_text.zoom_bar);

    rec->zb_text.text = XtCreateManagedWidget(
        "text",
        xmTextFieldWidgetClass,
        new_widget, NULL, 0);

    if (rec->zb_text.zoom_bar)
        XtVaGetValues(rec->zb_text.zoom_bar, XmNheight, &zoom_h, NULL);
    if (rec->zb_text.text)
        XtVaGetValues(rec->zb_text.text, XmNheight, &text_h, NULL);
    XtVaGetValues(new_widget, XmNshadowThickness, &shadow, NULL);

    if (zoom_h < 6)
        zoom_h = 6;

    need_h = (Dimension)(zoom_h + text_h + 2 * shadow);
    if (need_h && rec->core.height < need_h)
        rec->core.height = need_h;
}

static void zbTextLayout(SgZbTextRecCompat *rec)
{
    Dimension shadow = 0;
    Dimension inset;
    Dimension inner_w;
    Dimension zoom_h = 0;
    Dimension text_h = 0;
    Dimension zoom_bw = 0;
    Dimension text_bw = 0;

    if (!rec)
        return;

    XtVaGetValues((Widget)rec, XmNshadowThickness, &shadow, NULL);
    inset = (Dimension)(shadow + rec->core.border_width);

    if (rec->core.width > (Dimension)(2 * inset))
        inner_w = (Dimension)(rec->core.width - 2 * inset);
    else
        inner_w = 1;

    if (rec->zb_text.zoom_bar && XtIsManaged(rec->zb_text.zoom_bar))
    {
        XtVaGetValues(rec->zb_text.zoom_bar,
                      XmNheight, &zoom_h,
                      XmNborderWidth, &zoom_bw,
                      NULL);
        if (zoom_h < 6)
            zoom_h = 6;

        XtConfigureWidget(rec->zb_text.zoom_bar,
                          (Position)inset, (Position)inset,
                          inner_w, zoom_h, zoom_bw);
    }

    if (rec->zb_text.text && XtIsManaged(rec->zb_text.text))
    {
        XtVaGetValues(rec->zb_text.text,
                      XmNheight, &text_h,
                      XmNborderWidth, &text_bw,
                      NULL);

        XtConfigureWidget(rec->zb_text.text,
                          (Position)inset,
                          (Position)(inset + zoom_h),
                          inner_w, text_h, text_bw);
    }
}

static void zbTextResize(Widget w)
{
    zbTextLayout((SgZbTextRecCompat *)w);
}

static void zbTextChangeManaged(Widget w)
{
    zbTextLayout((SgZbTextRecCompat *)w);
}

Widget SgCreateZbText(Widget parent, char *name,
                      ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgZbTextWidgetClass,
                          parent, args, count);
}

Widget SgZbTextGetChild(Widget zb_text, int which)
{
    SgZbTextRecCompat *rec = (SgZbTextRecCompat *)zb_text;

    if (!zb_text || !XtIsSubclass(zb_text, sgZbTextWidgetClass))
        return NULL;

    if (which == SgZBTEXT_TEXT)
        return rec->zb_text.text;
    if (which == SgZBTEXT_ZOOM_BAR)
        return rec->zb_text.zoom_bar;
    return NULL;
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeZbTextClass(void)
{
    sgZbTextWidgetClass = (WidgetClass)&sgZbTextClassRec;
}

#else

#include <Xm/RowColumn.h>

typedef struct ZbTextFallbackState
{
    Widget widget;
    Widget text;
    Widget zoom_bar;
    struct ZbTextFallbackState *next;
} ZbTextFallbackState;

static ZbTextFallbackState *fallbackStates;

static ZbTextFallbackState *fallbackState(Widget w, Boolean create)
{
    ZbTextFallbackState *s;

    for (s = fallbackStates; s; s = s->next)
        if (s->widget == w)
            return s;

    if (!create || !w)
        return NULL;

    s = (ZbTextFallbackState *)XtCalloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->widget = w;
    s->zoom_bar = SgCreateZoomBar(w, "zoomBar", NULL, 0);
    if (s->zoom_bar)
        XtManageChild(s->zoom_bar);
    s->text = XmCreateTextField(w, "text", NULL, 0);
    XtManageChild(s->text);
    s->next = fallbackStates;
    fallbackStates = s;
    return s;
}

Widget SgCreateZbText(Widget parent, char *name,
                      ArgList args, Cardinal count)
{
    Widget w;

    if (!sgZbTextWidgetClass)
        sgZbTextWidgetClass = xmRowColumnWidgetClass;

    w = XtCreateWidget(name, sgZbTextWidgetClass, parent, args, count);
    (void)fallbackState(w, True);
    return w;
}

Widget SgZbTextGetChild(Widget zb_text, int which)
{
    ZbTextFallbackState *s = fallbackState(zb_text, True);
    if (!s)
        return NULL;
    return which == SgZBTEXT_TEXT ? s->text :
           which == SgZBTEXT_ZOOM_BAR ? s->zoom_bar : NULL;
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeZbTextFallback(void)
{
    sgZbTextWidgetClass = xmRowColumnWidgetClass;
}

#endif
