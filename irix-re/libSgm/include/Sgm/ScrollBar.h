#ifndef SGM_SCROLL_BAR_H
#define SGM_SCROLL_BAR_H

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

extern WidgetClass sgScrollBarWidgetClass;

Widget SgCreateScrollBar(
    Widget parent,
    char *name,
    ArgList args,
    Cardinal count);

void SgScrollBarGetValues(
    Widget widget,
    int *value,
    int *slider_size,
    int *increment,
    int *page_increment);

void SgScrollBarSetValues(
    Widget widget,
    int value,
    int slider_size,
    int increment,
    int page_increment,
    Boolean notify);

void SgScrollBarRedisplay(Widget widget);

#ifdef __cplusplus
}
#endif

#endif
