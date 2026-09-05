#ifndef ARCS_UEFI_COMPAT_SYS_TYPES_H
#define ARCS_UEFI_COMPAT_SYS_TYPES_H

#include <Uefi.h>
#include <stddef.h>

/* SGI standalone sources use this unsigned scalar for callback payloads. */
typedef UINTN __scunsigned_t;

#endif
