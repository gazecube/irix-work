#ifndef _SGZOOMBAR_H_
#define _SGZOOMBAR_H_

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SgIsZoomBar
#define SgIsZoomBar(w) XtIsSubclass(w, sgZoomBarWidgetClass)
#endif

externalref WidgetClass sgZoomBarWidgetClass;

typedef struct _SgZoomBarClassRec *SgZoomBarWidgetClass;
typedef struct _SgZoomBarRec      *SgZoomBarWidget;

#define SgNsectionWidths "sectionWidths"
#define SgCSectionWidths "SectionWidths"
#define SgNsectionCount  "sectionCount"
#define SgCSectionCount  "SectionCount"

extern Widget SgCreateZoomBar(Widget parent, char *name,
                              ArgList arglist, Cardinal argcount);

#ifdef __cplusplus
}
#endif

#endif
