#ifndef IID_IRIX_COMPAT_H
#define IID_IRIX_COMPAT_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * User-space implementation of the historical Linux/MIPS IRIX compatibility
 * boundary.  Keep SGI-specific syscall semantics here instead of growing the
 * CPU/runtime core in IrixGuest.c.
 */
void irix_compat_syssgi(IrixGuestRuntime *rt, IrixGuestCpu *cpu);
void irix_compat_sysmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu);

#ifdef __cplusplus
}
#endif

#endif
