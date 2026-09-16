#ifndef IID_IRIX_FS_BRIDGE_H
#define IID_IRIX_FS_BRIDGE_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

void irix_fs_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_fs_bridge_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif
