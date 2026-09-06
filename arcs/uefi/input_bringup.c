#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>

#include <gfxgui.h>

#include "input_bringup.h"
#include "maintenance_menu_bringup.h"

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
    EFI_SIMPLE_POINTER_PROTOCOL *Simple;
    EFI_ABSOLUTE_POINTER_PROTOCOL *Absolute;
    EFI_STATUS Status;
    EFI_INPUT_KEY Key;
    INT32 CurrentX;
    INT32 CurrentY;
    INT32 Width;
    INT32 Height;

    if (!SystemTable || !SystemTable->ConIn)
        return EFI_INVALID_PARAMETER;

    Width = gfxWidth();
    Height = gfxHeight();
    if (Width <= 0 || Height <= 0)
        return EFI_DEVICE_ERROR;

    CurrentX = Width / 2;
    CurrentY = Height / 2;
    _gfxsetms(CurrentX, CurrentY);

    /* Firmware console/pointer drivers can retain state from the boot
     * manager.  Reset them once before entering the PROM-style polling loop
     * so the first key/button transition is delivered from a known state. */
    if (SystemTable->ConIn->Reset)
        (VOID)SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);

    Simple = NULL;
    Absolute = NULL;
    (VOID)gBS->LocateProtocol(
        &gEfiAbsolutePointerProtocolGuid,
        NULL,
        (VOID **)&Absolute
        );
    (VOID)gBS->LocateProtocol(
        &gEfiSimplePointerProtocolGuid,
        NULL,
        (VOID **)&Simple
        );

    if (Absolute && Absolute->Reset)
        (VOID)Absolute->Reset(Absolute, FALSE);
    if (Simple && Simple->Reset)
        (VOID)Simple->Reset(Simple, FALSE);

    for (;;) {
        BOOLEAN DidWork = FALSE;
        BOOLEAN AbsoluteWorked = FALSE;

        /* Some firmware exposes both Absolute Pointer and Simple Pointer,
         * but only one of them may actually be wired to the active device.
         * Prefer absolute events when they are arriving, and otherwise poll
         * Simple Pointer in the same iteration instead of suppressing it just
         * because an idle Absolute Pointer protocol happens to exist. */
        if (Absolute && Absolute->GetState && Absolute->Mode) {
            EFI_ABSOLUTE_POINTER_STATE State;

            while (!EFI_ERROR(Absolute->GetState(Absolute, &State))) {
                INT32 NewX;
                INT32 NewY;
                INT32 Dx;
                INT32 Dy;

                NewX = (INT32)MapAbsolute(
                    State.CurrentX,
                    Absolute->Mode->AbsoluteMinX,
                    Absolute->Mode->AbsoluteMaxX,
                    (UINTN)Width
                    );
                NewY = (INT32)MapAbsolute(
                    State.CurrentY,
                    Absolute->Mode->AbsoluteMinY,
                    Absolute->Mode->AbsoluteMaxY,
                    (UINTN)Height
                    );

                Dx = NewX - CurrentX;
                Dy = NewY - CurrentY;
                CurrentX = NewX;
                CurrentY = NewY;

                guiCheckMouse(Dx, Dy, AbsolutePointerButtons(&State));
                DidWork = TRUE;
                AbsoluteWorked = TRUE;
            }
        }

        if (!AbsoluteWorked && Simple && Simple->GetState) {
            EFI_SIMPLE_POINTER_STATE State;

            while (!EFI_ERROR(Simple->GetState(Simple, &State))) {
                INT32 Dx = State.RelativeMovementX;
                INT32 Dy = State.RelativeMovementY;

                guiCheckMouse(Dx, Dy, SimplePointerButtons(&State));
                CurrentX = ClampCoord(CurrentX + Dx, Width);
                CurrentY = ClampCoord(CurrentY + Dy, Height);
                DidWork = TRUE;
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

        if (!DidWork) {
            Status = gBS->Stall(1000);
            if (EFI_ERROR(Status))
                return Status;
        }
    }
}
