/*
 * SgZoomBar behavioral reconstruction for SgFinder.
 *
 * SGI documents the Finder ZoomBar as the path-navigation bar: it contains
 * one button for each section of the finder text, and selecting a button asks
 * the Finder's SgNsetTextSectionFunc to select that section.  The exact IRIX
 * private widget record and rendering code have not yet been recovered, so
 * this file implements only that documented surface on the host ABI.
 */
#include <Sgm/Finder.h>

#include <Xm/PushB.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

WidgetClass sgZoomBarWidgetClass = NULL;

#if defined(SGM_HAVE_MANAGERP_H)
#include <Xm/ManagerP.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

#define ZOOMBAR_MAX_SECTIONS 64

typedef struct
{
    /* Host reconstruction state; not claimed to match IRIX n32 offsets. */
    Widget finder;
    Widget buttons[ZOOMBAR_MAX_SECTIONS];
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
static void zoomBarChangeManaged(Widget);
static void zoomBarFinderChangedCB(Widget, XtPointer, XtPointer);
static void zoomBarButtonCB(Widget, XtPointer, XtPointer);
static void zoomBarRebuild(SgZoomBarRecCompat *);
static void zoomBarLayout(SgZoomBarRecCompat *);

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
        NULL,
        0,
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        zoomBarDestroy,
        zoomBarResize,
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
    char *label_text;
    XmString label;
    Widget button;

    if (!rec || !text || rec->zoom_bar.button_count >= ZOOMBAR_MAX_SECTIONS)
        return;

    label_text = (char *)XtMalloc((Cardinal)length + 1);
    if (!label_text)
        return;
    memcpy(label_text, text, length);
    label_text[length] = '\0';

    sprintf(name, "section%u", (unsigned)section);
    label = XmStringCreateLocalized(label_text);
    XtFree(label_text);

    button = XtVaCreateManagedWidget(
        name,
        xmPushButtonWidgetClass,
        (Widget)rec,
        XmNlabelString, label,
        XmNmarginWidth, 3,
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

    if (!rec || !rec->zoom_bar.finder)
        return;

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

    /* Absolute path: SGI numbers ZoomBar sections from left to right.  The
     * leading separator is therefore section zero, representing the root. */
    if (*p == separator)
    {
        root_label[0] = separator;
        root_label[1] = '\0';
        zoomBarAddButton(rec, root_label, 1, section++);
        ++p;
    }

    while (*p && section < ZOOMBAR_MAX_SECTIONS)
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

static void zoomBarLayout(SgZoomBarRecCompat *rec)
{
    Cardinal i;
    Position x;
    Dimension available;
    Dimension height;

    if (!rec)
        return;

    x = 0;
    available = rec->core.width ? rec->core.width : 1;
    height = rec->core.height ? rec->core.height : 1;

    for (i = 0; i < rec->zoom_bar.button_count; ++i)
    {
        Widget child = rec->zoom_bar.buttons[i];
        XtWidgetGeometry preferred;
        Dimension width;

        if (!child || !XtIsManaged(child) || (Dimension)x >= available)
            continue;

        memset(&preferred, 0, sizeof(preferred));
        XtQueryGeometry(child, NULL, &preferred);
        width = (preferred.request_mode & CWWidth) ? preferred.width : 24;
        if (width < 12)
            width = 12;
        if ((Dimension)x + width > available)
            width = available - (Dimension)x;
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

    rec->zoom_bar.finder = XtParent(new_widget);
    rec->zoom_bar.button_count = 0;
    for (i = 0; i < ZOOMBAR_MAX_SECTIONS; ++i)
        rec->zoom_bar.buttons[i] = NULL;

    if (rec->zoom_bar.finder)
        XtAddCallback(rec->zoom_bar.finder,
                      XmNvalueChangedCallback,
                      zoomBarFinderChangedCB,
                      (XtPointer)new_widget);

    zoomBarRebuild(rec);
}

static void zoomBarDestroy(Widget widget)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)widget;

    if (rec->zoom_bar.finder)
        XtRemoveCallback(rec->zoom_bar.finder,
                         XmNvalueChangedCallback,
                         zoomBarFinderChangedCB,
                         (XtPointer)widget);

    /* Xt owns and destroys child widgets recursively. */
    rec->zoom_bar.finder = NULL;
    rec->zoom_bar.button_count = 0;
}

static void zoomBarResize(Widget widget)
{
    zoomBarLayout((SgZoomBarRecCompat *)widget);
}

static void zoomBarChangeManaged(Widget widget)
{
    zoomBarLayout((SgZoomBarRecCompat *)widget);
}

static void zoomBarFinderChangedCB(Widget finder,
                                   XtPointer client_data,
                                   XtPointer call_data)
{
    SgZoomBarRecCompat *rec = (SgZoomBarRecCompat *)client_data;
    (void)finder;
    (void)call_data;
    zoomBarRebuild(rec);
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

/* Build-only fallback when private Motif Manager headers are unavailable. */
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
