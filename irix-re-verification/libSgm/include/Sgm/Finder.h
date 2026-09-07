#ifndef SGM_FINDER_H
#define SGM_FINDER_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgFinderWidgetClass;

/*
 * Public Finder resource names documented by SGI.  The child-selector
 * numeric values below are reconstructed from the documented public order;
 * verify against the original IRIX Finder.h before treating them as frozen
 * binary ABI constants.
 */
#define SgNhistoryPixmap       "historyPixmap"
#define SgNseparator           "separator"
#define SgNsetTextSectionFunc  "setTextSectionFunc"
#define SgNuseDropPocket       "useDropPocket"
#define SgNuseHistoryMenu      "useHistoryMenu"

#define SgFINDER_DROP_POCKET       0
#define SgFINDER_TEXT              1
#define SgFINDER_ZOOM_BAR          2
#define SgFINDER_HISTORY_MENUBAR   3

typedef void (*SgSetTextFunc)(Widget finder, int section);

Widget SgCreateFinder(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

char *SgFinderGetTextString(Widget finder);
void SgFinderSetTextString(Widget finder, char *text);
Widget SgFinderGetChild(Widget finder, int which);
void SgFinderAddHistoryItem(Widget finder, char *text);
void SgFinderClearHistory(Widget finder);

#ifdef __cplusplus
}
#endif

#endif
