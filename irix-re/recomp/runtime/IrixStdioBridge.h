#ifndef IID_IRIX_STDIO_BRIDGE_H
#define IID_IRIX_STDIO_BRIDGE_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

int irix_stdio_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_stdio_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
