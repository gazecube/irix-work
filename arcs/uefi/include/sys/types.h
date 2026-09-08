#ifndef ARCS_UEFI_COMPAT_SYS_TYPES_H
#define ARCS_UEFI_COMPAT_SYS_TYPES_H

#include <Uefi.h>
#include <stddef.h>

/*
 * SGI ABI scalar names such as __scunsigned_t are supplied by sgidefs.h.
 * Keep this compatibility header limited to the host declarations needed
 * beneath the pristine standalone source instead of defining SGI ABI types
 * a second time.
 */

#endif