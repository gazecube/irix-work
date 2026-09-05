#ifndef IRIX_RE_CLOGIN_IMDFTRVISUALCOMPAT_H
#define IRIX_RE_CLOGIN_IMDFTRVISUALCOMPAT_H

#include <X11/Intrinsic.h>

/*
 * Binary-recovered public entry point from IRIX libimdFtrIcon.so.
 *
 * The first four argument roles are proven from Clogin::readyToDisplay and
 * imdFtrChooseVisualArgs disassembly. The fifth argument is a separate
 * 32-bit output written by internalChooseVisual; Clogin passes storage for it
 * but never reads it afterward. Its original semantic typedef/name has not
 * yet been recovered, so int * is a reconstructed ABI-compatible host type.
 */
Boolean imdFtrChooseVisualArgs(Widget widget,
                               int visualType,
                               ArgList args,
                               Cardinal *count,
                               int *auxVisualValue);

#endif
