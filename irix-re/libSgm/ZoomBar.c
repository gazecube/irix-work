/*
 * SgZoomBar reconstruction for SgFinder.
 *
 * IRIX libSgm.so.1 exposes sectionCount and sectionWidths resources on
 * SgZoomBar. Finder computes cumulative pixel positions from the real text
 * field and pushes those resources into the bar.
 *
 * Binary evidence also shows that the original SgZoomBar is an XmPrimitive
 * subclass, not a Manager containing labelled button children. The host
 * implementation still uses Motif push buttons temporarily for interaction,
 * but they are intentionally label-less and their geometry is driven by the
 * binary-confirmed section positions. The visible path text belongs only to
 * the text field below the bar.
 */
#include <Sgm/Finder.h>

#include <Xm/PushB.h>
#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WidgetClass sgZoomBarWidgetClass = NULL;

#if defined(SGM_HAVE_MANAGERP_H)
#include <Xm/ManagerP.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

#define ZOOMBAR_MAX_SECTIONS 255

typedef struct
{
    int section_count;
    XtPointer section_widths;
    Widget finder;
    Widget text;
    Widget buttons[ZOOMBAR_MAX_SECTIONS + 1];
    Cardinal button_count;
} SgZoomBarPartCompat;

typedef struct
{
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgZoomBarPartCompat zoom_bar;
} SgZoomBarRecCompat;

typedef struct
{
    XtPointer extension;
} SgZoomBarClassPartCompat;

typedef struct
{
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgZoomBarClassPartCompat zoom_bar_class;
} SgZoomBarClassRecCompat;

static void zoomBarInitialize(Widget, Widget, ArgList, Cardinal *);
static void zoomBarDestroy(Widget);
static void zoomBarResize(Widget);
static Boolean zoomBarSetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static void zoomBarChangeManaged(Widget);
static void zoomBarButtonCB(Widget, XtPointer, XtPointer);
static void zoomBarRebuild(SgZoomBarRecCompat *);
static void zoomBarLayout(SgZoomBarRecCompat *);

static XtResource zoomBarResources[] =
{
    {
        SgNsectionWidths, "SectionWidths", XtRPointer,
        sizeof(XtPointer),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.section_widths),
        XtRImmediate, (XtPointer)NULL
    },
    {
        SgNsectionCount, "SectionCount", XtRInt,
        sizeof(int),
        XtOffsetOf(SgZoomBarRecCompat, zoom_bar.section_count),
        XtRImmediate, (XtPointer)0
    }
};

static SgZoomBarClassRecCompat sgZoomBarClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgZoomBar",
        sizeof(SgZoomBarRecCompat),
        NULL,
        NULL,
        False,
        zoomBarInitialize,
        NULL,
        XtInheritRealize,
        NULL,
        0,
        zoomBarResources,
        XtNumber(zoomBarResources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        zoomBarDestroy,
        zoomBarResize,
        XtInheritExpose,
        zoomBarSetValues,
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
        zoomBarChangeManaged,
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

static Widget findFinderAncestor(Widget w)
{
    Widget p = w;
    while (p)
    {
        if (sgFinderWidgetClass && XtIsSubclass(p, sgFinderWidgetClass))
            return p;
        p = XtParent(p);
    }
    return NULL;
}

static void zoomBarClear(SgZoomBarRecCompat *rec)
{
    Cardinal i;
    if (!rec)
        return;
    for (i = 0; i < rec->zoom_bar.button_count; ++i)
    {
        if (rec->zoom_bar.buttons[i])
            XtDestroyWidget(rec->zoom_bar.buttons[i]);
        rec->zoom_bar.buttons[i] = NULL;
    }
    rec->zoom_bar.button_count = 0;
}

static void zoomBarAddButton(SgZoomBarRecCompat *rec,
                             const char *text,
                             size_t length,
                             Cardinal section)
{
    char name[32];
    XmString label;
    Widget button;

    if (!rec || !text ||
        rec->zoom_bar.button_count >= ZOOMBAR_MAX_SECTIONS + 1)
        return;

    (void)length;

    sprintf(name, "section%u", (unsigned)section);

    /*
     * The original SgZoomBar is a single XmPrimitive. It therefore cannot
     * contain per-section labelled widgets. Path text is used by Finder only
     * to calculate section widths; the ZoomBar itself renders the section
     * controls without duplicating those labels.
     *
     * Until the primitive renderer/action code is restored exactly, keep the
     * current host push-button interaction seam but make the children blank.
     */
    label = XmStringCreateLocalized("");

    button = XtVaCreateManagedWidget(
        name,
        xmPushButtonWidgetClass,
        (Widget)rec,
        XmNlabelString, label,
        XmNmarginWidth, 0,
        XmNmarginHeight, 0,
        XmNshadowThickness, 1,
        XmNhighlightThickness, 0,
        XmNuserData, (XtPointer)(uintptr_t)section,
        NULL);
    XmStringFree(label);

    XtAddCallback(button, XmNactivateCallback,
                  zoomBarButtonCB, (XtPointer)rec);
    rec->zoom_bar.buttons[rec->zoom_bar.button_count++] = button;
}

static void zoomBarRebuild(SgZoomBarRecCompat *rec)
{
    char *value;
    char separator;
    char root_label[2];
    const char *p;
    const char *start;
    Cardinal section;

    if (!rec)
        return;

    if (!rec->zoom_bar.finder)
        rec->zoom_bar.finder = findFinderAncestor((Widget)rec);
    if (rec->zoom_bar.finder && !rec->zoom_bar.text)
        rec->zoom_bar.text =
            SgFinderGetChild(rec->zoom_bar.finder, SgFINDER_TEXT);

    if (!rec->zoom_bar.finder || !rec->zoom_bar.text)
    {
        zoomBarClear(rec);
        return;
    }

    value = SgFinderGetTextString(rec->zoom_bar.finder);
    zoomBarClear(rec);
    if (!value || !*value)
    {
        if (value)
            XtFree(value);
        zoomBarLayout(rec);
        return;
    }

    separator = '/';
    XtVaGetValues(rec->zoom_bar.finder,
                  SgNseparator, &separator,
                  NULL);

    p = value;
    section = 0;
    if (*p == separator)
    {
        root_label[0] = separator;
        root_label[1] = '\0';
        zoomBarAddButton(rec, root_label, 1, section++);
        ++p;
    }

    while (*p && section < ZOOMBAR_MAX_SECTIONS + 1)
    {
        while (*p == separator)
            ++p;
        if (!*p)
            break;
        start = p;
        while (*p && *p != separator)
            ++p;
        if (p > start)
            zoomBarAddButton(rec, start, (size_t)(p - start), section++);
    }

    XtFree(value);
    zoomBarLayout(rec);
}

static Dimension fallbackButtonWidth(Widget child)
{
    XtWidgetGeometry preferred;
    Dimension width = 1;
    memset(&preferred, 0, sizeof(preferred));
    XtQueryGeometry(child, NULL, &preferred);
    if (preferred.request_mode & CWWidth)
        width = preferred.width;
    else
        XtVaGetValues(child, XmNwidth, &width, NULL);
    return width ? width : 1;
}

static void zoomBarLayout(SgZoomBarRecCompat *rec)
{
    Cardinal i;
    Position x;
    Dimension available;
    Dimension height;
    unsigned short *bounds;
    int bound_count;
    Position last_text_x = -1;

    if (!rec)
        return;

    x = 0;
    available = rec->core.width ? rec->core.width : 1;
    height = rec->core.height ? rec->core.height : 1;
    bounds = (unsigned short *)rec->zoom_bar.section_widths;
    bound_count = rec->zoom_bar.section_count;

    if (rec->zoom_bar.text && rec->zoom_bar.button_count)
    {
        char *value = XmTextFieldGetString(rec->zoom_bar.text);
        if (value)
        {
            Position y;
            (void)XmTextFieldPosToXY(rec->zoom_bar.text,
                                     (XmTextPosition)strlen(value),
                                     &last_text_x, &y);
            XtFree(value);
        }
    }

    for (i = 0; i < rec->zoom_bar.button_count; ++i)
    {
        Widget child = rec->zoom_bar.buttons[i];
        Dimension width;
        Position endpoint = -1;

        if (!child || !XtIsManaged(child) || (Dimension)x >= available)
            continue;

        if (bounds && (int)i < bound_count)
            endpoint = (Position)bounds[i];
        else if (i + 1 == rec->zoom_bar.button_count && last_text_x >= 0)
            endpoint = last_text_x;

        if (endpoint > x)
            width = (Dimension)(endpoint - x);
        else
            width = fallbackButtonWidth(child);

        if ((Dimension)x + width > available)
            width = (Dimension)(available - (Dimension)x);
        if (width < 1)
            width = 1;

        XtConfigureWidget(child, x, 0, width, height, 0);
        x = (Position)(x + width);
    }
}

static void zoomBarInitialize(Widget request, Widget new_widget,
                              ArgList args, Cardinal *num_args)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)new_widget;
    Cardinal i;
    (void)request;
    (void)args;
    (void)num_args;

    rec->zoom_bar.finder = findFinderAncestor(new_widget);
    rec->zoom_bar.text = NULL;
    rec->zoom_bar.button_count = 0;
    for (i = 0; i < ZOOMBAR_MAX_SECTIONS + 1; ++i)
        rec->zoom_bar.buttons[i] = NULL;

    if (rec->zoom_bar.finder)
        rec->zoom_bar.text =
            SgFinderGetChild(rec->zoom_bar.finder, SgFINDER_TEXT);
}

static void zoomBarDestroy(Widget widget)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;
    rec->zoom_bar.text = NULL;
    rec->zoom_bar.finder = NULL;
    rec->zoom_bar.button_count = 0;
}

static void zoomBarResize(Widget widget)
{
    zoomBarLayout((SgZoomBarRecCompat *)widget);
}

static Boolean zoomBarSetValues(Widget old_widget,
                                Widget request_widget,
                                Widget new_widget,
                                ArgList args,
                                Cardinal *num_args)
{
    SgZoomBarRecCompat *newrec = (SgZoomBarRecCompat *)new_widget;
    (void)old_widget;
    (void)request_widget;
    (void)args;
    (void)num_args;

    /* Finder rewrites the same sectionWidths array in place. */
    zoomBarRebuild(newrec);
    return True;
}

static void zoomBarChangeManaged(Widget widget)
{
    zoomBarLayout((SgZoomBarRecCompat *)widget);
}

static void zoomBarButtonCB(Widget button,
                            XtPointer client_data,
                            XtPointer call_data)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)client_data;
    XtPointer user_data = NULL;
    SgSetTextFunc set_section = NULL;
    int section;

    (void)call_data;
    if (!rec || !rec->zoom_bar.finder)
        return;

    XtVaGetValues(button, XmNuserData, &user_data, NULL);
    section = (int)(uintptr_t)user_data;
    XtVaGetValues(rec->zoom_bar.finder,
                  SgNsetTextSectionFunc, &set_section,
                  NULL);
    if (set_section)
        set_section(rec->zoom_bar.finder, section);
}

Widget SgCreateZoomBar(Widget parent, char *name,
                       ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgZoomBarWidgetClass,
                          parent, args, count);
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeZoomBarClass(void)
{
    sgZoomBarWidgetClass = (WidgetClass)&sgZoomBarClassRec;
}

#else

#include <Xm/RowColumn.h>

Widget SgCreateZoomBar(Widget parent, char *name,
                       ArgList args, Cardinal count)
{
    if (!sgZoomBarWidgetClass)
        sgZoomBarWidgetClass = xmRowColumnWidgetClass;
    return XtCreateWidget(name, sgZoomBarWidgetClass,
                          parent, args, count);
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeZoomBarFallback(void)
{
    sgZoomBarWidgetClass = xmRowColumnWidgetClass;
}

#endif
