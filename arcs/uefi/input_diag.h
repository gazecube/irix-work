#ifndef ARCS_UEFI_INPUT_DIAG_H
#define ARCS_UEFI_INPUT_DIAG_H

#include <Uefi.h>

typedef struct {
    UINTN AbsoluteCount;
    UINTN SimpleCount;
    EFI_STATUS AbsoluteStatus;
    EFI_STATUS SimpleStatus;
    UINT64 AbsoluteEvents;
    UINT64 SimpleEvents;
    UINT64 RawAbsoluteX;
    UINT64 RawAbsoluteY;
    INT32 LastSimpleDx;
    INT32 LastSimpleDy;
    INT32 CurrentX;
    INT32 CurrentY;
} ARCS_INPUT_DIAG;

VOID
ArcsInputDiagDraw(
    EFI_SYSTEM_TABLE *SystemTable,
    const ARCS_INPUT_DIAG *Diag
    );

#endif
