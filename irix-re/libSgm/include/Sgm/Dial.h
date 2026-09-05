#ifndef SGM_DIAL_H
#define SGM_DIAL_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgDialWidgetClass;

Widget SgCreateDial(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

void SgDialGetValue(Widget widget, int *value);
void SgDialSetValue(Widget widget, int value);

#ifdef __cplusplus
}
#endif

#endif
