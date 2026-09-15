/*
 * SgFinder reconstruction.
 *
 * Private structure and source-unit naming are taken from the original SGI
 * Finder.h/FinderP.h. Behaviour is reconstructed from the N32
 * libSgm.a(Finder.o) object and the original Sgm Finder demo.
 */

#include <Sgm/FinderP.h>
#include <Sgm/ZbText.h>
#include <Sgm/ZoomBar.h>
#include <Sgm/DynaMenuP.h>
#include <Sgm/DropPocket.h>

#include <Xm/TextF.h>
#include <Xm/ManagerP.h>
#include <Xm/PushB.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>

#include <stdlib.h>
#include <string.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

/* Original ZoomBar callback payload. */
typedef struct
{
    int reason;
    XEvent *event;
    int section;
} SgZoomBarCallbackStruct;

static void setFontAndMargin(SgFinderWidget, XmFontList);
static void setTextSection(Widget, int);
static void dropPocketCB(Widget, XtPointer, XtPointer);
static void zoomBarPushCB(Widget, XtPointer, XtPointer);
static void updateZoomBar(SgFinderWidget);
static void textChangeCB(Widget, XtPointer, XtPointer);
static void updateDropPocket(SgFinderWidget);
static void valueChangedCB(Widget, XtPointer, XtPointer);
static void activateCB(Widget, XtPointer, XtPointer);
static void historyPushCB(Widget, XtPointer, XtPointer);

static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Resize(Widget);
static void DoLayout(SgFinderWidget);
static void PreferredSize(SgFinderWidget, Dimension *, Dimension *);
static XtGeometryResult QueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static XtGeometryResult GeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static void ChangeManaged(Widget);
static Boolean SetValues(Widget, Widget, Widget, ArgList, Cardinal *);

static XtResource resources[] =
{
    {
        SgNaddHistoryOnActivate, SgCAddHistoryOnActivate, XtRBoolean,
        sizeof(Boolean), XtOffsetOf(SgFinderRec, finder.addHistoryOnActivate),
        XtRImmediate, (XtPointer)True
    },
    {
        SgNseparator, SgCSeparator, XtRUnsignedChar,
        sizeof(unsigned char), XtOffsetOf(SgFinderRec, finder.separator),
        XtRImmediate, (XtPointer)'/'
    },
    {
        SgNsetTextSectionFunc, SgCSetTextSectionFunc, XtRPointer,
        sizeof(SgSetTextFunc), XtOffsetOf(SgFinderRec, finder.setTextSectionFunc),
        XtRImmediate, (XtPointer)setTextSection
    },
    {
        SgNhistoryPixmap, SgCHistoryPixmap, XtRPixmap,
        sizeof(Pixmap), XtOffsetOf(SgFinderRec, finder.historyPixmap),
        XtRImmediate, (XtPointer)XmUNSPECIFIED_PIXMAP
    },
    {
        SgNinsensitiveHistoryPixmap, SgCInsensitiveHistoryPixmap, XtRPixmap,
        sizeof(Pixmap), XtOffsetOf(SgFinderRec, finder.insensitiveHistoryPixmap),
        XtRImmediate, (XtPointer)XmUNSPECIFIED_PIXMAP
    },
    {
        SgNuseDropPocket, SgCUseDropPocket, XtRBoolean,
        sizeof(Boolean), XtOffsetOf(SgFinderRec, finder.useDropPocket),
        XtRImmediate, (XtPointer)True
    },
    {
        SgNuseHistoryMenu, SgCUseHistoryMenu, XtRBoolean,
        sizeof(Boolean), XtOffsetOf(SgFinderRec, finder.useHistoryMenu),
        XtRImmediate, (XtPointer)True
    },
    {
        XmNactivateCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList), XtOffsetOf(SgFinderRec, finder.activate_callback),
        XtRCallback, NULL
    },
    {
        XmNvalueChangedCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList), XtOffsetOf(SgFinderRec, finder.value_changed_callback),
        XtRCallback, NULL
    }
};

SgFinderClassRec sgFinderClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgFinder",
        sizeof(SgFinderRec),
        NULL,
        NULL,
        False,
        Initialize,
        NULL,
        XtInheritRealize,
        NULL,
        0,
        resources,
        XtNumber(resources),
        NULLQUARK,
        True,
        XtExposeCompressMultiple,
        True,
        False,
        NULL,
        Resize,
        XtInheritExpose,
        SetValues,
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
        NULL, 0, sizeof(XmManagerConstraintRec),
        NULL, NULL, NULL, NULL
    },
    {
        XtInheritTranslations,
        NULL, 0, NULL, 0,
        XmInheritParentProcess,
        NULL
    },
    { 0 }
};

WidgetClass sgFinderWidgetClass = (WidgetClass)&sgFinderClassRec;

static Boolean childManaged(Widget w)
{
    return w && XtIsManaged(w);
}

static void setFontAndMargin(SgFinderWidget ft, XmFontList font_list)
{
    XmFontContext context;
    XmStringCharSet tag = NULL;
    XFontStruct *font = NULL;

    if (!ft || !font_list)
        return;

    if (!XmFontListInitFontContext(&context, font_list))
    {
        XtWarning("SgFinder: unable to initialize font context");
        return;
    }

    if (XmFontListGetNextFont(context, &tag, &font))
        ft->finder.fontStruct = font;
    else
        XtWarning("SgFinder: unable to obtain XFontStruct from font list");

    if (tag)
        XtFree(tag);
    XmFontListFreeFontContext(context);
}

static void setTextSection(Widget widget, int section)
{
    SgFinderWidget ft = (SgFinderWidget)widget;
    char *text;
    size_t i, len;
    int found = -1;

    if (!ft || section < 0)
        return;

    text = SgFinderGetTextString(widget);
    if (!text)
        return;

    len = strlen(text);
    for (i = 0; i < len; ++i)
    {
        if ((unsigned char)text[i] == ft->finder.separator)
        {
            ++found;
            if (found == section)
            {
                text[i + 1] = '\0';
                break;
            }
        }
    }

    SgFinderSetTextString(widget, text);
    XtFree(text);
}

static void dropPocketCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;
    SgDropPocketCallbackStruct *cb = (SgDropPocketCallbackStruct *)call_data;
    char *text = NULL;
    (void)child;

    if (!ft || !ft->finder.useDropPocket || !cb || !cb->iconName)
        return;

    if (XmStringGetLtoR(cb->iconName, XmFONTLIST_DEFAULT_TAG, &text) && text)
    {
        SgFinderSetTextString((Widget)ft, text);
        XtFree(text);
    }
}

static void zoomBarPushCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;
    SgZoomBarCallbackStruct *cb = (SgZoomBarCallbackStruct *)call_data;
    (void)child;

    if (!ft || !cb || cb->section < 0 || !ft->finder.setTextSectionFunc)
        return;

    ft->finder.setTextSectionFunc((Widget)ft, cb->section);
}

static void updateZoomBar(SgFinderWidget ft)
{
    char *text;
    size_t i, len;
    int count = 0;
    int cumulative = DESIRED_SEPARATOR + 3;

    if (!ft || !ft->finder.text || !ft->finder.zoomBar)
        return;

    text = XmTextFieldGetString(ft->finder.text);
    if (!text)
        return;

    len = strlen(text);
    ft->finder.divisionCount = 0;

    for (i = 0; i < len && count < MAXDIVS; ++i)
    {
        int width = 0;

        if (ft->finder.fontStruct)
            width = XTextWidth(ft->finder.fontStruct, &text[i], 1);
        else
        {
            Position x = 0, y = 0;
            Position px = 0, py = 0;
            if (XmTextFieldPosToXY(ft->finder.text, (XmTextPosition)i, &x, &y) &&
                XmTextFieldPosToXY(ft->finder.text, (XmTextPosition)(i + 1), &px, &py))
                width = (int)px - (int)x;
        }

        if (width < 0)
            width = 0;
        cumulative += width;

        if ((unsigned char)text[i] == ft->finder.separator)
            ft->finder.divisions[count++] = (Dimension)cumulative;
    }

    ft->finder.divisionCount = count;
    XtVaSetValues(ft->finder.zoomBar,
                  SgNsectionWidths, ft->finder.divisions,
                  SgNsectionCount, ft->finder.divisionCount,
                  NULL);

    XtFree(text);
}

static void textChangeCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;
    XmTextPosition first;
    XmTextPosition last;
    (void)child;
    (void)call_data;

    if (!ft || !ft->finder.text)
        return;

    first = XmTextFieldXYToPos(ft->finder.text, 7, 0);
    last = XmTextFieldGetLastPosition(ft->finder.text);

    if (first != ft->finder.oldFirstPos || last != ft->finder.oldLastPos)
    {
        ft->finder.oldFirstPos = first;
        ft->finder.oldLastPos = last;
        updateZoomBar(ft);
    }
}

static void updateDropPocket(SgFinderWidget ft)
{
    char *text;
    XmString name;

    if (!ft || !ft->finder.dropPocket)
        return;

    text = SgFinderGetTextString((Widget)ft);
    if (!text)
        return;

    name = XmStringCreateLocalized(text);
    XtVaSetValues(ft->finder.dropPocket, SgNname, name, NULL);
    XmStringFree(name);
    XtFree(text);
}

static void valueChangedCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;

    textChangeCB(child, client_data, call_data);

    if (ft && ft->finder.value_changed_callback)
        XtCallCallbackList((Widget)ft, ft->finder.value_changed_callback, call_data);
}

static void activateCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;
    char *text;
    (void)child;

    if (!ft)
        return;

    text = SgFinderGetTextString((Widget)ft);
    textChangeCB(ft->finder.text, ft, call_data);

    if (ft->finder.useDropPocket)
        updateDropPocket(ft);

    if (ft->finder.historyBar && ft->finder.useHistoryMenu &&
        ft->finder.addHistoryOnActivate && text)
        SgDynaMenuAddHistoryItem((SgDynaMenuWidget)ft->finder.historyBar, text);

    if (text)
        XtFree(text);

    if (ft->finder.activate_callback)
        XtCallCallbackList((Widget)ft, ft->finder.activate_callback, call_data);
}

static void historyPushCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderWidget ft = (SgFinderWidget)client_data;
    SgDynaMenuWidget menu = (SgDynaMenuWidget)child;
    SgDynaMenuCallbackStruct *cb = (SgDynaMenuCallbackStruct *)call_data;
    char *text = NULL;
    int index;

    if (!ft || !menu || !cb || !ft->finder.useHistoryMenu)
        return;

    index = (int)cb->button_number;
    if (index < 0 || index >= menu->dynaMenu.historyItemCount ||
        !menu->dynaMenu.historyItems[index])
        return;

    if (XmStringGetLtoR(menu->dynaMenu.historyItems[index],
                        XmFONTLIST_DEFAULT_TAG, &text) && text)
    {
        SgFinderSetTextString((Widget)ft, text);
        XtFree(text);
    }
}

static void configureHistoryPixmaps(SgFinderWidget ft)
{
    SgDynaMenuWidget menu;

    if (!ft || !ft->finder.historyBar)
        return;

    menu = (SgDynaMenuWidget)ft->finder.historyBar;
    if (!menu->dynaMenu.historyButton)
        return;

    XtVaSetValues(menu->dynaMenu.historyButton,
                  XmNlabelType, XmPIXMAP,
                  XmNlabelPixmap, ft->finder.historyPixmap,
                  XmNlabelInsensitivePixmap, ft->finder.insensitiveHistoryPixmap,
                  NULL);
}

static void Initialize(Widget request, Widget new_widget,
                       ArgList args, Cardinal *num_args)
{
    SgFinderWidget ft = (SgFinderWidget)new_widget;
    Widget children[3];
    Cardinal child_count = 0;
    XmFontList font_list = NULL;
    (void)args;
    (void)num_args;

    ft->finder.dropPocket = NULL;
    ft->finder.zbText = NULL;
    ft->finder.zoomBar = NULL;
    ft->finder.text = NULL;
    ft->finder.historyBar = NULL;
    ft->finder.fontStruct = NULL;
    ft->finder.divisionCount = 0;
    ft->finder.oldFirstPos = (XmTextPosition)-1;
    ft->finder.oldLastPos = (XmTextPosition)-1;
    memset(ft->finder.divisions, 0, sizeof(ft->finder.divisions));

    if (request->core.width == 0)
        ft->core.width = 5;
    if (request->core.height == 0)
        ft->core.height = 5;

    if (ft->finder.useDropPocket)
    {
        ft->finder.dropPocket = XtVaCreateWidget(
            "dropPocket", sgDropPocketWidgetClass, new_widget, NULL);
        if (ft->finder.dropPocket)
            children[child_count++] = ft->finder.dropPocket;
    }

    ft->finder.zbText = XtVaCreateWidget(
        "zbText", sgZbTextWidgetClass, new_widget, NULL);
    if (ft->finder.zbText)
        children[child_count++] = ft->finder.zbText;

    if (ft->finder.useHistoryMenu)
    {
        ft->finder.historyBar = XtVaCreateWidget(
            "historyBar", sgDynaMenuWidgetClass, new_widget, NULL);
        if (ft->finder.historyBar)
            children[child_count++] = ft->finder.historyBar;
    }

    if (child_count)
        XtManageChildren(children, child_count);

    if (ft->finder.zbText)
    {
        ft->finder.zoomBar = SgZbTextGetChild(ft->finder.zbText, SgZBTEXT_ZOOM_BAR);
        ft->finder.text = SgZbTextGetChild(ft->finder.zbText, SgZBTEXT_TEXT);
    }

    if (ft->finder.text)
    {
        XtAddCallback(ft->finder.text, XmNvalueChangedCallback,
                      valueChangedCB, ft);
        XtAddCallback(ft->finder.text, XmNactivateCallback,
                      activateCB, ft);
        XtVaGetValues(ft->finder.text, XmNfontList, &font_list, NULL);
        setFontAndMargin(ft, font_list);
    }

    if (ft->finder.zoomBar)
        XtAddCallback(ft->finder.zoomBar, XmNactivateCallback,
                      zoomBarPushCB, ft);

    if (ft->finder.dropPocket)
        XtAddCallback(ft->finder.dropPocket, SgNiconUpdateCallback,
                      dropPocketCB, ft);

    if (ft->finder.historyBar)
    {
        XtAddCallback(ft->finder.historyBar, SgNdynaPushCallback,
                      historyPushCB, ft);
        configureHistoryPixmaps(ft);
    }

    DoLayout(ft);
    updateZoomBar(ft);
}

static void DoLayout(SgFinderWidget ft)
{
    Dimension row_height = 1;
    Dimension text_width = MIN_TEXT_WIDTH;
    Dimension drop_width = 0;
    Dimension history_width = 0;
    Position x = 0;

    if (!ft || !ft->finder.zbText)
        return;

    if (ft->finder.zbText->core.height)
        row_height = ft->finder.zbText->core.height;

    if (ft->finder.dropPocket)
        drop_width = ft->finder.dropPocket->core.height;

    if (ft->finder.historyBar)
        history_width = (Dimension)(row_height / 2 + 4);

    if (ft->core.width > drop_width + history_width + 4)
        text_width = (Dimension)(ft->core.width - drop_width - history_width - 4);
    if (text_width < MIN_TEXT_WIDTH)
        text_width = MIN_TEXT_WIDTH;

    if (childManaged(ft->finder.dropPocket))
    {
        Dimension size = row_height;
        XtConfigureWidget(ft->finder.dropPocket, 0, 0,
                          size, size, ft->finder.dropPocket->core.border_width);
        x = (Position)(size + DESIRED_SEPARATOR);
    }

    if (childManaged(ft->finder.zbText))
    {
        XtConfigureWidget(ft->finder.zbText, x, 0,
                          text_width, row_height,
                          ft->finder.zbText->core.border_width);
        x = (Position)(x + text_width + DESIRED_SEPARATOR);
    }

    if (childManaged(ft->finder.historyBar))
    {
        XtConfigureWidget(ft->finder.historyBar, x, 0,
                          history_width, row_height,
                          ft->finder.historyBar->core.border_width);
    }
}

static void Resize(Widget w)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    DoLayout(ft);
    updateZoomBar(ft);
}

static void PreferredSize(SgFinderWidget ft, Dimension *width, Dimension *height)
{
    Cardinal i;
    Dimension w = 0;
    Dimension h = 0;

    if (!ft)
        return;

    for (i = 0; i < ft->composite.num_children; ++i)
    {
        Widget child = ft->composite.children[i];
        if (!childManaged(child))
            continue;
        w = (Dimension)(w + child->core.width);
        if (child->core.height > h)
            h = child->core.height;
    }

    if (width) *width = w;
    if (height) *height = h;
}

static XtGeometryResult QueryGeometry(Widget w,
                                      XtWidgetGeometry *intended,
                                      XtWidgetGeometry *preferred)
{
    Dimension pw = 0, ph = 0;

    PreferredSize((SgFinderWidget)w, &pw, &ph);
    preferred->request_mode = CWWidth | CWHeight;
    preferred->width = pw;
    preferred->height = ph;

    if (intended &&
        (intended->request_mode & (CWWidth | CWHeight)) == (CWWidth | CWHeight) &&
        intended->width == pw && intended->height == ph)
        return XtGeometryYes;

    if (w->core.width == pw && w->core.height == ph)
        return XtGeometryNo;

    return XtGeometryAlmost;
}

static XtGeometryResult GeometryManager(Widget child,
                                        XtWidgetGeometry *request,
                                        XtWidgetGeometry *reply)
{
    SgFinderWidget ft = (SgFinderWidget)XtParent(child);
    (void)reply;

    if (request->request_mode & (CWX | CWY))
        return XtGeometryNo;

    if (request->request_mode & CWWidth)
        child->core.width = request->width;
    if (request->request_mode & CWHeight)
        child->core.height = request->height;

    ChangeManaged((Widget)ft);
    return XtGeometryYes;
}

static void ChangeManaged(Widget w)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    Dimension pw = 0, ph = 0;

    PreferredSize(ft, &pw, &ph);
    if (ft->core.width < pw || ft->core.height < ph)
        (void)XtMakeResizeRequest(w, pw, ph, NULL, NULL);

    DoLayout(ft);
    updateZoomBar(ft);
}

static Boolean SetValues(Widget old_widget, Widget request_widget,
                         Widget new_widget, ArgList args, Cardinal *num_args)
{
    SgFinderWidget old = (SgFinderWidget)old_widget;
    SgFinderWidget ft = (SgFinderWidget)new_widget;
    (void)request_widget;
    (void)args;
    (void)num_args;

    /* Original widget does not allow these structural toggles to change. */
    if (ft->finder.useDropPocket != old->finder.useDropPocket)
        ft->finder.useDropPocket = old->finder.useDropPocket;
    if (ft->finder.useHistoryMenu != old->finder.useHistoryMenu)
        ft->finder.useHistoryMenu = old->finder.useHistoryMenu;

    if (ft->finder.historyPixmap != old->finder.historyPixmap ||
        ft->finder.insensitiveHistoryPixmap != old->finder.insensitiveHistoryPixmap)
        configureHistoryPixmaps(ft);

    return False;
}

Widget SgCreateFinder(Widget parent, char *name,
                      ArgList arglist, Cardinal argcount)
{
    return XtCreateWidget(name, sgFinderWidgetClass,
                          parent, arglist, argcount);
}

void SgFinderAddHistoryItem(Widget w, char *str)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    if (ft && ft->finder.historyBar)
        SgDynaMenuAddHistoryItem((SgDynaMenuWidget)ft->finder.historyBar, str);
}

void SgFinderClearHistory(Widget w)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    if (ft && ft->finder.historyBar)
        SgDynaMenuClearHistory((SgDynaMenuWidget)ft->finder.historyBar);
}

char *SgFinderGetTextString(Widget w)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    char *text;
    size_t len;

    if (!ft || !ft->finder.text)
        return NULL;

    text = XmTextFieldGetString(ft->finder.text);
    if (!text)
        return NULL;

    len = strlen(text);
    if (len > 0 && text[len - 1] == '\n')
        text[len - 1] = '\0';
    return text;
}

void SgFinderSetTextString(Widget w, char *value)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    XmAnyCallbackStruct cb;

    if (!ft || !ft->finder.text || !value)
        return;

    XmTextFieldSetString(ft->finder.text, value);
    XmTextFieldSetInsertionPosition(ft->finder.text,
                                    (XmTextPosition)strlen(value));

    cb.reason = XmCR_ACTIVATE;
    cb.event = NULL;
    activateCB(ft->finder.text, ft, &cb);
}

Widget SgFinderGetChild(Widget w, int child)
{
    SgFinderWidget ft = (SgFinderWidget)w;
    if (!ft)
        return NULL;

    switch (child)
    {
        case SgFINDER_TEXT:
            return ft->finder.text;
        case SgFINDER_ZOOM_BAR:
            return ft->finder.zoomBar;
        case SgFINDER_DROP_POCKET:
            return ft->finder.dropPocket;
        case SgFINDER_HISTORY_MENUBAR:
            return ft->finder.historyBar;
        default:
            return NULL;
    }
}
