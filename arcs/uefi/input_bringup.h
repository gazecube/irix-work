#ifndef ARCS_UEFI_INPUT_BRINGUP_H
#define ARCS_UEFI_INPUT_BRINGUP_H

#include <Uefi.h>

EFI_STATUS
ArcsRunInputBringup(
    EFI_SYSTEM_TABLE *SystemTable
    );

/* Discover pointers (idempotent) and service them once without blocking, so
 * the autoboot countdown can use the mouse before the maintenance loop runs. */
EFI_STATUS ArcsInputInit(EFI_SYSTEM_TABLE *SystemTable);
BOOLEAN ArcsInputPollPointers(VOID);

#endif
