#ifndef IRIX_X11_CONTEXT_BRIDGE_H
#define IRIX_X11_CONTEXT_BRIDGE_H

#include "IrixGuest.h"

void irix_x11_context_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_context_bridge_lookup(const char *name);

#endif
