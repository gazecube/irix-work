#ifndef ARCS_UEFI_BOOT_H
#define ARCS_UEFI_BOOT_H

#include <Uefi.h>

void ArcsUefiBootInit(EFI_HANDLE ImageHandle);
EFI_STATUS ArcsUefiBootDefault(void);

#endif
