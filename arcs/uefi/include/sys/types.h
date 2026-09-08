#ifndef ARCS_UEFI_COMPAT_SYS_TYPES_H
#define ARCS_UEFI_COMPAT_SYS_TYPES_H

#include <Uefi.h>
#include <stddef.h>
#include <sgidefs.h>

/*
 * The original standalone headers expect sys/types.h to make the SGI ABI
 * scalar namespace visible.  Keep ownership of those types in sgidefs.h;
 * this wrapper only assembles the expected header environment on x86-64.
 */

#endif