#ifndef IRIX_X11_WM_BRIDGE_H
#define IRIX_X11_WM_BRIDGE_H

#include "IrixGuest.h"

void irix_x11_wm_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_wm_bridge_lookup(const char *name);

#endif
