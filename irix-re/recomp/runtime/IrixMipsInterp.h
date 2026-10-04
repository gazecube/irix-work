#ifndef IID_IRIX_MIPS_INTERP_H
#define IID_IRIX_MIPS_INTERP_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IrixMipsTranslatedFrame {
    struct IrixMipsTranslatedFrame *previous;
    IrixGuestAddr address;
    uint32_t size;
    IrixGuestAddr return_address;
} IrixMipsTranslatedFrame;

void irix_mips_interp_push_translated(IrixMipsTranslatedFrame *frame,
                                      IrixGuestAddr address, uint32_t size,
                                      IrixGuestAddr return_address);
void irix_mips_interp_pop_translated(IrixMipsTranslatedFrame *frame);

int irix_mips_interp_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address);
int irix_mips_interp_step(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address);
int irix_mips_interp_should_force(IrixGuestAddr address);

#ifdef __cplusplus
}
#endif

#endif
