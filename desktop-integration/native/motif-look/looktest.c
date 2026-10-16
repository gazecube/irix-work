#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Label.h>
#include <Xm/Text.h>
#include <Xm/Scale.h>
#include <Xm/ScrollBar.h>
#include <Xm/Frame.h>
#include <Xm/List.h>
#include <Xm/Form.h>
#include <Xm/CascadeB.h>

int main(int argc, char **argv)
{
    Widget top, form, bar, menu, cb, col, w, frame;
    XtAppContext app;
    XmString s;
    top = XtVaAppInitialize(&app, "LookTest", NULL, 0, &argc, argv, NULL,
                            XmNwidth, 360, XmNheight, 420, NULL);
    form = XmCreateForm(top, "form", NULL, 0);
    XtManageChild(form);
    bar = XmCreateMenuBar(form, "bar", NULL, 0);
    XtVaSetValues(bar, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    menu = XmCreatePulldownMenu(bar, "filemenu", NULL, 0);
    XtVaCreateManagedWidget("Open", xmPushButtonWidgetClass, menu, NULL);
    XtVaCreateManagedWidget("Save", xmPushButtonWidgetClass, menu, NULL);
    cb = XtVaCreateManagedWidget("File", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, menu, NULL);
    XtManageChild(bar);
    col = XtVaCreateManagedWidget("col", xmRowColumnWidgetClass, form,
          XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, bar,
          XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
          XmNbottomAttachment, XmATTACH_FORM, XmNmarginWidth, 8, XmNmarginHeight, 8,
          XmNspacing, 6, NULL);
    XtVaCreateManagedWidget("Push Button", xmPushButtonWidgetClass, col, NULL);
    w = XtVaCreateManagedWidget("Default Button", xmPushButtonWidgetClass, col,
                                XmNshowAsDefault, 1, NULL);
    XtVaCreateManagedWidget("Toggle (on)", xmToggleButtonWidgetClass, col, XmNset, True, NULL);
    XtVaCreateManagedWidget("Toggle (off)", xmToggleButtonWidgetClass, col, NULL);
    XtVaCreateManagedWidget("Radio", xmToggleButtonWidgetClass, col,
                            XmNindicatorType, XmONE_OF_MANY, XmNset, True, NULL);
    XtVaCreateManagedWidget("A label", xmLabelWidgetClass, col, NULL);
    frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, col, NULL);
    w = XmCreateText(frame, "text", NULL, 0);
    XtVaSetValues(w, XmNvalue, "Text field", XmNcolumns, 20, NULL);
    XtManageChild(w);
    XtVaCreateManagedWidget("scale", xmScaleWidgetClass, col, XmNorientation, XmHORIZONTAL,
                            XmNvalue, 40, XmNshowValue, True, NULL);
    XtVaCreateManagedWidget("sb", xmScrollBarWidgetClass, col, XmNorientation, XmHORIZONTAL,
                            XmNsliderSize, 30, NULL);
    s = XmStringCreateLocalized("Item");
    {
        Arg a[4]; int n = 0;
        XmString items[3];
        items[0] = XmStringCreateLocalized("one");
        items[1] = XmStringCreateLocalized("two");
        items[2] = XmStringCreateLocalized("three");
        XtSetArg(a[n], XmNitems, items); n++;
        XtSetArg(a[n], XmNitemCount, 3); n++;
        XtSetArg(a[n], XmNvisibleItemCount, 3); n++;
        w = XmCreateScrolledList(col, "list", a, n);
        XtManageChild(w);
    }
    (void)s; (void)cb;
    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}
