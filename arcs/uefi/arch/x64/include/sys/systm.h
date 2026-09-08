/*
 * x86-64 standalone compatibility subset of IRIX <sys/systm.h>.
 *
 * Original libsc sources include <sys/systm.h> for the traditional kernel/
 * standalone utility declarations.  Pulling the complete IRIX kernel header
 * into the UEFI build would also pull in MIPS-only register and kernel state,
 * so keep the original source-level interfaces required by standalone libsc
 * at the architecture boundary instead.
 */
#ifndef ARCS_UEFI_X64_SYS_SYSTM_H
#define ARCS_UEFI_X64_SYS_SYSTM_H

#include <sys/types.h>

extern int splhi(void);
extern void splx(int);

extern int bcmp(const void *, const void *, size_t);
extern void bcopy(const void *, void *, size_t);
extern void bzero(void *, size_t);

#endif /* ARCS_UEFI_X64_SYS_SYSTM_H */
