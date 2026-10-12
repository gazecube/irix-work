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

#endif
