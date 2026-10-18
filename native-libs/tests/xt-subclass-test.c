#define _DEFAULT_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/CoreP.h>
#include <X11/Shell.h>
typedef struct { int dummy; } MyClassPart;
typedef struct _MyClassRec { CoreClassPart core_class; MyClassPart my_class; } MyClassRec;
typedef struct { int tag; } MyPart;
typedef struct _MyRec { CorePart core; MyPart my; } MyRec;
static int inited, exposed, destroyed;
static void Init(Widget req, Widget new_w, ArgList a, Cardinal *n) { ((MyRec*)new_w)->my.tag = 42; inited++; }
static void Redisplay(Widget w, XEvent *e, Region r) { exposed++; }
static void Destroy(Widget w) { destroyed++; }
MyClassRec myClassRec = { .core_class = {
  .superclass = (WidgetClass)&widgetClassRec, .class_name = "My", .widget_size = sizeof(MyRec),
  .class_inited = FALSE, .initialize = Init, .realize = XtInheritRealize,
  .compress_motion = TRUE, .compress_exposure = TRUE, .compress_enterleave = TRUE,
  .destroy = Destroy, .resize = XtInheritResize, .expose = Redisplay,
  .accept_focus = XtInheritAcceptFocus, .version = XtVersion,
  .query_geometry = XtInheritQueryGeometry, .display_accelerator = XtInheritDisplayAccelerator }, { 0 } };
int main(int argc, char **argv) {
  XtAppContext app; Widget top, w; Arg args[2];
  top = XtAppInitialize(&app, "T", NULL, 0, &argc, argv, NULL, NULL, 0);
  XtSetArg(args[0], "width", 50); XtSetArg(args[1], "height", 50);
  w = XtCreateManagedWidget("w", (WidgetClass)&myClassRec, top, args, 2);
  XtRealizeWidget(top);
  for (int i = 0; i < 20; i++) { XEvent ev; if (XtAppPending(app)) { XtAppNextEvent(app, &ev); XtDispatchEvent(&ev); } else usleep(20000); }
  printf("tag=%d inited=%d exposed=%d isSub=%d\n", ((MyRec*)w)->my.tag, inited, exposed, XtIsSubclass(w, (WidgetClass)&myClassRec));
  XtDestroyWidget(top); printf("destroyed=%d\n", destroyed); return 0; }
