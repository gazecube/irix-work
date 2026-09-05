#ifndef SGM_VISUAL_DRAWING_A_H
#define SGM_VISUAL_DRAWING_A_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgVisualDrawingAreaWidgetClass;

#define SgNinstallColormap "installColormap"
#define SgCInstallColormap "InstallColormap"
#define SgNditherBackground "ditherBackground"
#define SgCDitherBackground "DitherBackground"

Widget SgCreateVisualDrawingArea(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

void SgVisualDrawingAreaInstallColormap(Widget widget);
void SgVisualDrawingAreaUninstallColormap(Widget widget);

#ifdef __cplusplus
}
#endif

#endif
