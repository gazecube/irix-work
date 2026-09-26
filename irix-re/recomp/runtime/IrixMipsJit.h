#ifndef IID_IRIX_MIPS_JIT_H
#define IID_IRIX_MIPS_JIT_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

int irix_mips_jit_available(void);
int irix_mips_jit_requested(void);
int irix_mips_jit_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                       IrixGuestAddr address);
void irix_mips_jit_shutdown(IrixGuestRuntime *rt);
void irix_mips_jit_guest_stub(IrixGuestRuntime *rt, IrixGuestCpu *cpu);

#ifdef __cplusplus
}
#endif

#endif
