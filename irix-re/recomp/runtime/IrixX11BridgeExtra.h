#ifndef IRIX_X11_BRIDGE_EXTRA_H
#define IRIX_X11_BRIDGE_EXTRA_H

#include "IrixGuest.h"
#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void irix_x11_extra_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_extra_lookup(const char *name);
Display *irix_x11_host_display(IrixGuestAddr guest_display);

#ifdef __cplusplus
}
#endif

#endif
