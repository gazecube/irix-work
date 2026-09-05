#ifndef SGM_THUMB_WHEEL_H
#define SGM_THUMB_WHEEL_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgThumbWheelWidgetClass;

Widget SgCreateThumbWheel(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

void SgThumbWheelGetValue(Widget widget, int *value);
void SgThumbWheelSetValue(Widget widget, int value);

#ifdef __cplusplus
}
#endif

#endif
