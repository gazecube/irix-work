#ifndef IID_IRIX_X11_BRIDGE_H
#define IID_IRIX_X11_BRIDGE_H

#include "IrixGuest.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void irix_x11_bridge_init(IrixGuestRuntime *rt,
                          const char *const *fast_quark_names,
                          size_t fast_quark_count);
IrixGuestFunction irix_x11_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
