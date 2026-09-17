#ifndef IRIX_CXX_BRIDGE_H
#define IRIX_CXX_BRIDGE_H

#include "IrixGuest.h"

void irix_cxx_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_cxx_bridge_lookup(const char *name);

#endif
