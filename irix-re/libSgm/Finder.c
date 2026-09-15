/*
 * SgFinder reconstruction from IRIX libSgm.so.1.
 *
 * Binary-confirmed child ownership:
 *   +0xec dropPocket, +0xf0 private SgZbText, +0xf4 ZoomBar,
 *   +0xf8 text, +0xfc historyBar.
 * Finder also owns a 255-entry cumulative section-width table which is
 * passed to SgZoomBar as sectionCount/sectionWidths.
 */
#include <Sgm/Finder.h>
#include <Xm/DrawingA.h>
#include <Xm/PushB.h>
#include <Xm/TextF.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern WidgetClass sgDropPocketWidgetClass;
extern WidgetClass sgDynaMenuWidgetClass;

#define FINDER_HISTORY_CAPACITY 6
#define FINDER_MAX_SECTIONS 255

#if defined(SGM_HAVE_MANAGERP_H)
#include <Xm/ManagerP.h>
#ifndef XmInheritParentProcess
#define XmInheritParentProcess ((XmParentProcessProc)_XtInherit)
#endif

typedef struct {
    XtCallbackList activate_callback;
    XtCallbackList value_changed_callback;
    Pixmap history_pixmap;
    unsigned char separator;
    XtPointer set_text_section_func;
    Boolean use_drop_pocket;
    Boolean use_history_menu;
    Widget drop_pocket;
    Widget zb_text;
    Widget zoom_bar;
    Widget text;
    Widget history_menubar;
    unsigned short section_widths[FINDER_MAX_SECTIONS];
    Cardinal section_count;
    char *history[FINDER_HISTORY_CAPACITY];
    Widget history_item[FINDER_HISTORY_CAPACITY];
    Cardinal history_count;
} SgFinderPartCompat;

typedef struct {
    CorePart core;
    CompositePart composite;
    ConstraintPart constraint;
    XmManagerPart manager;
    SgFinderPartCompat finder;
} SgFinderRecCompat;

typedef struct { XtPointer extension; } SgFinderClassPartCompat;
typedef struct {
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
static void finderLayout(SgFinderRecCompat *);
static void updateZoomBarSections(SgFinderRecCompat *);
static void textActivateCB(Widget, XtPointer, XtPointer);
static void textValueChangedCB(Widget, XtPointer, XtPointer);
static void historyItemActivateCB(Widget, XtPointer, XtPointer);
static void rebuildHistoryMenu(SgFinderRecCompat *);
static void clearHistoryStorage(SgFinderRecCompat *, Boolean);

static void activateTextChild(SgFinderRecCompat *rec)
{
    XmAnyCallbackStruct cb;
    if (!rec || !rec->finder.text) return;
    cb.reason = XmCR_ACTIVATE;
    cb.event = NULL;
    XtCallCallbacks(rec->finder.text, XmNactivateCallback, (XtPointer)&cb);
}

static void defaultSetTextSection(Widget finder, int section)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)finder;
    char *value, *p;
    int current = 0;
    if (!rec || !rec->finder.text || section < 0) return;
    value = XmTextFieldGetString(rec->finder.text);
    if (!value) return;
    for (p = value; *p && current <= section; ++p) {
        if ((unsigned char)*p == rec->finder.separator) {
            if (current == section) { p[1] = '\0'; break; }
            ++current;
        }
    }
    SgFinderSetTextString(finder, value);
    activateTextChild(rec);
    XtFree(value);
}

static XtResource finderResources[] = {
    { XmNactivateCallback, XmCCallback, XtRCallback, sizeof(XtCallbackList),
      XtOffsetOf(SgFinderRecCompat, finder.activate_callback), XtRCallback, NULL },
    { XmNvalueChangedCallback, XmCCallback, XtRCallback, sizeof(XtCallbackList),
      XtOffsetOf(SgFinderRecCompat, finder.value_changed_callback), XtRCallback, NULL },
    { SgNhistoryPixmap, "HistoryPixmap", XtRPixmap, sizeof(Pixmap),
      XtOffsetOf(SgFinderRecCompat, finder.history_pixmap), XtRImmediate,
      (XtPointer)XmUNSPECIFIED_PIXMAP },
    { SgNseparator, "Separator", XtRUnsignedChar, sizeof(unsigned char),
      XtOffsetOf(SgFinderRecCompat, finder.separator), XtRImmediate, (XtPointer)'/' },
    { SgNsetTextSectionFunc, "SetTextSectionFunc", XtRPointer, sizeof(XtPointer),
      XtOffsetOf(SgFinderRecCompat, finder.set_text_section_func), XtRImmediate,
      (XtPointer)defaultSetTextSection },
    { SgNuseDropPocket, "UseDropPocket", XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgFinderRecCompat, finder.use_drop_pocket), XtRImmediate, (XtPointer)True },
    { SgNuseHistoryMenu, "UseHistoryMenu", XtRBoolean, sizeof(Boolean),
      XtOffsetOf(SgFinderRecCompat, finder.use_history_menu), XtRImmediate, (XtPointer)True }
};

SgFinderClassRecCompat sgFinderClassRec = {
    { (WidgetClass)&xmManagerClassRec, "SgFinder", sizeof(SgFinderRecCompat),
      NULL, NULL, False, finderInitialize, NULL, XtInheritRealize, NULL, 0,
      finderResources, XtNumber(finderResources), NULLQUARK, True,
      XtExposeCompressMultiple, True, False, finderDestroy, finderResize,
      XtInheritExpose, NULL, NULL, XtInheritSetValuesAlmost, NULL, NULL,
      XtVersion, NULL, XtInheritTranslations, XtInheritQueryGeometry, NULL, NULL },
    { XtInheritGeometryManager, finderChangeManaged, XtInheritInsertChild,
      XtInheritDeleteChild, NULL },
    { NULL, 0, sizeof(XmManagerConstraintRec), NULL, NULL, NULL, NULL },
    { XtInheritTranslations, NULL, 0, NULL, 0, XmInheritParentProcess, NULL },
    { NULL }
};
WidgetClass sgFinderWidgetClass = (WidgetClass)&sgFinderClassRec;

static WidgetClass childClass(WidgetClass preferred, WidgetClass fallback)
{ return preferred ? preferred : fallback; }

static void finderInitialize(Widget request, Widget new_widget,
                             ArgList args, Cardinal *num_args)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)new_widget;
    Cardinal i;
    (void)request; (void)args; (void)num_args;
    rec->finder.drop_pocket = rec->finder.zb_text = NULL;
    rec->finder.zoom_bar = rec->finder.text = NULL;
    rec->finder.history_menubar = NULL;
    rec->finder.section_count = rec->finder.history_count = 0;
    memset(rec->finder.section_widths, 0, sizeof(rec->finder.section_widths));
    for (i = 0; i < FINDER_HISTORY_CAPACITY; ++i) {
        rec->finder.history[i] = NULL;
        rec->finder.history_item[i] = NULL;
    }
    if (rec->finder.use_drop_pocket)
        rec->finder.drop_pocket = XtCreateManagedWidget(
            "dropPocket", childClass(sgDropPocketWidgetClass, xmDrawingAreaWidgetClass),
            new_widget, NULL, 0);

    rec->finder.zb_text = SgCreateZbText(new_widget, "zbText", NULL, 0);
    if (rec->finder.zb_text) {
        XtManageChild(rec->finder.zb_text);
        rec->finder.text = SgZbTextGetChild(rec->finder.zb_text, SgZBTEXT_TEXT);
        rec->finder.zoom_bar = SgZbTextGetChild(rec->finder.zb_text, SgZBTEXT_ZOOM_BAR);
    }
    if (rec->finder.use_history_menu)
        rec->finder.history_menubar = XtCreateManagedWidget(
            "historyBar", childClass(sgDynaMenuWidgetClass, xmDrawingAreaWidgetClass),
            new_widget, NULL, 0);
    if (rec->finder.text) {
        XtAddCallback(rec->finder.text, XmNactivateCallback, textActivateCB, new_widget);
        XtAddCallback(rec->finder.text, XmNvalueChangedCallback, textValueChangedCB, new_widget);
    }
    finderLayout(rec);
    updateZoomBarSections(rec);
}

static void clearHistoryStorage(SgFinderRecCompat *rec, Boolean destroy_widgets)
{
    Cardinal i;
    if (!rec) return;
    for (i = 0; i < FINDER_HISTORY_CAPACITY; ++i) {
        if (rec->finder.history_item[i]) {
            if (destroy_widgets) XtDestroyWidget(rec->finder.history_item[i]);
            rec->finder.history_item[i] = NULL;
        }
        if (rec->finder.history[i]) { XtFree(rec->finder.history[i]); rec->finder.history[i] = NULL; }
    }
    rec->finder.history_count = 0;
}

static void finderDestroy(Widget w)
{
    SgFinderRecCompat *rec = (SgFinderRecCompat *)w;
    clearHistoryStorage(rec, False);
    rec->finder.drop_pocket = rec->finder.zb_text = NULL;
    rec->finder.zoom_bar = rec->finder.text = rec->finder.history_menubar = NULL;
}

static Boolean childManaged(Widget w) { return w && XtIsManaged(w); }

static void finderLayout(SgFinderRecCompat *rec)
{
    Dimension zoom_h = 0, text_h = 0, zb_shadow = 0;
    Dimension finder_w, row_h, zb_w, history_w;
    unsigned int fixed = 0;
    Position x = 0;
    if (!rec || !rec->finder.zb_text) return;
    finder_w = rec->core.width ? rec->core.width : 1;
    if (rec->finder.zoom_bar) XtVaGetValues(rec->finder.zoom_bar, XmNheight, &zoom_h, NULL);
    if (rec->finder.text) XtVaGetValues(rec->finder.text, XmNheight, &text_h, NULL);
    XtVaGetValues(rec->finder.zb_text, XmNshadowThickness, &zb_shadow, NULL);
    if (zoom_h < 6) zoom_h = 6;
    row_h = (Dimension)(zoom_h + text_h + 2 * zb_shadow);
    if (!row_h) row_h = rec->core.height ? rec->core.height : 1;

    if (childManaged(rec->finder.drop_pocket)) fixed += (unsigned int)row_h + 2U;
    if (childManaged(rec->finder.history_menubar)) fixed += (unsigned int)(row_h / 2) + 6U;
    zb_w = (unsigned int)finder_w > fixed ? (Dimension)((unsigned int)finder_w - fixed) : 40;
    if (zb_w < 40) zb_w = 40;

    if (childManaged(rec->finder.drop_pocket)) {
        Dimension bw = 0;
        XtVaGetValues(rec->finder.drop_pocket, XmNborderWidth, &bw, NULL);
        XtConfigureWidget(rec->finder.drop_pocket, 0, 0, row_h, row_h, bw);
        x = (Position)(row_h + 2);
    }
    if (childManaged(rec->finder.zb_text)) {
        Dimension bw = 0;
        XtVaGetValues(rec->finder.zb_text, XmNborderWidth, &bw, NULL);
        XtConfigureWidget(rec->finder.zb_text, x, 0, zb_w, row_h, bw);
        x = (Position)(x + zb_w + 2);
    }
    if (childManaged(rec->finder.history_menubar)) {
        Dimension bw = 0;
        history_w = (Dimension)(row_h / 2 + 4);
        XtVaGetValues(rec->finder.history_menubar, XmNborderWidth, &bw, NULL);
        XtConfigureWidget(rec->finder.history_menubar, x, 0, history_w, row_h, bw);
    }
}

static void updateZoomBarSections(SgFinderRecCompat *rec)
{
    char *value;
    size_t i, len;
    Cardinal count = 0;
    Position x = 5, y = 0;
    if (!rec || !rec->finder.text || !rec->finder.zoom_bar) return;
    value = XmTextFieldGetString(rec->finder.text);
    if (!value) return;
    len = strlen(value);
    for (i = 0; i < len && count < FINDER_MAX_SECTIONS; ++i) {
        if ((unsigned char)value[i] != rec->finder.separator) continue;
        if (!XmTextFieldPosToXY(rec->finder.text, (XmTextPosition)(i + 1), &x, &y))
            x = count ? (Position)rec->finder.section_widths[count - 1] : 5;
        if (x < 0) x = 0;
        rec->finder.section_widths[count++] = (unsigned short)x;
    }
    rec->finder.section_count = count;
    XtVaSetValues(rec->finder.zoom_bar,
                  SgNsectionCount, (int)count,
                  SgNsectionWidths, (XtPointer)rec->finder.section_widths,
                  NULL);
    XtFree(value);
}

static void finderResize(Widget w)
{ SgFinderRecCompat *r=(SgFinderRecCompat *)w; finderLayout(r); updateZoomBarSections(r); }
static void finderChangeManaged(Widget w)
{ SgFinderRecCompat *r=(SgFinderRecCompat *)w; finderLayout(r); updateZoomBarSections(r); }

static void textActivateCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)client_data;
    XmAnyCallbackStruct cb;
    (void)child; (void)call_data;
    if (!rec || !rec->finder.activate_callback) return;
    cb.reason=XmCR_ACTIVATE; cb.event=NULL;
    XtCallCallbackList((Widget)rec, rec->finder.activate_callback, &cb);
}

static void textValueChangedCB(Widget child, XtPointer client_data, XtPointer call_data)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)client_data;
    XmAnyCallbackStruct cb;
    (void)child; (void)call_data;
    if (!rec) return;
    updateZoomBarSections(rec);
    if (!rec->finder.value_changed_callback) return;
    cb.reason=XmCR_VALUE_CHANGED; cb.event=NULL;
    XtCallCallbackList((Widget)rec, rec->finder.value_changed_callback, &cb);
}

static void historyItemActivateCB(Widget item, XtPointer client_data, XtPointer call_data)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)client_data;
    XmString label=NULL; char *text=NULL;
    (void)call_data;
    if (!rec || !item) return;
    XtVaGetValues(item, XmNlabelString, &label, NULL);
    if (!label || !XmStringGetLtoR(label, XmFONTLIST_DEFAULT_TAG, &text)) return;
    SgFinderSetTextString((Widget)rec, text);
    activateTextChild(rec);
    XtFree(text);
}

static void rebuildHistoryMenu(SgFinderRecCompat *rec)
{
    Cardinal i;
    if (!rec || !rec->finder.history_menubar) return;
    for (i=0;i<FINDER_HISTORY_CAPACITY;++i) {
        if (rec->finder.history_item[i]) {
            XtDestroyWidget(rec->finder.history_item[i]);
            rec->finder.history_item[i]=NULL;
        }
    }
    for (i=0;i<rec->finder.history_count;++i) if (rec->finder.history[i]) {
        char name[32]; XmString label;
        sprintf(name,"historyItem%u",(unsigned)i);
        label=XmStringCreateLocalized(rec->finder.history[i]);
        rec->finder.history_item[i]=XtVaCreateManagedWidget(
            name,xmPushButtonWidgetClass,rec->finder.history_menubar,
            XmNlabelString,label,NULL);
        XmStringFree(label);
        XtAddCallback(rec->finder.history_item[i],XmNactivateCallback,
                      historyItemActivateCB,rec);
    }
}

Widget SgCreateFinder(Widget parent, char *name, ArgList args, Cardinal count)
{ return XtCreateWidget(name, sgFinderWidgetClass, parent, args, count); }

char *SgFinderGetTextString(Widget finder)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)finder;
    char *text; size_t len;
    if (!finder || !XtIsSubclass(finder,sgFinderWidgetClass) || !rec->finder.text) return NULL;
    text=XmTextFieldGetString(rec->finder.text);
    if (!text) return NULL;
    len=strlen(text);
    if (len && text[len-1]=='\n') text[len-1]='\0';
    return text;
}

void SgFinderSetTextString(Widget finder, char *text)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)finder;
    XmAnyCallbackStruct cb;
    if (!finder || !XtIsSubclass(finder,sgFinderWidgetClass) || !rec->finder.text) return;
    if (!text) text="";
    XmTextFieldSetString(rec->finder.text,text);
    XmTextFieldSetInsertionPosition(rec->finder.text,(XmTextPosition)strlen(text));
    updateZoomBarSections(rec);
    if (rec->finder.activate_callback) {
        cb.reason=XmCR_ACTIVATE; cb.event=NULL;
        XtCallCallbackList(finder,rec->finder.activate_callback,&cb);
    }
}

Widget SgFinderGetChild(Widget finder, int which)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)finder;
    if (!finder || !XtIsSubclass(finder,sgFinderWidgetClass)) return NULL;
    switch(which) {
    case SgFINDER_TEXT: return rec->finder.text;
    case SgFINDER_ZOOM_BAR: return rec->finder.zoom_bar;
    case SgFINDER_DROP_POCKET: return rec->finder.drop_pocket;
    case SgFINDER_HISTORY_MENUBAR: return rec->finder.history_menubar;
    default: return NULL;
    }
}

void SgFinderClearHistory(Widget finder)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)finder;
    if (!finder || !XtIsSubclass(finder,sgFinderWidgetClass) ||
        !rec->finder.use_history_menu || !rec->finder.history_menubar) return;
    clearHistoryStorage(rec,True);
}

void SgFinderAddHistoryItem(Widget finder, char *text)
{
    SgFinderRecCompat *rec=(SgFinderRecCompat *)finder;
    Cardinal i,found,count;
    if (!finder || !XtIsSubclass(finder,sgFinderWidgetClass) ||
        !rec->finder.use_history_menu || !rec->finder.history_menubar || !text || !*text) return;
    found=rec->finder.history_count;
    for(i=0;i<rec->finder.history_count;++i)
        if(rec->finder.history[i] && !strcmp(rec->finder.history[i],text)){found=i;break;}
    if(found<rec->finder.history_count){
        char *existing=rec->finder.history[found];
        for(i=found;i>0;--i) rec->finder.history[i]=rec->finder.history[i-1];
        rec->finder.history[0]=existing;
    } else {
        count=rec->finder.history_count;
        if(count==FINDER_HISTORY_CAPACITY){
            XtFree(rec->finder.history[FINDER_HISTORY_CAPACITY-1]);
            rec->finder.history[FINDER_HISTORY_CAPACITY-1]=NULL;
            count=FINDER_HISTORY_CAPACITY-1;
        }
        for(i=count;i>0;--i) rec->finder.history[i]=rec->finder.history[i-1];
        rec->finder.history[0]=XtNewString(text);
        if(rec->finder.history_count<FINDER_HISTORY_CAPACITY) ++rec->finder.history_count;
    }
    rebuildHistoryMenu(rec);
}

#else

typedef struct FinderFallbackState {
    Widget finder, text;
    struct FinderFallbackState *next;
} FinderFallbackState;
static FinderFallbackState *fallbackStates;
WidgetClass sgFinderWidgetClass=NULL;
#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void initializeFinderFallback(void){sgFinderWidgetClass=xmDrawingAreaWidgetClass;}
static FinderFallbackState *fallbackState(Widget finder,Boolean create)
{
    FinderFallbackState *s;
    for(s=fallbackStates;s;s=s->next) if(s->finder==finder) return s;
    if(!create||!finder) return NULL;
    s=(FinderFallbackState *)calloc(1,sizeof(*s)); if(!s) return NULL;
    s->finder=finder; s->text=XmCreateTextField(finder,"text",NULL,0); XtManageChild(s->text);
    s->next=fallbackStates; fallbackStates=s; return s;
}
Widget SgCreateFinder(Widget p,char *n,ArgList a,Cardinal c)
{Widget w=XtCreateWidget(n,sgFinderWidgetClass,p,a,c);(void)fallbackState(w,True);return w;}
char *SgFinderGetTextString(Widget w){FinderFallbackState*s=fallbackState(w,True);return s&&s->text?XmTextFieldGetString(s->text):NULL;}
void SgFinderSetTextString(Widget w,char*t){FinderFallbackState*s=fallbackState(w,True);if(s&&s->text)XmTextFieldSetString(s->text,t?t:"");}
Widget SgFinderGetChild(Widget w,int which){FinderFallbackState*s=fallbackState(w,True);return s&&which==SgFINDER_TEXT?s->text:NULL;}
void SgFinderClearHistory(Widget w){(void)w;}
void SgFinderAddHistoryItem(Widget w,char*t){(void)w;(void)t;}
#endif
