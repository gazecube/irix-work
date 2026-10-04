#ifndef IID_IRIX_LIBC_BRIDGE_H
#define IID_IRIX_LIBC_BRIDGE_H

#include "IrixGuest.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int irix_libc_bridge_init(IrixGuestRuntime *rt, IrixGuestAddr heap_base, uint32_t heap_size);
IrixGuestFunction irix_libc_bridge_lookup(const char *name);
IrixGuestAddr irix_libc_bridge_data_lookup(const char *name);
uint32_t irix_libc_guest_allocation_size(IrixGuestAddr addr);

#ifdef __cplusplus
}
#endif

#endif
