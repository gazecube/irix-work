#ifndef IID_IRIX_RUNTIME_XT_BRIDGE_H
#define IID_IRIX_RUNTIME_XT_BRIDGE_H

#include "IrixGuest.h"

void irix_runtime_xt_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_runtime_xt_bridge_lookup(const char *name);

#endif
