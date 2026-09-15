/*
 * Broad ABI-first reconstruction of IRIX 6.5.0 libSgm.so.1.
 *
 * The initial implementation deliberately maps the recovered SGI widget
 * classes onto the nearest Motif classes. This is not the final behavior;
 * it gives existing IID consumers real class handles and constructors while
 * each SGI widget is reconstructed from the original binary.
 */
#include <Xm/Xm.h>
#include <Xm/BulletinB.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/LabelG.h>
#include <Xm/List.h>
#include <Xm/PanedW.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/ScrollBar.h>
#include <Xm/ScrolledW.h>
#include <Xm/Text.h>
#include <X11/StringDefs.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

WidgetClass glxDrawWidgetClass = (WidgetClass)0;
WidgetClass glwMDrawingAreaWidgetClass = (WidgetClass)0;
WidgetClass sgColorChooserWidgetClass = (WidgetClass)0;
WidgetClass glxMDrawWidgetClass = (WidgetClass)0;
WidgetClass sgDropPocketWidgetClass = (WidgetClass)0;
extern WidgetClass sgDynaMenuWidgetClass;
WidgetClass sgZbTextWidgetClass = (WidgetClass)0;
extern WidgetClass sgZoomBarWidgetClass;
WidgetClass sgColumnWidgetClass = (WidgetClass)0;
WidgetClass sgOglColorChooserWidgetClass = (WidgetClass)0;
WidgetClass sgArcWidgetClass = (WidgetClass)0;
WidgetClass sgDialWidgetClass = (WidgetClass)0;
WidgetClass sgGraphWidgetClass = (WidgetClass)0;
WidgetClass sgHorzPanedWindowWidgetClass = (WidgetClass)0;
extern WidgetClass sgGridWidgetClass;
WidgetClass sgIconGadgetClass = (WidgetClass)0;
WidgetClass sgListWidgetClass = (WidgetClass)0;
WidgetClass sgRubberBoardWidgetClass = (WidgetClass)0;
WidgetClass sgScrollBarWidgetClass = (WidgetClass)0;
WidgetClass sgSpringBoxWidgetClass = (WidgetClass)0;
extern WidgetClass sgThumbWheelWidgetClass;
extern WidgetClass sgVisualDrawingAreaWidgetClass;
WidgetClass sgOscWidgetClass = (WidgetClass)0;

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void SgmCompatInitClasses(void)
{
    glxDrawWidgetClass = xmDrawingAreaWidgetClass;
    glwMDrawingAreaWidgetClass = xmDrawingAreaWidgetClass;
    sgColorChooserWidgetClass = xmFormWidgetClass;
    glxMDrawWidgetClass = xmDrawingAreaWidgetClass;
    sgDropPocketWidgetClass = xmDrawingAreaWidgetClass;
    sgZbTextWidgetClass = xmFormWidgetClass;
    sgColumnWidgetClass = xmFormWidgetClass;
    sgOglColorChooserWidgetClass = xmFormWidgetClass;
    sgArcWidgetClass = xmDrawingAreaWidgetClass;
    sgDialWidgetClass = xmScaleWidgetClass;
    sgGraphWidgetClass = xmBulletinBoardWidgetClass;
    sgHorzPanedWindowWidgetClass = xmPanedWindowWidgetClass;
    sgIconGadgetClass = xmLabelGadgetClass;
    sgListWidgetClass = xmListWidgetClass;
    sgRubberBoardWidgetClass = xmBulletinBoardWidgetClass;
    sgScrollBarWidgetClass = xmScrollBarWidgetClass;
    sgSpringBoxWidgetClass = xmBulletinBoardWidgetClass;
    sgOscWidgetClass = xmDrawingAreaWidgetClass;
}

static Widget sgmCreate(Widget parent, char *name, ArgList args,
                        Cardinal count, WidgetClass klass, Boolean managed)
{
    if (!klass)
        klass = xmDrawingAreaWidgetClass;

    if (managed)
        return XtCreateManagedWidget(name, klass, parent, args, count);

    return XtCreateWidget(name, klass, parent, args, count);
}

#define SGM_CREATOR(name, klass) \
Widget name(Widget parent, char *widgetName, ArgList args, Cardinal count) \
{ return sgmCreate(parent, widgetName, args, count, klass, False); }

SGM_CREATOR(SgCreateColorChooser, sgColorChooserWidgetClass)
SGM_CREATOR(SgCreateDropPocket, sgDropPocketWidgetClass)
SGM_CREATOR(SgCreateZbText, sgZbTextWidgetClass)
SGM_CREATOR(SgCreateColumn, sgColumnWidgetClass)
SGM_CREATOR(SgOglCreateColorChooser, sgOglColorChooserWidgetClass)
SGM_CREATOR(SgCreateDial, sgDialWidgetClass)
SGM_CREATOR(SgCreateGraph, sgGraphWidgetClass)
SGM_CREATOR(SgCreateArc, sgArcWidgetClass)
SGM_CREATOR(SgCreateIconGadget, sgIconGadgetClass)
SGM_CREATOR(SgCreateList, sgListWidgetClass)
SGM_CREATOR(SgCreateRubberBoard, sgRubberBoardWidgetClass)
SGM_CREATOR(SgCreateScrollBar, sgScrollBarWidgetClass)
SGM_CREATOR(SgCreateSpringBox, sgSpringBoxWidgetClass)
SGM_CREATOR(SgCreateOsc, sgOscWidgetClass)
SGM_CREATOR(SgCreateHorzPanedWindow, sgHorzPanedWindowWidgetClass)
SGM_CREATOR(GlxCreateMDraw, glxMDrawWidgetClass)

Widget SgCreateManagedGraph(Widget parent, char *name, ArgList args,
                            Cardinal count)
{
    return sgmCreate(parent, name, args, count, sgGraphWidgetClass, True);
}

Widget SgCreateColorChooserDialog(Widget parent, char *name, ArgList args,
                                  Cardinal count)
{
    return XmCreateFormDialog(parent, name, args, count);
}

Widget SgOglCreateColorChooserDialog(Widget parent, char *name, ArgList args,
                                     Cardinal count)
{
    return XmCreateFormDialog(parent, name, args, count);
}

Widget SgCreateColumnDialog(Widget parent, char *name, ArgList args,
                            Cardinal count)
{
    return XmCreateFormDialog(parent, name, args, count);
}

Widget SgCreateRubberBoardDialog(Widget parent, char *name, ArgList args,
                                 Cardinal count)
{
    return XmCreateFormDialog(parent, name, args, count);
}

Widget SgCreateSpringBoxDialog(Widget parent, char *name, ArgList args,
                               Cardinal count)
{
    return XmCreateFormDialog(parent, name, args, count);
}

Widget SgCreateScrolledList(Widget parent, char *name, ArgList args,
                            Cardinal count)
{
    return XmCreateScrolledList(parent, name, args, count);
}

Widget SgCreateScrolledGraph(Widget parent, char *name, ArgList args,
                             Cardinal count)
{
    Widget sw = XmCreateScrolledWindow(parent, name, args, count);
    Widget graph = XtCreateWidget(name, sgGraphWidgetClass, sw, 0, 0);
    XtManageChild(graph);
    return graph;
}

Widget SgCreateMediaViewer(Widget parent, char *name, ArgList args,
                           Cardinal count)
{
    return XmCreateForm(parent, name, args, count);
}

void SgDialGetValue(Widget w, int *value)
{
    if (!value)
        return;

    *value = 0;
    if (w)
        XmScaleGetValue(w, value);
}

void SgDialSetValue(Widget w, int value)
{
    if (w)
        XmScaleSetValue(w, value);
}

void SgScrollBarGetValues(Widget w, int *value, int *slider,
                          int *increment, int *pageIncrement)
{
    if (w)
        XmScrollBarGetValues(w, value, slider, increment, pageIncrement);
}

void SgScrollBarSetValues(Widget w, int value, int slider,
                          int increment, int pageIncrement, Boolean notify)
{
    if (w)
        XmScrollBarSetValues(w, value, slider, increment, pageIncrement,
                             notify);
}

void SgScrollBarRedisplay(Widget w)
{
    if (w && XtIsRealized(w))
        XClearArea(XtDisplay(w), XtWindow(w), 0, 0, 0, 0, True);
}
