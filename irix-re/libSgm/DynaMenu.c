/*
 * SgDynaMenu reconstruction.
 *
 * Source of truth:
 *   - IRIX Sgm/DynaMenu.h and DynaMenuP.h
 *   - IRIX N32 libSgm.a(DynaMenu.o)
 */

#include <Sgm/DynaMenuP.h>

#include <Xm/ManagerP.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>

#include <stdio.h>
#include <string.h>

#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

/* Exact exported 218-byte translation object from DynaMenu.o .data. */
char _SgDynaMenu_pushBTranslations[218] =
    "<Btn2Down>:ProcessDrag()\n"
    "~s ~m ~a <Key>Return:PrimitiveParentActivate()\n"
    "<Key>osfActivate:PrimitiveParentActivate()\n"
    "<Key>osfCancel:PrimitiveParentCancel()\n"
    "<Key>osfHelp:Help()\n"
    "<EnterWindow>:Enter()\n"
    "<LeaveWindow>:Leave()";

static XtTranslations pushBTranslations;

static void ClassInitialize(void);
static void Initialize(Widget, Widget, ArgList, Cardinal *);
static void Destroy(Widget);
static void Resize(Widget);
static Boolean SetValues(Widget, Widget, Widget, ArgList, Cardinal *);
static XtGeometryResult QueryGeometry(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static XtGeometryResult GeometryManager(Widget, XtWidgetGeometry *, XtWidgetGeometry *);
static void ChangeManaged(Widget);

static void historyPushCB(Widget, XtPointer, XtPointer);
static void Post(Widget, XtPointer, XEvent *, Boolean *);
static void makeHistoryMenu(SgDynaMenuWidget);
static void createMenuItems(SgDynaMenuWidget);
static void updateMenuItems(SgDynaMenuWidget);
static void copyItems(SgDynaMenuWidget);
static void deleteItem(SgDynaMenuWidget, int);
static void addItem(SgDynaMenuWidget, XmString, int);
static void clearItemList(SgDynaMenuWidget);
static void DoLayout(SgDynaMenuWidget);
static void PreferredSize(SgDynaMenuWidget, Dimension *, Dimension *);

static XtResource resources[] =
{
    {
        SgNdynaPushCallback, XmCCallback, XtRCallback,
        sizeof(XtCallbackList),
        XtOffsetOf(SgDynaMenuRec, dynaMenu.dynaPushCallback),
        XtRCallback, NULL
    },
    {
        SgNhistoryListItems, SgCHistoryListItems, XmRXmStringTable,
        sizeof(XmStringTable),
        XtOffsetOf(SgDynaMenuRec, dynaMenu.historyItems),
        XtRImmediate, NULL
    },
    {
        SgNhistoryListItemCount, SgCHistoryListItemCount, XtRInt,
        sizeof(int),
        XtOffsetOf(SgDynaMenuRec, dynaMenu.historyItemCount),
        XtRImmediate, (XtPointer)0
    },
    {
        SgNmaxHistoryCount, SgCMaxHistoryCount, XtRInt,
        sizeof(int),
        XtOffsetOf(SgDynaMenuRec, dynaMenu.maxHistoryCount),
        XtRImmediate, (XtPointer)6
    }
};

SgDynaMenuClassRec sgDynaMenuClassRec =
{
    {
        (WidgetClass)&xmManagerClassRec,
        "SgDynaMenu",
        sizeof(SgDynaMenuRec),
        ClassInitialize,
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
        Destroy,
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

WidgetClass sgDynaMenuWidgetClass = (WidgetClass)&sgDynaMenuClassRec;

static void ClassInitialize(void)
{
    pushBTranslations = XtParseTranslationTable(_SgDynaMenu_pushBTranslations);
}

static void clearItemList(SgDynaMenuWidget ft)
{
    int i;

    if (!ft || !ft->dynaMenu.historyItems)
        return;

    for (i = 0; i < ft->dynaMenu.historyItemCount; ++i)
        if (ft->dynaMenu.historyItems[i])
            XmStringFree(ft->dynaMenu.historyItems[i]);

    XtFree((char *)ft->dynaMenu.historyItems);
    ft->dynaMenu.historyItems = NULL;
    ft->dynaMenu.historyItemCount = 0;
}

static void copyItems(SgDynaMenuWidget ft)
{
    XmStringTable source;
    XmStringTable copy;
    int count;
    int i;

    source = ft->dynaMenu.historyItems;
    count = ft->dynaMenu.historyItemCount;

    if (count < 0)
        count = 0;
    if (count > ft->dynaMenu.maxHistoryCount)
        count = ft->dynaMenu.maxHistoryCount;

    if (!source || count == 0)
    {
        ft->dynaMenu.historyItems = NULL;
        ft->dynaMenu.historyItemCount = 0;
        return;
    }

    copy = (XmStringTable)XtMalloc((Cardinal)count * sizeof(XmString));
    for (i = 0; i < count; ++i)
        copy[i] = source[i] ? XmStringCopy(source[i]) : NULL;

    ft->dynaMenu.historyItems = copy;
    ft->dynaMenu.historyItemCount = count;
}

static void historyPushCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    SgDynaMenuWidget ft = (SgDynaMenuWidget)client_data;
    XmPushButtonCallbackStruct *push = (XmPushButtonCallbackStruct *)call_data;
    XtPointer user_data = NULL;
    SgDynaMenuCallbackStruct cb;

    XtVaGetValues(w, XmNuserData, &user_data, NULL);

    cb.reason = SgCR_DYNA_PUSH;
    cb.event = push ? push->event : NULL;
    cb.button_number = (XtArgVal)user_data;

    if (ft && ft->dynaMenu.dynaPushCallback)
        XtCallCallbackList((Widget)ft, ft->dynaMenu.dynaPushCallback, &cb);
}

static void Post(Widget w, XtPointer client_data, XEvent *event, Boolean *cont)
{
    SgDynaMenuWidget ft = (SgDynaMenuWidget)client_data;
    (void)w;
    (void)cont;

    if (!ft || !ft->dynaMenu.historyPopup ||
        ft->dynaMenu.historyItemCount <= 0 || !event)
        return;

    if (event->type == ButtonPress)
    {
        XmMenuPosition(ft->dynaMenu.historyPopup, &event->xbutton);
        XtManageChild(ft->dynaMenu.historyPopup);
    }
}

static void createMenuItems(SgDynaMenuWidget ft)
{
    int i;

    if (!ft || !ft->dynaMenu.historyPopup)
        return;

    for (i = 0; i < ft->dynaMenu.maxHistoryCount; ++i)
    {
        char name[32];
        Widget item;

        sprintf(name, "history%d", i);
        item = XtVaCreateWidget(name,
                                xmPushButtonWidgetClass,
                                ft->dynaMenu.historyPopup,
                                XmNuserData, (XtPointer)(XtArgVal)i,
                                NULL);
        if (pushBTranslations)
            XtOverrideTranslations(item, pushBTranslations);
        XtAddCallback(item, XmNactivateCallback, historyPushCB, ft);
    }
}

static void updateMenuItems(SgDynaMenuWidget ft)
{
    WidgetList children = NULL;
    Cardinal child_count = 0;
    Cardinal i;

    if (!ft || !ft->dynaMenu.historyPopup)
        return;

    XtVaGetValues(ft->dynaMenu.historyPopup,
                  XmNchildren, &children,
                  XmNnumChildren, &child_count,
                  NULL);

    if (child_count)
        XtUnmanageChildren(children, child_count);

    for (i = 0; i < child_count && (int)i < ft->dynaMenu.historyItemCount; ++i)
    {
        XtVaSetValues(children[i],
                      XmNlabelString, ft->dynaMenu.historyItems[i],
                      XmNuserData, (XtPointer)(XtArgVal)i,
                      NULL);
        XtManageChild(children[i]);
    }
}

static void makeHistoryMenu(SgDynaMenuWidget ft)
{
    if (!ft)
        return;

    ft->dynaMenu.historyButton =
        XtVaCreateManagedWidget("historyButton",
                                xmPushButtonWidgetClass,
                                (Widget)ft,
                                XmNtranslations, pushBTranslations,
                                NULL);

    ft->dynaMenu.historyPopup =
        XmCreatePopupMenu(ft->dynaMenu.historyButton,
                          "historyPopup", NULL, 0);

    XtAddEventHandler(ft->dynaMenu.historyButton,
                      ButtonPressMask, False, Post, ft);

    createMenuItems(ft);
    updateMenuItems(ft);
}

static void Initialize(Widget request, Widget new_widget,
                       ArgList args, Cardinal *num_args)
{
    SgDynaMenuWidget ft = (SgDynaMenuWidget)new_widget;
    (void)args;
    (void)num_args;

    if (request->core.width == 0)
        ft->core.width = 5;
    if (request->core.height == 0)
        ft->core.height = 5;

    if (ft->dynaMenu.maxHistoryCount < 0)
        ft->dynaMenu.maxHistoryCount = 0;

    if (ft->dynaMenu.historyItemCount > 0 && !ft->dynaMenu.historyItems)
    {
        XtWarning("Widget DynaMenu: historyItems and historyItemCount are not kosher.");
        ft->dynaMenu.historyItemCount = 0;
    }

    copyItems(ft);
    makeHistoryMenu(ft);
}

static void Destroy(Widget w)
{
    clearItemList((SgDynaMenuWidget)w);
}

static void DoLayout(SgDynaMenuWidget ft)
{
    if (!ft || !ft->dynaMenu.historyButton)
        return;

    XtConfigureWidget(ft->dynaMenu.historyButton,
                      0, 0,
                      ft->core.width,
                      ft->core.height,
                      ft->dynaMenu.historyButton->core.border_width);
}

static void Resize(Widget w)
{
    DoLayout((SgDynaMenuWidget)w);
}

static void PreferredSize(SgDynaMenuWidget ft,
                          Dimension *width, Dimension *height)
{
    if (!ft || !ft->dynaMenu.historyButton)
    {
        if (width) *width = 1;
        if (height) *height = 1;
        return;
    }

    if (width) *width = ft->dynaMenu.historyButton->core.width;
    if (height) *height = ft->dynaMenu.historyButton->core.height;
}

static XtGeometryResult QueryGeometry(Widget w,
                                      XtWidgetGeometry *intended,
                                      XtWidgetGeometry *preferred)
{
    Dimension pw, ph;

    PreferredSize((SgDynaMenuWidget)w, &pw, &ph);
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
    SgDynaMenuWidget ft = (SgDynaMenuWidget)XtParent(child);
    (void)reply;

    if (request->request_mode & (CWX | CWY))
        return XtGeometryNo;

    if (request->request_mode & CWWidth)
        ft->core.width = request->width;
    if (request->request_mode & CWHeight)
        ft->core.height = request->height;

    DoLayout(ft);
    return XtGeometryYes;
}

static void ChangeManaged(Widget w)
{
    DoLayout((SgDynaMenuWidget)w);
}

static Boolean SetValues(Widget old_widget, Widget request_widget,
                         Widget new_widget, ArgList args, Cardinal *num_args)
{
    SgDynaMenuWidget old = (SgDynaMenuWidget)old_widget;
    SgDynaMenuWidget ft = (SgDynaMenuWidget)new_widget;
    Boolean redraw = False;
    (void)request_widget;
    (void)args;
    (void)num_args;

    if (ft->dynaMenu.maxHistoryCount != old->dynaMenu.maxHistoryCount)
    {
        XtWarning("DynaMenu Widget: Cannot change SgNmaxHistoryCount after creation.");
        ft->dynaMenu.maxHistoryCount = old->dynaMenu.maxHistoryCount;
    }

    if (ft->dynaMenu.historyItems != old->dynaMenu.historyItems ||
        ft->dynaMenu.historyItemCount != old->dynaMenu.historyItemCount)
    {
        if (old->dynaMenu.historyItems)
            clearItemList(old);
        copyItems(ft);
        updateMenuItems(ft);
        redraw = True;
    }

    return redraw;
}

static void deleteItem(SgDynaMenuWidget ft, int pos)
{
    XmStringTable items;
    int i;
    int new_count;

    if (!ft || pos < 0 || pos >= ft->dynaMenu.historyItemCount)
        return;

    if (ft->dynaMenu.historyItems[pos])
        XmStringFree(ft->dynaMenu.historyItems[pos]);

    for (i = pos; i + 1 < ft->dynaMenu.historyItemCount; ++i)
        ft->dynaMenu.historyItems[i] = ft->dynaMenu.historyItems[i + 1];

    new_count = ft->dynaMenu.historyItemCount - 1;
    if (new_count == 0)
    {
        XtFree((char *)ft->dynaMenu.historyItems);
        ft->dynaMenu.historyItems = NULL;
    }
    else
    {
        items = (XmStringTable)XtRealloc((char *)ft->dynaMenu.historyItems,
                                         (Cardinal)new_count * sizeof(XmString));
        ft->dynaMenu.historyItems = items;
    }
    ft->dynaMenu.historyItemCount = new_count;
}

static void addItem(SgDynaMenuWidget ft, XmString item, int pos)
{
    XmStringTable items;
    int i;
    int old_count;

    if (!ft || !item)
        return;

    old_count = ft->dynaMenu.historyItemCount;
    if (pos < 0) pos = 0;
    if (pos > old_count) pos = old_count;

    items = (XmStringTable)XtRealloc((char *)ft->dynaMenu.historyItems,
                                     (Cardinal)(old_count + 1) * sizeof(XmString));
    ft->dynaMenu.historyItems = items;

    for (i = old_count; i > pos; --i)
        items[i] = items[i - 1];

    items[pos] = XmStringCopy(item);
    ft->dynaMenu.historyItemCount = old_count + 1;
}

Widget SgCreateDynaMenu(Widget parent, char *name,
                        ArgList arglist, Cardinal argcount)
{
    return XtCreateWidget(name, sgDynaMenuWidgetClass,
                          parent, arglist, argcount);
}

void SgDynaMenuAddHistoryItem(SgDynaMenuWidget ft, char *str)
{
    XmString item;
    int i;

    if (!ft || !str || !*str || ft->dynaMenu.maxHistoryCount <= 0)
        return;

    item = XmStringCreateLocalized(str);

    for (i = 0; i < ft->dynaMenu.historyItemCount; ++i)
    {
        if (XmStringCompare(ft->dynaMenu.historyItems[i], item))
        {
            deleteItem(ft, i);
            break;
        }
    }

    if (ft->dynaMenu.historyItemCount >= ft->dynaMenu.maxHistoryCount)
        deleteItem(ft, ft->dynaMenu.historyItemCount - 1);

    addItem(ft, item, 0);
    XmStringFree(item);
    updateMenuItems(ft);
}

void SgDynaMenuClearHistory(SgDynaMenuWidget ft)
{
    if (!ft)
        return;

    clearItemList(ft);
    updateMenuItems(ft);
}
