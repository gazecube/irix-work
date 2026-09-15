#ifndef SGM_FINDER_H
#define SGM_FINDER_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgFinderWidgetClass;
extern WidgetClass sgZbTextWidgetClass;
extern WidgetClass sgZoomBarWidgetClass;

/* Original public Finder resource names. */
#define SgNhistoryPixmap       "historyPixmap"
#define SgNseparator           "separator"
#define SgNsetTextSectionFunc  "setTextSectionFunc"
#define SgNuseDropPocket       "useDropPocket"
#define SgNuseHistoryMenu      "useHistoryMenu"

/* Binary-confirmed SgZoomBar resources from IRIX libSgm.so.1. */
#define SgNsectionWidths       "sectionWidths"
#define SgNsectionCount        "sectionCount"

/*
 * Original IRIX 6.5.5 Sgm/Finder.h child selector ABI.
 * These values are source-header confirmed, not reconstructed guesses.
 */
#define SgFINDER_TEXT              0
#define SgFINDER_ZOOM_BAR          1
#define SgFINDER_DROP_POCKET       2
#define SgFINDER_HISTORY_MENUBAR   3

/* Binary-confirmed private SgZbText child selector behavior. */
#define SgZBTEXT_TEXT              0
#define SgZBTEXT_ZOOM_BAR          1

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

/* Private widget exported by the original libSgm and used by SgFinder. */
Widget SgCreateZbText(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);
Widget SgZbTextGetChild(Widget zb_text, int which);

Widget SgCreateZoomBar(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

#ifdef __cplusplus
}
#endif

#endif
