/*
 * InputOnly.c++ - decompilation from IRIX 6.5 /usr/sbin/fm
 *
 * CreateInputOnly__GP10_WidgetRecPCcP3ArgUi
 *   address 0x100246c0, size 72
 *
 * The MIPS body is a thin argument-reordering wrapper around XtCreateWidget.
 */

#include <stdint.h>

typedef void *Widget;
typedef void *WidgetClass;
struct Arg;
typedef unsigned int Cardinal;

extern "C" {
extern WidgetClass inputOnlyWidgetClass;
extern Widget XtCreateWidget(const char *name, WidgetClass widget_class,
                             Widget parent, Arg *args, Cardinal num_args);

Widget CreateInputOnly__GP10_WidgetRecPCcP3ArgUi(
    Widget parent, const char *name, Arg *args, Cardinal num_args)
{
    return XtCreateWidget(name, inputOnlyWidgetClass, parent, args, num_args);
}

} /* extern "C" */
