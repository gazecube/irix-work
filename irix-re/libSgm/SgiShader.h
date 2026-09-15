#ifndef SGM_SGI_SHADER_H
#define SGM_SGI_SHADER_H

#include <Xm/Xm.h>
#include <X11/Xlib.h>

/*
 * IRIX/Motif shader compatibility used by reconstructed libSgm widgets.
 *
 * The original implementation lives in IRIX libXm.so.1 as _sgFindShader()
 * and _sgFindGC().  This interface deliberately does not export those private
 * libXm names: libSgm owns only a behavior-compatible helper.
 */
GC SgShaderGetGC(Widget widget, Pixel background, int shader_index);

#endif
