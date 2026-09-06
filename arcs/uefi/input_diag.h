#ifndef ARCS_UEFI_INPUT_DIAG_H
#define ARCS_UEFI_INPUT_DIAG_H

#include <Uefi.h>

#define ARCS_DIAG_PATH_NODES 8

typedef struct {
    UINTN AbsoluteCount;
    UINTN SimpleCount;
    EFI_STATUS AbsoluteStatus;
    EFI_STATUS SimpleStatus;
    EFI_STATUS AbsoluteWaitStatus;
    EFI_STATUS SimpleWaitStatus;
    UINT64 AbsoluteWaitSignals;
    UINT64 SimpleWaitSignals;
    UINT64 AbsoluteEvents;
    UINT64 SimpleEvents;
    UINT64 RawAbsoluteX;
    UINT64 RawAbsoluteY;
    INT32 LastSimpleDx;
    INT32 LastSimpleDy;
    INT32 CurrentX;
    INT32 CurrentY;
    UINTN AbsolutePathCount;
    UINTN SimplePathCount;
    UINT8 AbsolutePathType[ARCS_DIAG_PATH_NODES];
    UINT8 AbsolutePathSubType[ARCS_DIAG_PATH_NODES];
    UINT8 SimplePathType[ARCS_DIAG_PATH_NODES];
    UINT8 SimplePathSubType[ARCS_DIAG_PATH_NODES];
} ARCS_INPUT_DIAG;

VOID
ArcsInputDiagDraw(
    EFI_SYSTEM_TABLE *SystemTable,
    const ARCS_INPUT_DIAG *Diag
    );

#endif
