#ifndef IRIX_KERNEL_INFO_H
#define IRIX_KERNEL_INFO_H

#include <stddef.h>
#include <stdint.h>

/* IRIX kernel-information layer: answers the kernel queries IRIX user
 * programs make (hardware inventory, ...) from the Linux host. */

/* One inventory_t record, N32 layout without inv_next (always NULL here). */
typedef struct IrixKinfoInvent {
    int32_t inv_class;
    int32_t inv_type;
    int32_t inv_controller;
    int32_t inv_unit;
    int32_t inv_state;
} IrixKinfoInvent;

/* Fill up to max records; returns the number written. */
size_t irix_kinfo_inventory(IrixKinfoInvent *out, size_t max);

/* Number of online processors (sysmp MP_NPROCS and friends). */
int irix_kinfo_nprocs(void);

/* System-activity data (sysmp MP_SASZ / MP_SAGET / MP_SAGET1).  `sub` is a
 * MPSA_* code.  Sizes and word layouts follow IRIX 6.5 struct sysinfo,
 * minfo, dinfo, syserr, rminfo and sysinfo_cpu (N32: every member is a
 * 32-bit word except one 64-bit minfo member, which occupies two words). */
#define IRIX_KINFO_MPSA_SINFO      1
#define IRIX_KINFO_MPSA_MINFO      2
#define IRIX_KINFO_MPSA_DINFO      3
#define IRIX_KINFO_MPSA_SERR       4
#define IRIX_KINFO_MPSA_RMINFO     8
#define IRIX_KINFO_MPSA_SINFO_CPU 32

/* Size in bytes of the record for `sub`, or 0 when unsupported. */
size_t irix_kinfo_sa_size(int sub);

/* Fill `words` (size/4 entries, guest word order) for `sub`.  cpu < 0 sums
 * all processors.  Returns 0, or -1 if `sub` or `cpu` is invalid. */
int irix_kinfo_sa_get(int sub, int cpu, uint32_t *words, size_t nwords);

#endif
