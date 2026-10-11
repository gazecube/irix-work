#ifndef IRIX_X11_WINDOW_BRIDGE_H
#define IRIX_X11_WINDOW_BRIDGE_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void irix_x11_window_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_window_bridge_lookup(const char *name);
Display *irix_x11_window_font_display_from_guest(IrixGuestRuntime *rt,
                                                  IrixGuestAddr guest_display);

#ifdef __cplusplus
}
#endif

#endif
