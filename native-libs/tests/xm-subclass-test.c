#define _DEFAULT_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <Xm/Xm.h>
#include <Xm/PrimitiveP.h>
#include <Xm/Form.h>
#include <Xm/PushB.h>
typedef struct { int dummy; } MyClassPart;
typedef struct _MyClassRec { CoreClassPart core_class; XmPrimitiveClassPart primitive_class; MyClassPart my; } MyClassRec;
typedef struct { int tag; } MyPart;
typedef struct _MyRec { CorePart core; XmPrimitivePart primitive; MyPart my; } MyRec;
static int inited, redisplayed, clicked;
static void Init(Widget r, Widget n, ArgList a, Cardinal *c) { ((MyRec*)n)->my.tag = 7; inited++; }
static void Redisplay(Widget w, XEvent *e, Region r) { redisplayed++; }
MyClassRec myClassRec = { .core_class = { .superclass = (WidgetClass)&xmPrimitiveClassRec, .class_name = "My",
  .widget_size = sizeof(MyRec), .initialize = Init, .realize = XtInheritRealize, .compress_motion = TRUE,
  .compress_exposure = TRUE, .compress_enterleave = TRUE, .resize = XtInheritResize, .expose = Redisplay,
  .version = XtVersion, .query_geometry = XtInheritQueryGeometry, .display_accelerator = XtInheritDisplayAccelerator },
  .primitive_class = { .border_highlight = XmInheritBorderHighlight, .border_unhighlight = XmInheritBorderUnhighlight,
  .translations = XtInheritTranslations, .arm_and_activate = XmInheritArmAndActivate } };
static void cb(Widget w, XtPointer c, XtPointer d) { clicked++; }
int main(int argc, char **argv) {
  XtAppContext app; Widget top, form, pb, my; Arg a[4]; int n;
  top = XtAppInitialize(&app, "T", NULL, 0, &argc, argv, NULL, NULL, 0);
  form = XmCreateForm(top, "form", NULL, 0);
  XmString s = XmStringCreateSimple("Hello");
  n = 0; XtSetArg(a[n], XmNlabelString, s); n++;
  pb = XmCreatePushButton(form, "pb", a, n); XtManageChild(pb);
  XtAddCallback(pb, XmNactivateCallback, cb, NULL);
  n = 0; XtSetArg(a[n], XmNwidth, 40); n++; XtSetArg(a[n], XmNheight, 20); n++;
  XtSetArg(a[n], XmNtopAttachment, XmATTACH_WIDGET); n++; XtSetArg(a[n], XmNtopWidget, pb); n++;
  my = XtCreateManagedWidget("my", (WidgetClass)&myClassRec, form, a, n);
  XtManageChild(form); XtRealizeWidget(top);
  for (int i = 0; i < 30; i++) { XEvent ev; if (XtAppPending(app)) { XtAppNextEvent(app, &ev); XtDispatchEvent(&ev); } else usleep(20000); }
  XtCallCallbacks(pb, XmNactivateCallback, NULL);
  printf("pushbutton=%d class=%s tag=%d inited=%d redisplayed=%d clicked=%d subclassOfPrimitive=%d w=%d h=%d\n", XmIsPushButton(pb), XtClass(pb)->core_class.class_name, ((MyRec*)my)->my.tag, inited, redisplayed, clicked, XtIsSubclass(my, xmPrimitiveWidgetClass), XtWidth(pb), XtHeight(pb));
  return 0; }
