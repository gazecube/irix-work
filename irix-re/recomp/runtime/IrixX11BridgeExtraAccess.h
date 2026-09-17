#ifndef IRIX_X11_BRIDGE_EXTRA_ACCESS_H
#define IRIX_X11_BRIDGE_EXTRA_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr);
GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr);
XFontStruct *irix_x11_primary_font_from_guest(IrixGuestAddr addr);
IrixGuestAddr irix_x11_primary_font_to_guest(IrixGuestRuntime *rt, XFontStruct *font);

#endif
