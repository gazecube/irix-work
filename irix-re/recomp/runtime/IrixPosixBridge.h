#ifndef IID_IRIX_POSIX_BRIDGE_H
#define IID_IRIX_POSIX_BRIDGE_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

void irix_posix_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_posix_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
