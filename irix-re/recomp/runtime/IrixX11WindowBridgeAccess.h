#ifndef IRIX_X11_WINDOW_BRIDGE_ACCESS_H
#define IRIX_X11_WINDOW_BRIDGE_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

XFontSet irix_x11_window_fontset_from_guest(IrixGuestAddr addr);
Display *irix_x11_window_font_display_from_guest(IrixGuestRuntime *rt,
                                                  IrixGuestAddr guest_display);
int irix_x11_window_flush_guest_display(IrixGuestRuntime *rt,
                                         IrixGuestAddr guest_display);
int irix_x11_window_sync_guest_display(IrixGuestRuntime *rt,
                                        IrixGuestCpu *caller,
                                        IrixGuestAddr guest_display);

#endif
