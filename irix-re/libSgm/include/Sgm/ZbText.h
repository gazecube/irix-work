#ifndef _SGZBTEXT_H_
#define _SGZBTEXT_H_

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SgIsZbText
#define SgIsZbText(w) XtIsSubclass(w, sgZbTextWidgetClass)
#endif

#define SgZBTEXT_TEXT 0
#define SgZBTEXT_ZOOM_BAR 1

externalref WidgetClass sgZbTextWidgetClass;

typedef struct _SgZbTextClassRec *SgZbTextWidgetClass;
typedef struct _SgZbTextRec      *SgZbTextWidget;

extern Widget SgCreateZbText(Widget parent, char *name,
                             ArgList arglist, Cardinal argcount);
extern Widget SgZbTextGetChild(Widget w, int child);

#ifdef __cplusplus
}
#endif

#endif
