#ifndef _SGZOOMBARP_H_
#define _SGZOOMBARP_H_

#include <Sgm/ZoomBar.h>
#include <Xm/PrimitiveP.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Original SGI ZoomBar.o chains its Enter/Leave actions through these
 * private XmPrimitive action procedures. Some host Motif headers do not
 * publish prototypes for them even when the symbols are available.
 */
extern void _XmPrimitiveEnter(Widget, XEvent *, String *, Cardinal *);
extern void _XmPrimitiveLeave(Widget, XEvent *, String *, Cardinal *);

typedef struct _SgZoomBarClassPart
{
    int foo;
} SgZoomBarClassPart;

typedef struct _SgZoomBarClassRec {
    CoreClassPart core_class;
    XmPrimitiveClassPart primitive_class;
    SgZoomBarClassPart zoombar_class;
} SgZoomBarClassRec;

externalref SgZoomBarClassRec sgZoomBarClassRec;

typedef struct _SgZoomBarPart
{
    XtCallbackList activate_callback;
    XtCallbackList arm_callback;
    XtCallbackList disarm_callback;
    Boolean armed;
    int click_count;
    Dimension *section_widths;
    int section_count;
    int pushed_section;
    int located_section;
} SgZoomBarPart;

typedef struct _SgZoomBarRec {
    CorePart core;
    XmPrimitivePart primitive;
    SgZoomBarPart zoomBar;
} SgZoomBarRec;

#ifdef __cplusplus
}
#endif

#endif
