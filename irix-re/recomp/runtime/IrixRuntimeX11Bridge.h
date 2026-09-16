#ifndef IRIX_RUNTIME_X11_BRIDGE_H
#define IRIX_RUNTIME_X11_BRIDGE_H

#include "IrixGuest.h"

void irix_runtime_x11_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_runtime_x11_bridge_lookup(const char *name);

#endif
