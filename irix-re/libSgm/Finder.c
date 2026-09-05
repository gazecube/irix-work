/*
 * SgFinder compatibility reconstruction.
 *
 * Public behavior in this file is based on SGI SgFinder documentation and
 * the libSgm exported API. The exact IRIX 6.5.0 private instance/class layout
 * is still awaiting binary/header recovery, so host-only fields and child
 * names are explicitly reconstruction details rather than ABI claims.
 */
#include <Sgm/Finder.h>

#include <Xm/DrawingA.h>
#include <Xm/Scale.h>
#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <stdlib.h>
#include <string.h>

extern WidgetClass sgDropPocketWidgetClass;
extern WidgetClass sgDynaMenuWidgetClass;
extern WidgetClass sgZoomBarWidgetClass;

#if defined(SGM_HAVE_MANAGERP_H)
#include <Xm/ManagerP.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

typedef struct
{
    XtCallbackList activate_callback;
    XtCallbackList value_changed_callback;
    Pixmap history_pixmap;
    unsigned char separator;
    XtPointer set_text_section_func;
    Boolean use_drop_pocket;
    Boolean use_history_menu;

    /* Host reconstruction state; not claimed to match n32 offsets. */
    Widget drop_pocket;
    Widget text;
    Widget zoom_bar;
    Widget history_menubar;
} SgFinderPartCompat;

typedef struct
{
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgFinderPartCompat finder;
} SgFinderRecCompat;

typedef struct
{
    XtPointer extension;
} SgFinderClassPartCompat;

typedef struct
{
    CoreClassPart core_class;
    CompositeClassPart composite_class;
    ConstraintClassPart constraint_class;
    XmManagerClassPart manager_class;
    SgFinderClassPartCompat finder_class;
} SgFinderClassRecCompat;

static void finderInitialize(Widget, Widget, ArgList, Cardinal *);
static void finderDestroy(Widget);
static void finderResize(Widget);
static void finderChangeManaged(Widget);
static void textActivateCB(Widget, XtPointer, XtPointer);
static void textValueChangedCB(Widget, XtPointer, XtPointer);

static void defaultSetTextSection(Widget finder, int section)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)finder;
    char *value;
    char *p;
    int current;

    if (!rec || !rec->finder.text || section < 0)
        return;

    value = XmTextFieldGetString(rec->finder.text);
    if (!value)
        return;

    p = value;
    current = 0;
    while (*p && current <= section)
    {
        if ((unsigned char)*p == rec->finder.separator)
        {
            if (current == section)
            {
                p[1] = '\0';
                break;
            }
            ++current;
        }
        ++p;
    }

    SgFinderSetTextString(finder, value);
    XtFree(value);
}

static XtResource finderResources[] =
{
    {
        XmNactivateCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgFinderRecCompat, finder.activate_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        XmNvalueChangedCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgFinderRecCompat, finder.value_changed_callback),
        XtRCallback, (XtPointer)NULL
    },
    {
        SgNhistoryPixmap, "HistoryPixmap", XtRPixmap,
        sizeof(Pixmap),
        XtOffsetOf(SgFinderRecCompat, finder.history_pixmap),
        XtRImmediate, (XtPointer)XmUNSPECIFIED_PIXMAP
    },
    {
        SgNseparator, "Separator", XtRUnsignedChar,
        sizeof(unsigned char),
        XtOffsetOf(SgFinderRecCompat, finder.separator),
        XtRImmediate, (XtPointer)'/'
    },
    {
        SgNsetTextSectionFunc, "SetTextSectionFunc", XtRPointer,
        sizeof(XtPointer),
        XtOffsetOf(SgFinderRecCompat, finder.set_text_section_func),
        XtRImmediate, (XtPointer)defaultSetTextSection
    },
    {
        SgNuseDropPocket, "UseDropPocket", XtRBoolean,
        sizeof(Boolean),
        XtOffsetOf(SgFinderRecCompat, finder.use_drop_pocket),
        XtRImmediate, (XtPointer)True
    },
    {
        SgNuseHistoryMenu, "UseHistoryMenu", XtRBoolean,
        sizeof(Boolean),
        XtOffsetOf(SgFinderRecCompat, finder.use_history_menu),
        XtRImmediate, (XtPointer)True
    }
};

SgFinderClassRecCompat sgFinderClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgFinder",
        sizeof(SgFinderRecCompat),
        NULL,
        NULL,
        False,
        finderInitialize,
        NULL,
        XtInheritRealize,
        NULL,
        0,
        finderResources,
        XtNumber(finderResources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        finderDestroy,
        finderResize,
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
        finderChangeManaged,
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

WidgetClass sgFinderWidgetClass = (WidgetClass)&sgFinderClassRec;

static Widget finderChildClass(WidgetClass preferred, WidgetClass fallback)
{
    return preferred ? preferred : fallback;
}

static void finderInitialize(Widget request, Widget new_widget,
                             ArgList args, Cardinal *num_args)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)new_widget;

    (void)request;
    (void)args;
    (void)num_args;

    rec->finder.drop_pocket = NULL;
    rec->finder.text = NULL;
    rec->finder.zoom_bar = NULL;
    rec->finder.history_menubar = NULL;

    if (rec->finder.use_drop_pocket)
    {
        rec->finder.drop_pocket = XtCreateManagedWidget(
            "dropPocket",
            finderChildClass(sgDropPocketWidgetClass,
                             xmDrawingAreaWidgetClass),
            new_widget, NULL, 0);
    }

    rec->finder.text = XtCreateManagedWidget(
        "text",
        xmTextFieldWidgetClass,
        new_widget, NULL, 0);

    rec->finder.zoom_bar = XtCreateManagedWidget(
        "zoomBar",
        finderChildClass(sgZoomBarWidgetClass, xmScaleWidgetClass),
        new_widget, NULL, 0);

    if (rec->finder.use_history_menu)
    {
        rec->finder.history_menubar = XtCreateManagedWidget(
            "historyMenubar",
            finderChildClass(sgDynaMenuWidgetClass,
                             xmDrawingAreaWidgetClass),
            new_widget, NULL, 0);
    }

    if (rec->finder.text)
    {
        XtAddCallback(rec->finder.text, XmNactivateCallback,
                      textActivateCB, (XtPointer)new_widget);
        XtAddCallback(rec->finder.text, XmNvalueChangedCallback,
                      textValueChangedCB, (XtPointer)new_widget);
    }
}

static void finderDestroy(Widget w)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)w;

    rec->finder.drop_pocket = NULL;
    rec->finder.text = NULL;
    rec->finder.zoom_bar = NULL;
    rec->finder.history_menubar = NULL;
}

static void finderLayout(SgFinderRecCompat *rec)
{
    Dimension width;
    Dimension height;
    Dimension top_height;
    Dimension side;
    Dimension left;
    Dimension right;

    if (!rec)
        return;

    width = rec->core.width ? rec->core.width : 1;
    height = rec->core.height ? rec->core.height : 1;
    top_height = height > 28 ? (Dimension)(height - 20) : height;
    side = top_height;
    if (side > 32)
        side = 32;

    left = 0;
    if (rec->finder.drop_pocket && XtIsManaged(rec->finder.drop_pocket))
    {
        XtConfigureWidget(rec->finder.drop_pocket,
                          0, 0, side, top_height, 0);
        left = side;
    }

    right = width;
    if (rec->finder.history_menubar &&
        XtIsManaged(rec->finder.history_menubar))
    {
        if (right > side)
            right = (Dimension)(right - side);
        else
            right = 0;
        XtConfigureWidget(rec->finder.history_menubar,
                          (Position)right, 0, side, top_height, 0);
    }

    if (rec->finder.text && XtIsManaged(rec->finder.text))
    {
        Dimension text_width = right > left
            ? (Dimension)(right - left) : 1;
        XtConfigureWidget(rec->finder.text,
                          (Position)left, 0,
                          text_width, top_height, 0);
    }

    if (rec->finder.zoom_bar && XtIsManaged(rec->finder.zoom_bar))
    {
        Dimension zoom_height = height > top_height
            ? (Dimension)(height - top_height) : 1;
        XtConfigureWidget(rec->finder.zoom_bar,
                          0, (Position)top_height,
                          width, zoom_height, 0);
    }
}

static void finderResize(Widget w)
{
    finderLayout((SgFinderRecCompat *)w);
}

static void finderChangeManaged(Widget w)
{
    finderLayout((SgFinderRecCompat *)w);
}

static void textActivateCB(Widget child, XtPointer client_data,
                           XtPointer call_data)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)client_data;
    XmAnyCallbackStruct callback;

    (void)child;
    (void)call_data;

    if (!rec || !rec->finder.activate_callback)
        return;

    callback.reason = XmCR_ACTIVATE;
    callback.event = NULL;
    XtCallCallbackList((Widget)rec,
                       rec->finder.activate_callback,
                       (XtPointer)&callback);
}

static void textValueChangedCB(Widget child, XtPointer client_data,
                               XtPointer call_data)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)client_data;
    XmAnyCallbackStruct callback;

    (void)child;
    (void)call_data;

    if (!rec || !rec->finder.value_changed_callback)
        return;

    callback.reason = XmCR_VALUE_CHANGED;
    callback.event = NULL;
    XtCallCallbackList((Widget)rec,
                       rec->finder.value_changed_callback,
                       (XtPointer)&callback);
}

Widget SgCreateFinder(Widget parent, char *name, ArgList args, Cardinal count)
{
    return XtCreateWidget(name, sgFinderWidgetClass, parent, args, count);
}

char *SgFinderGetTextString(Widget finder)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)finder;

    if (!finder || !XtIsSubclass(finder, sgFinderWidgetClass) ||
        !rec->finder.text)
        return NULL;

    return XmTextFieldGetString(rec->finder.text);
}

void SgFinderSetTextString(Widget finder, char *text)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)finder;
    XmAnyCallbackStruct callback;

    if (!finder || !XtIsSubclass(finder, sgFinderWidgetClass) ||
        !rec->finder.text)
        return;

    if (!text)
        text = "";

    XmTextFieldSetString(rec->finder.text, text);
    XmTextFieldSetInsertionPosition(rec->finder.text,
                                    (XmTextPosition)strlen(text));

    if (rec->finder.activate_callback)
    {
        callback.reason = XmCR_ACTIVATE;
        callback.event = NULL;
        XtCallCallbackList(finder,
                           rec->finder.activate_callback,
                           (XtPointer)&callback);
    }
}

Widget SgFinderGetChild(Widget finder, int which)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)finder;

    if (!finder || !XtIsSubclass(finder, sgFinderWidgetClass))
        return NULL;

    switch (which)
    {
        case SgFINDER_DROP_POCKET:
            return rec->finder.drop_pocket;
        case SgFINDER_TEXT:
            return rec->finder.text;
        case SgFINDER_ZOOM_BAR:
            return rec->finder.zoom_bar;
        case SgFINDER_HISTORY_MENUBAR:
            return rec->finder.history_menubar;
        default:
            return NULL;
    }
}

#else

/*
 * Build-only fallback for Motif installations without ManagerP.h. This keeps
 * the exported entry points usable, but it is not a native SgFinder class.
 */
typedef struct FinderFallbackState
{
    Widget finder;
    Widget text;
    struct FinderFallbackState *next;
} FinderFallbackState;

static FinderFallbackState *fallbackStates;
WidgetClass sgFinderWidgetClass = NULL;

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeFinderFallback(void)
{
    sgFinderWidgetClass = xmDrawingAreaWidgetClass;
}

static FinderFallbackState *fallbackState(Widget finder, Boolean create)
{
    FinderFallbackState *state;

    for (state = fallbackStates; state; state = state->next)
        if (state->finder == finder)
            return state;

    if (!create || !finder)
        return NULL;

    state = (FinderFallbackState *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;

    state->finder = finder;
    state->text = XmCreateTextField(finder, "text", NULL, 0);
    XtManageChild(state->text);
    state->next = fallbackStates;
    fallbackStates = state;
    return state;
}

Widget SgCreateFinder(Widget parent, char *name, ArgList args, Cardinal count)
{
    Widget finder = XtCreateWidget(name, sgFinderWidgetClass,
                                   parent, args, count);
    (void)fallbackState(finder, True);
    return finder;
}

char *SgFinderGetTextString(Widget finder)
{
    FinderFallbackState *state = fallbackState(finder, True);
    return state && state->text ? XmTextFieldGetString(state->text) : NULL;
}

void SgFinderSetTextString(Widget finder, char *text)
{
    FinderFallbackState *state = fallbackState(finder, True);
    if (state && state->text)
        XmTextFieldSetString(state->text, text ? text : "");
}

Widget SgFinderGetChild(Widget finder, int which)
{
    FinderFallbackState *state = fallbackState(finder, True);
    if (!state)
        return NULL;
    return which == SgFINDER_TEXT ? state->text : NULL;
}

#endif
