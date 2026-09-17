#ifndef IRIX_X11_BRIDGE_EXTRA_ACCESS_H
#define IRIX_X11_BRIDGE_EXTRA_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr);
GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr);

#endif
