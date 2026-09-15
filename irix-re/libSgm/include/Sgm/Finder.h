#ifndef _SGFINDER_H_
#define _SGFINDER_H_

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SgIsFinder
#define SgIsFinder(w) XtIsSubclass(w, sgFinderWidgetClass)
#endif

#define SgNaddHistoryOnActivate "addHistoryOnActivate"
#define SgCAddHistoryOnActivate "AddHistoryOnActivate"
#define SgNseparator "separator"
#define SgCSeparator "Separator"
#define SgNsetTextSectionFunc "setTextSectionFunc"
#define SgCSetTextSectionFunc "SetTextSectionFunc"
#define SgNhistoryPixmap "historyPixmap"
#define SgCHistoryPixmap "HistoryPixmap"
#define SgNinsensitiveHistoryPixmap "insensitiveHistoryPixmap"
#define SgCInsensitiveHistoryPixmap "InsensitiveHistoryPixmap"
#define SgNuseDropPocket "useDropPocket"
#define SgCUseDropPocket "UseDropPocket"
#define SgNuseHistoryMenu "useHistoryMenu"
#define SgCUseHistoryMenu "UseHistoryMenu"

#define SgFINDER_TEXT 0
#define SgFINDER_ZOOM_BAR 1
#define SgFINDER_DROP_POCKET 2
#define SgFINDER_HISTORY_MENUBAR 3

externalref WidgetClass sgFinderWidgetClass;

typedef struct _SgFinderClassRec *SgFinderWidgetClass;
typedef struct _SgFinderRec      *SgFinderWidget;
typedef void (*SgSetTextFunc)(Widget ft, int section);

extern Widget SgCreateFinder(Widget parent, char *name,
                             ArgList arglist, Cardinal argcount);
extern void SgFinderAddHistoryItem(Widget w, char *str);
extern void SgFinderClearHistory(Widget w);
extern char *SgFinderGetTextString(Widget w);
extern void SgFinderSetTextString(Widget w, char *value);
extern Widget SgFinderGetChild(Widget w, int child);

#ifdef __cplusplus
}
#endif

#endif
