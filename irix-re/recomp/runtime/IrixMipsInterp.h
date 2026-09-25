#ifndef IID_IRIX_MIPS_INTERP_H
#define IID_IRIX_MIPS_INTERP_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

int irix_mips_interp_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address);
int irix_mips_interp_should_force(IrixGuestAddr address);

#ifdef __cplusplus
}
#endif

#endif
