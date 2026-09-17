#ifndef IRIX_X11_WINDOW_BRIDGE_ACCESS_H
#define IRIX_X11_WINDOW_BRIDGE_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

XFontSet irix_x11_window_fontset_from_guest(IrixGuestAddr addr);

#endif
