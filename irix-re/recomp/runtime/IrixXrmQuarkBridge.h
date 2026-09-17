#ifndef IID_IRIX_XRM_QUARK_BRIDGE_H
#define IID_IRIX_XRM_QUARK_BRIDGE_H

#include "IrixGuest.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void irix_xrm_quark_bridge_init(IrixGuestRuntime *rt);
void irix_xrm_quark_bridge_set_fast_table(const char *const *names, size_t count);
IrixGuestFunction irix_xrm_quark_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
