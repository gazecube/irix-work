#ifndef IRIX_RUNTIME_LIBC_BRIDGE_H
#define IRIX_RUNTIME_LIBC_BRIDGE_H

#include "IrixGuest.h"

void irix_runtime_libc_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_runtime_libc_bridge_lookup(const char *name);

#endif
