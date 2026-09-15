#ifndef _SGDYNAMENU_H_
#define _SGDYNAMENU_H_

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SgNhistoryListItems "historyListItems"
#define SgNmaxHistoryCount "maxHistoryCount"
#define SgNhistoryListItemCount "historyListItemCount"
#define SgCHistoryListItems "HistoryListItems"
#define SgCMaxHistoryCount "MaxHistoryCount"
#define SgCHistoryListItemCount "HistoryListItemCount"
#define SgCR_DYNA_PUSH 3134
#define SgNdynaPushCallback "dynaPushCallback"

#ifndef SgIsDynaMenu
#define SgIsDynaMenu(w) XtIsSubclass(w, sgDynaMenuWidgetClass)
#endif

externalref WidgetClass sgDynaMenuWidgetClass;
typedef struct _SgDynaMenuClassRec *SgDynaMenuWidgetClass;
typedef struct _SgDynaMenuRec      *SgDynaMenuWidget;

typedef struct
{
    int reason;
    XEvent *event;
    XtArgVal button_number;
} SgDynaMenuCallbackStruct;

extern Widget SgCreateDynaMenu(Widget parent, char *name,
                               ArgList arglist, Cardinal argcount);
extern void SgDynaMenuAddHistoryItem(SgDynaMenuWidget ft, char *str);
extern void SgDynaMenuClearHistory(SgDynaMenuWidget ft);

#ifdef __cplusplus
}
#endif

#endif
