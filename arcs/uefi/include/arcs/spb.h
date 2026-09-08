#ifndef ARCS_UEFI_COMPAT_SPB_H
#define ARCS_UEFI_COMPAT_SPB_H

#include <arcs/types.h>

/*
 * Minimal X64/UEFI System Parameter Block surface required by the original
 * standalone libsc editor.  The real SGI header hard-codes MIPS K0/K1
 * addresses; preserve the source-level SPB contract while moving storage to
 * the platform boundary.
 */
typedef struct {
    LONG *DebugBlock;
} SystemParameterBlock;
typedef SystemParameterBlock spb_t;

extern spb_t *get_spb(void);
#define SPB get_spb()

#endif /* ARCS_UEFI_COMPAT_SPB_H */
