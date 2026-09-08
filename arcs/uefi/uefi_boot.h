#ifndef ARCS_UEFI_BOOT_H
#define ARCS_UEFI_BOOT_H

#include <Uefi.h>

void ArcsUefiBootInit(EFI_HANDLE ImageHandle);
EFI_STATUS ArcsUefiBootDefault(void);
EFI_STATUS ArcsUefiBootArcsPath(const char *Bootfile);

/* ARCS Execute() compatibility boundary used by the imported SGI boot policy.
 * The argument/environment vectors are retained in the signature even though
 * the UEFI image loader does not consume them yet.
 */
INT32 Execute(char *Path, INT32 Argc, char **Argv, char **Envp);

#endif
