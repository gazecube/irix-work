#ifndef SGM_FINDER_H
#define SGM_FINDER_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgFinderWidgetClass;

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
