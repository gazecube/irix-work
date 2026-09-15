/*
 * Primitive class-extension records exported by original IRIX libSgm.
 *
 * Recovered from the N32 static archive object data and relocations:
 *   libSgm.a(DropPocket.o) _SgDropPocketPrimClassExtRec, 28 bytes
 *   libSgm.a(ZoomBar.o)    _SgZoomBPrimClassExtRec,       28 bytes
 *
 * Both objects are XmPrimitiveClassExtRec values with the same contents:
 *   next_extension      = NULL
 *   record_type         = NULLQUARK
 *   version             = XmPrimitiveClassExtVersion
 *   record_size         = sizeof(XmPrimitiveClassExtRec)
 *   widget_baseline     = _XtInherit
 *   widget_display_rect = _XtInherit
 *   widget_margins      = NULL
 */

#include <Xm/PrimitiveP.h>
#include <X11/IntrinsicP.h>

XmPrimitiveClassExtRec _SgDropPocketPrimClassExtRec =
{
    NULL,
    NULLQUARK,
    XmPrimitiveClassExtVersion,
    sizeof(XmPrimitiveClassExtRec),
    (XmWidgetBaselineProc)_XtInherit,
    (XmWidgetDisplayRectProc)_XtInherit,
    NULL
};

XmPrimitiveClassExtRec _SgZoomBPrimClassExtRec =
{
    NULL,
    NULLQUARK,
    XmPrimitiveClassExtVersion,
    sizeof(XmPrimitiveClassExtRec),
    (XmWidgetBaselineProc)_XtInherit,
    (XmWidgetDisplayRectProc)_XtInherit,
    NULL
};
