/*
 * Host Motif adaptation for the binary-reconstructed SgVisualDrawingArea.
 *
 * IRIX 6.5.0 stores class_initialize == NULL and a NULLQUARK Motif base-class
 * extension in the file image. OpenMotif 2.3.x expects a class owning an
 * XmBaseClassExtRec to bind record_type to XmQmotif during class
 * initialization. Keep that host-only requirement out of the recovered SGI
 * lifecycle proper.
 */
#include <Xm/Xm.h>
#include <X11/IntrinsicP.h>

#if defined(SGM_HAVE_BASECLASSP_H)
#include <Xm/BaseClassP.h>

extern WidgetClass sgVisualDrawingAreaWidgetClass;

static void sgmVisualDrawingAreaHostClassInitialize(void)
{
    WidgetClass klass = sgVisualDrawingAreaWidgetClass;
    XmBaseClassExt extension;

    if (!klass)
        return;

    extension = (XmBaseClassExt)klass->core_class.extension;
    if (extension)
        extension->record_type = XmQmotif;
}

#if defined(__GNUC__)
__attribute__((constructor))
#endif
static void sgmInstallVisualDrawingAreaHostClassInitialize(void)
{
    if (sgVisualDrawingAreaWidgetClass)
        sgVisualDrawingAreaWidgetClass->core_class.class_initialize =
            sgmVisualDrawingAreaHostClassInitialize;
}

#endif
