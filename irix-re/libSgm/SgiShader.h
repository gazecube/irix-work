#ifndef SGM_SGI_SHADER_H
#define SGM_SGI_SHADER_H

#include <Xm/Xm.h>
#include <X11/Xlib.h>

/*
 * IRIX/Motif shader compatibility used by reconstructed libSgm widgets.
 *
 * The original implementation lives in IRIX libXm.so.1 as _sgFindShader()
 * and _sgFindGC().  This interface deliberately does not export those private
 * libXm names: libSgm owns only behavior-compatible helpers.
 */
GC SgShaderGetGC(Widget widget, Pixel background, int shader_index);

/*
 * Reproduce the default SGI XmManager shadow renderer for one visual state.
 * The state table comes from libXm.a(Manager.o) _sgManagerData and the layer
 * loop comes from libXm.a(Draw.o) _RenderShadowsDefault.
 */
void SgRenderManagerShadows(Widget widget,
                            Drawable drawable,
                            Position x,
                            Position y,
                            Dimension width,
                            Dimension height,
                            int visual_state,
                            Dimension shadow_thickness,
                            unsigned int shadow_style);

#endif
