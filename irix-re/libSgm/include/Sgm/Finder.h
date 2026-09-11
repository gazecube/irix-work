#ifndef SGM_FINDER_H
#define SGM_FINDER_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgFinderWidgetClass;

/* Original public Finder resource names. */
#define SgNhistoryPixmap       "historyPixmap"
#define SgNseparator           "separator"
#define SgNsetTextSectionFunc  "setTextSectionFunc"
#define SgNuseDropPocket       "useDropPocket"
#define SgNuseHistoryMenu      "useHistoryMenu"

/*
 * Original IRIX 6.5.5 Sgm/Finder.h child selector ABI.
 * These values are source-header confirmed, not reconstructed guesses.
 */
#define SgFINDER_TEXT              0
#define SgFINDER_ZOOM_BAR          1
#define SgFINDER_DROP_POCKET       2
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
