#ifndef IID_IRIX_XRM_COMPAT_H
#define IID_IRIX_XRM_COMPAT_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

void irix_xrm_compat_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_xrm_compat_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
