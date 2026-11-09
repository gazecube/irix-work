#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <stdlib.h>
extern Widget SgCreateDial(Widget, char *, ArgList, Cardinal);
void *__rld_obj_head;
/* dialtest3 SIZE VALUE [START RANGE MARKERS MAX] : one dial */
int main(int argc, char **argv)
{
    XtAppContext app; Widget top, form, d; Arg a[14]; int n = 0;
    int sz = argc > 1 ? atoi(argv[1]) : 64, val = argc > 2 ? atoi(argv[2]) : 0;
    int start = argc > 3 ? atoi(argv[3]) : 0, range = argc > 4 ? atoi(argv[4]) : 360, marks = argc > 5 ? atoi(argv[5]) : 16, max = argc > 6 ? atoi(argv[6]) : 360;
    top = XtVaOpenApplication(&app, "Dialtest3", NULL, 0, &argc, argv, NULL, sessionShellWidgetClass, NULL);
    form = XmCreateForm(top, "form", NULL, 0); XtManageChild(form);
    XtSetArg(a[n], XmNx, 8); n++; XtSetArg(a[n], XmNy, 8); n++;
    XtSetArg(a[n], XmNwidth, sz); n++; XtSetArg(a[n], XmNheight, sz); n++;
    XtSetArg(a[n], XmNminimum, 0); n++; XtSetArg(a[n], XmNmaximum, max); n++; XtSetArg(a[n], XmNvalue, val); n++;
    XtSetArg(a[n], "startAngle", start); n++; XtSetArg(a[n], "angleRange", range); n++; XtSetArg(a[n], "dialMarkers", marks); n++;
    d = SgCreateDial(form, "dial", a, n); XtManageChild(d);
    XtRealizeWidget(top);
    for (;;) { XEvent ev; XtAppNextEvent(app, &ev); XtDispatchEvent(&ev); }
}
