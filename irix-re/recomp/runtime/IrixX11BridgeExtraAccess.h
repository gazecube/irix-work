#ifndef IRIX_X11_BRIDGE_EXTRA_ACCESS_H
#define IRIX_X11_BRIDGE_EXTRA_ACCESS_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

Display *irix_x11_primary_display_from_guest(IrixGuestAddr addr);
GC irix_x11_primary_gc_from_guest(IrixGuestAddr addr);
XFontStruct *irix_x11_primary_font_from_guest(IrixGuestAddr addr);
IrixGuestAddr irix_x11_primary_font_to_guest(IrixGuestRuntime *rt, XFontStruct *font);
void irix_x11_primary_forget_font(IrixGuestAddr addr);
IrixGuestAddr irix_x11_primary_create_gc(IrixGuestRuntime *rt,
                                         IrixGuestAddr display_addr,
                                         uint32_t drawable,
                                         uint32_t mask,
                                         IrixGuestAddr values_addr);
int irix_x11_primary_free_gc(IrixGuestRuntime *rt,
                             IrixGuestAddr display_addr,
                             IrixGuestAddr guest_gc);

#endif
