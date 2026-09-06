#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>

#include <gfxgui.h>

#include "input_bringup.h"
#include "input_diag.h"
#include "maintenance_menu_bringup.h"

#define MAX_POINTER_PROTOCOLS 16

static INT32
ClampCoord(INT32 Value, INT32 Limit)
{
    if (Value < 0)
        return 0;
    if (Value >= Limit)
        return Limit - 1;
    return Value;
}

static UINTN
MapAbsolute(UINT64 Value, UINT64 Min, UINT64 Max, UINTN Pixels)
{
    UINT64 Span;

    if (Pixels <= 1 || Max <= Min)
        return 0;

    if (Value < Min)
        Value = Min;
    if (Value > Max)
        Value = Max;

    Span = Max - Min;
    return (UINTN)(((Value - Min) * (UINT64)(Pixels - 1)) / Span);
}

/*
 * UEFI drivers are not required to have already expanded every controller
 * tree before an application starts.  In particular, a USB HID device may
 * exist below a host-controller handle while EFI_SIMPLE_POINTER_PROTOCOL or
 * EFI_ABSOLUTE_POINTER_PROTOCOL has not yet been produced on its child
 * handle.  Follow the ConnectController() "connect all handles" pattern from
 * the UEFI specification before discovering pointer protocols.
 *
 * This is intentionally an adapter-side operation.  The original SGI GUI
 * still consumes only htp mouse deltas/buttons through guiCheckMouse().
 */
static VOID
ConnectAllControllers(VOID)
{
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;
    UINTN I;

    if (EFI_ERROR(gBS->LocateHandleBuffer(
            AllHandles,
            NULL,
            NULL,
            &HandleCount,
            &Handles)))
        return;

    for (I = 0; I < HandleCount; ++I)
        (VOID)gBS->ConnectController(Handles[I], NULL, NULL, TRUE);

    FreePool(Handles);
}

static UINT8
SimplePointerButtons(const EFI_SIMPLE_POINTER_STATE *State)
{
    UINT8 Buttons = 0x07;

    /* The original SGI GUI consumes three active-low button bits.  Preserve
     * that convention at the adapter boundary: left is bit 2, right bit 0. */
    if (State->LeftButton)
        Buttons &= (UINT8)~0x04;
    if (State->RightButton)
        Buttons &= (UINT8)~0x01;

    return Buttons;
}

static UINT8
AbsolutePointerButtons(const EFI_ABSOLUTE_POINTER_STATE *State)
{
    UINT8 Buttons = 0x07;

    if (State->ActiveButtons & EFI_ABSP_TouchActive)
        Buttons &= (UINT8)~0x04;
    if (State->ActiveButtons & EFI_ABS_AltActive)
        Buttons &= (UINT8)~0x01;

    return Buttons;
}

EFI_STATUS
ArcsRunInputBringup(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_SIMPLE_POINTER_PROTOCOL *Simple[MAX_POINTER_PROTOCOLS];
    EFI_ABSOLUTE_POINTER_PROTOCOL *Absolute[MAX_POINTER_PROTOCOLS];
    EFI_HANDLE *Handles;
    EFI_STATUS Status;
    EFI_INPUT_KEY Key;
    UINTN SimpleCount;
    UINTN AbsoluteCount;
    UINTN HandleCount;
    UINTN I;
    INT32 CurrentX;
    INT32 CurrentY;
    INT32 Width;
    INT32 Height;
    ARCS_INPUT_DIAG Diag;

    if (!SystemTable || !SystemTable->ConIn)
        return EFI_INVALID_PARAMETER;

    Width = gfxWidth();
    Height = gfxHeight();
    if (Width <= 0 || Height <= 0)
        return EFI_DEVICE_ERROR;

    CurrentX = Width / 2;
    CurrentY = Height / 2;
    _gfxsetms(CurrentX, CurrentY);

    if (SystemTable->ConIn->Reset)
        (VOID)SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);

    /* Force firmware drivers to bind recursively before asking which pointer
     * protocols exist.  OVMF can otherwise leave HID child handles latent. */
    ConnectAllControllers();

    SimpleCount = 0;
    AbsoluteCount = 0;

    Handles = NULL;
    HandleCount = 0;
    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiAbsolutePointerProtocolGuid,
        NULL,
        &HandleCount,
        &Handles
        );
    if (!EFI_ERROR(Status)) {
        for (I = 0; I < HandleCount && AbsoluteCount < MAX_POINTER_PROTOCOLS; ++I) {
            EFI_ABSOLUTE_POINTER_PROTOCOL *Protocol = NULL;

            if (!EFI_ERROR(gBS->HandleProtocol(
                    Handles[I],
                    &gEfiAbsolutePointerProtocolGuid,
                    (VOID **)&Protocol)) && Protocol) {
                Absolute[AbsoluteCount++] = Protocol;
            }
        }
        FreePool(Handles);
    }

    Handles = NULL;
    HandleCount = 0;
    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimplePointerProtocolGuid,
        NULL,
        &HandleCount,
        &Handles
        );
    if (!EFI_ERROR(Status)) {
        for (I = 0; I < HandleCount && SimpleCount < MAX_POINTER_PROTOCOLS; ++I) {
            EFI_SIMPLE_POINTER_PROTOCOL *Protocol = NULL;

            if (!EFI_ERROR(gBS->HandleProtocol(
                    Handles[I],
                    &gEfiSimplePointerProtocolGuid,
                    (VOID **)&Protocol)) && Protocol) {
                Simple[SimpleCount++] = Protocol;
            }
        }
        FreePool(Handles);
    }

    Diag.AbsoluteCount = AbsoluteCount;
    Diag.SimpleCount = SimpleCount;
    Diag.AbsoluteStatus = AbsoluteCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.SimpleStatus = SimpleCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.AbsoluteWaitStatus = AbsoluteCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.SimpleWaitStatus = SimpleCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.AbsoluteWaitSignals = 0;
    Diag.SimpleWaitSignals = 0;
    Diag.AbsoluteEvents = 0;
    Diag.SimpleEvents = 0;
    Diag.RawAbsoluteX = 0;
    Diag.RawAbsoluteY = 0;
    Diag.LastSimpleDx = 0;
    Diag.LastSimpleDy = 0;
    Diag.CurrentX = CurrentX;
    Diag.CurrentY = CurrentY;
    ArcsInputDiagDraw(SystemTable, &Diag);

    for (;;) {
        BOOLEAN DidWork = FALSE;
        BOOLEAN AbsoluteWorked = FALSE;
        BOOLEAN DiagDirty = FALSE;

        /* UEFI may publish several instances of a pointer protocol.  Do not
         * assume LocateProtocol's arbitrary first instance is the active
         * mouse; poll every discovered Absolute Pointer instance. */
        for (I = 0; I < AbsoluteCount; ++I) {
            EFI_ABSOLUTE_POINTER_PROTOCOL *Protocol = Absolute[I];
            EFI_ABSOLUTE_POINTER_STATE State;

            if (!Protocol || !Protocol->GetState || !Protocol->Mode)
                continue;

            if (Protocol->WaitForInput) {
                EFI_STATUS WaitStatus = gBS->CheckEvent(Protocol->WaitForInput);
                if (Diag.AbsoluteWaitStatus != WaitStatus) {
                    Diag.AbsoluteWaitStatus = WaitStatus;
                    DiagDirty = TRUE;
                }
                if (!EFI_ERROR(WaitStatus)) {
                    ++Diag.AbsoluteWaitSignals;
                    DiagDirty = TRUE;
                }
            }

            for (;;) {
                EFI_STATUS PointerStatus = Protocol->GetState(Protocol, &State);

                if (Diag.AbsoluteStatus != PointerStatus) {
                    Diag.AbsoluteStatus = PointerStatus;
                    DiagDirty = TRUE;
                }
                if (EFI_ERROR(PointerStatus))
                    break;

                {
                    INT32 NewX;
                    INT32 NewY;
                    INT32 Dx;
                    INT32 Dy;

                    NewX = (INT32)MapAbsolute(
                        State.CurrentX,
                        Protocol->Mode->AbsoluteMinX,
                        Protocol->Mode->AbsoluteMaxX,
                        (UINTN)Width
                        );
                    NewY = (INT32)MapAbsolute(
                        State.CurrentY,
                        Protocol->Mode->AbsoluteMinY,
                        Protocol->Mode->AbsoluteMaxY,
                        (UINTN)Height
                        );

                    Dx = NewX - CurrentX;
                    Dy = NewY - CurrentY;
                    CurrentX = NewX;
                    CurrentY = NewY;

                    ++Diag.AbsoluteEvents;
                    Diag.RawAbsoluteX = State.CurrentX;
                    Diag.RawAbsoluteY = State.CurrentY;
                    Diag.CurrentX = CurrentX;
                    Diag.CurrentY = CurrentY;
                    DiagDirty = TRUE;

                    guiCheckMouse(Dx, Dy, AbsolutePointerButtons(&State));
                    DidWork = TRUE;
                    AbsoluteWorked = TRUE;
                }
            }
        }

        /* If no absolute device produced an event, poll every Simple Pointer
         * instance too.  This covers firmware with multiple mouse/touchpad
         * handles or a dead first instance. */
        if (!AbsoluteWorked) {
            for (I = 0; I < SimpleCount; ++I) {
                EFI_SIMPLE_POINTER_PROTOCOL *Protocol = Simple[I];
                EFI_SIMPLE_POINTER_STATE State;

                if (!Protocol || !Protocol->GetState)
                    continue;

                if (Protocol->WaitForInput) {
                    EFI_STATUS WaitStatus = gBS->CheckEvent(Protocol->WaitForInput);
                    if (Diag.SimpleWaitStatus != WaitStatus) {
                        Diag.SimpleWaitStatus = WaitStatus;
                        DiagDirty = TRUE;
                    }
                    if (!EFI_ERROR(WaitStatus)) {
                        ++Diag.SimpleWaitSignals;
                        DiagDirty = TRUE;
                    }
                }

                for (;;) {
                    EFI_STATUS PointerStatus = Protocol->GetState(Protocol, &State);

                    if (Diag.SimpleStatus != PointerStatus) {
                        Diag.SimpleStatus = PointerStatus;
                        DiagDirty = TRUE;
                    }
                    if (EFI_ERROR(PointerStatus))
                        break;

                    {
                        INT32 Dx = State.RelativeMovementX;
                        INT32 Dy = State.RelativeMovementY;

                        guiCheckMouse(Dx, Dy, SimplePointerButtons(&State));
                        CurrentX = ClampCoord(CurrentX + Dx, Width);
                        CurrentY = ClampCoord(CurrentY + Dy, Height);

                        ++Diag.SimpleEvents;
                        Diag.LastSimpleDx = Dx;
                        Diag.LastSimpleDy = Dy;
                        Diag.CurrentX = CurrentX;
                        Diag.CurrentY = CurrentY;
                        DiagDirty = TRUE;
                        DidWork = TRUE;
                    }
                }
            }
        }

        while (!EFI_ERROR(SystemTable->ConIn->ReadKeyStroke(
                    SystemTable->ConIn,
                    &Key))) {
            DidWork = TRUE;

            if (Key.ScanCode == SCAN_ESC)
                return EFI_SUCCESS;

            if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= L'5')
                ArcsMaintenanceMenuSelect((UINTN)(Key.UnicodeChar - L'0'));
        }

        if (DiagDirty)
            ArcsInputDiagDraw(SystemTable, &Diag);

        if (!DidWork) {
            Status = gBS->Stall(1000);
            if (EFI_ERROR(Status))
                return Status;
        }
    }
}
