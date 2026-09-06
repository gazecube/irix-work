#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>

#include <gfxgui.h>

#include "input_bringup.h"
#include "input_diag.h"
#include "maintenance_menu_bringup.h"
#include "usb_hid_pointer.h"

#define MAX_POINTER_PROTOCOLS 16
#define MAX_INPUT_EVENTS (2 + MAX_POINTER_PROTOCOLS * 2)

#define WAIT_KIND_KEY     0
#define WAIT_KIND_ABS     1
#define WAIT_KIND_SIMPLE  2
#define WAIT_KIND_USB     3

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

static VOID
CaptureDevicePathSignature(
    EFI_HANDLE Handle,
    UINT8 *Types,
    UINT8 *SubTypes,
    UINTN *Count
    )
{
    EFI_DEVICE_PATH_PROTOCOL *Node = NULL;
    UINTN N = 0;

    if (!Handle || !Types || !SubTypes || !Count)
        return;

    *Count = 0;
    if (EFI_ERROR(gBS->HandleProtocol(
            Handle,
            &gEfiDevicePathProtocolGuid,
            (VOID **)&Node)) || !Node)
        return;

    while (N < ARCS_DIAG_PATH_NODES) {
        UINTN Length;

        Types[N] = Node->Type;
        SubTypes[N] = Node->SubType;
        ++N;

        if (Node->Type == 0x7f)
            break;

        Length = (UINTN)Node->Length[0] | ((UINTN)Node->Length[1] << 8);
        if (Length < sizeof(EFI_DEVICE_PATH_PROTOCOL))
            break;

        Node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Node + Length);
    }

    *Count = N;
}

/* Expand controller trees before protocol discovery. This uses only standard
 * UEFI Boot Services and is not tied to any particular firmware. */
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

static BOOLEAN
DrainAbsolute(
    EFI_ABSOLUTE_POINTER_PROTOCOL *Protocol,
    INT32 Width,
    INT32 Height,
    INT32 *CurrentX,
    INT32 *CurrentY,
    ARCS_INPUT_DIAG *Diag
    )
{
    EFI_ABSOLUTE_POINTER_STATE State;
    BOOLEAN DidWork = FALSE;

    if (!Protocol || !Protocol->GetState || !Protocol->Mode)
        return FALSE;

    for (;;) {
        EFI_STATUS PointerStatus = Protocol->GetState(Protocol, &State);
        Diag->AbsoluteStatus = PointerStatus;
        if (EFI_ERROR(PointerStatus))
            break;

        {
            INT32 NewX = (INT32)MapAbsolute(
                State.CurrentX,
                Protocol->Mode->AbsoluteMinX,
                Protocol->Mode->AbsoluteMaxX,
                (UINTN)Width);
            INT32 NewY = (INT32)MapAbsolute(
                State.CurrentY,
                Protocol->Mode->AbsoluteMinY,
                Protocol->Mode->AbsoluteMaxY,
                (UINTN)Height);
            INT32 Dx = NewX - *CurrentX;
            INT32 Dy = NewY - *CurrentY;

            *CurrentX = NewX;
            *CurrentY = NewY;
            ++Diag->AbsoluteEvents;
            Diag->RawAbsoluteX = State.CurrentX;
            Diag->RawAbsoluteY = State.CurrentY;
            Diag->CurrentX = *CurrentX;
            Diag->CurrentY = *CurrentY;

            guiCheckMouse(Dx, Dy, AbsolutePointerButtons(&State));
            DidWork = TRUE;
        }
    }

    return DidWork;
}

static BOOLEAN
DrainSimple(
    EFI_SIMPLE_POINTER_PROTOCOL *Protocol,
    INT32 Width,
    INT32 Height,
    INT32 *CurrentX,
    INT32 *CurrentY,
    ARCS_INPUT_DIAG *Diag
    )
{
    EFI_SIMPLE_POINTER_STATE State;
    BOOLEAN DidWork = FALSE;

    if (!Protocol || !Protocol->GetState)
        return FALSE;

    for (;;) {
        EFI_STATUS PointerStatus = Protocol->GetState(Protocol, &State);
        Diag->SimpleStatus = PointerStatus;
        if (EFI_ERROR(PointerStatus))
            break;

        {
            INT32 Dx = State.RelativeMovementX;
            INT32 Dy = State.RelativeMovementY;

            guiCheckMouse(Dx, Dy, SimplePointerButtons(&State));
            *CurrentX = ClampCoord(*CurrentX + Dx, Width);
            *CurrentY = ClampCoord(*CurrentY + Dy, Height);

            ++Diag->SimpleEvents;
            Diag->LastSimpleDx = Dx;
            Diag->LastSimpleDy = Dy;
            Diag->CurrentX = *CurrentX;
            Diag->CurrentY = *CurrentY;
            DidWork = TRUE;
        }
    }

    return DidWork;
}

static BOOLEAN
DrainUsbHid(
    INT32 Width,
    INT32 Height,
    INT32 *CurrentX,
    INT32 *CurrentY,
    ARCS_INPUT_DIAG *Diag
    )
{
    ARCS_USB_POINTER_STATE State;
    EFI_STATUS Status;

    Status = ArcsUsbHidPointerGetState(&State);
    Diag->UsbStatus = Status;
    Diag->UsbPackets = ArcsUsbHidPointerPacketCount();
    if (EFI_ERROR(Status))
        return FALSE;

    if (State.Kind == ArcsUsbPointerRelative) {
        guiCheckMouse(State.Dx, State.Dy, State.Buttons);
        *CurrentX = ClampCoord(*CurrentX + State.Dx, Width);
        *CurrentY = ClampCoord(*CurrentY + State.Dy, Height);
        Diag->LastSimpleDx = State.Dx;
        Diag->LastSimpleDy = State.Dy;
    } else if (State.Kind == ArcsUsbPointerAbsolute) {
        INT32 NewX = (INT32)MapAbsolute(
            State.X, State.MinX, State.MaxX, (UINTN)Width);
        INT32 NewY = (INT32)MapAbsolute(
            State.Y, State.MinY, State.MaxY, (UINTN)Height);
        INT32 Dx = NewX - *CurrentX;
        INT32 Dy = NewY - *CurrentY;

        guiCheckMouse(Dx, Dy, State.Buttons);
        *CurrentX = NewX;
        *CurrentY = NewY;
        Diag->RawAbsoluteX = State.X;
        Diag->RawAbsoluteY = State.Y;
    } else {
        return FALSE;
    }

    Diag->CurrentX = *CurrentX;
    Diag->CurrentY = *CurrentY;
    return TRUE;
}

EFI_STATUS
ArcsRunInputBringup(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_SIMPLE_POINTER_PROTOCOL *Simple[MAX_POINTER_PROTOCOLS];
    EFI_ABSOLUTE_POINTER_PROTOCOL *Absolute[MAX_POINTER_PROTOCOLS];
    EFI_EVENT WaitEvents[MAX_INPUT_EVENTS];
    UINT8 WaitKinds[MAX_INPUT_EVENTS];
    UINTN WaitIndices[MAX_INPUT_EVENTS];
    EFI_HANDLE *Handles;
    EFI_STATUS Status;
    EFI_STATUS ReturnStatus = EFI_SUCCESS;
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
    EFI_ABSOLUTE_POINTER_PROTOCOL *ConsoleAbsolute = NULL;
    EFI_SIMPLE_POINTER_PROTOCOL *ConsoleSimple = NULL;
    BOOLEAN HaveDirectPointer;

    if (!SystemTable || !SystemTable->ConIn)
        return EFI_INVALID_PARAMETER;

    ZeroMem(&Diag, sizeof(Diag));

    Width = gfxWidth();
    Height = gfxHeight();
    if (Width <= 0 || Height <= 0)
        return EFI_DEVICE_ERROR;

    CurrentX = Width / 2;
    CurrentY = Height / 2;
    _gfxsetms(CurrentX, CurrentY);

    if (SystemTable->ConIn->Reset)
        (VOID)SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);

    ConnectAllControllers();

    SimpleCount = 0;
    AbsoluteCount = 0;

    if (SystemTable->ConsoleInHandle) {
        if (!EFI_ERROR(gBS->HandleProtocol(
                SystemTable->ConsoleInHandle,
                &gEfiAbsolutePointerProtocolGuid,
                (VOID **)&ConsoleAbsolute)) && ConsoleAbsolute)
            Diag.ConsoleAbsolute = TRUE;
        if (!EFI_ERROR(gBS->HandleProtocol(
                SystemTable->ConsoleInHandle,
                &gEfiSimplePointerProtocolGuid,
                (VOID **)&ConsoleSimple)) && ConsoleSimple)
            Diag.ConsoleSimple = TRUE;
    }

    /* Prefer real device handles over the console splitter aggregate. OVMF's
     * ConSplitter may publish pathless pointer protocols even when no mouse
     * DXE is feeding them. */
    Handles = NULL;
    HandleCount = 0;
    Status = gBS->LocateHandleBuffer(
        ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL,
        &HandleCount, &Handles);
    if (!EFI_ERROR(Status)) {
        Diag.EnumeratedAbsoluteCount = HandleCount;
        for (I = 0; I < HandleCount && AbsoluteCount < MAX_POINTER_PROTOCOLS; ++I) {
            EFI_ABSOLUTE_POINTER_PROTOCOL *Protocol = NULL;

            if (Handles[I] == SystemTable->ConsoleInHandle)
                continue;
            if (!EFI_ERROR(gBS->HandleProtocol(
                    Handles[I], &gEfiAbsolutePointerProtocolGuid,
                    (VOID **)&Protocol)) && Protocol) {
                if (AbsoluteCount == 0)
                    CaptureDevicePathSignature(
                        Handles[I],
                        Diag.AbsolutePathType,
                        Diag.AbsolutePathSubType,
                        &Diag.AbsolutePathCount);
                Absolute[AbsoluteCount++] = Protocol;
            }
        }
        FreePool(Handles);
    }

    Handles = NULL;
    HandleCount = 0;
    Status = gBS->LocateHandleBuffer(
        ByProtocol, &gEfiSimplePointerProtocolGuid, NULL,
        &HandleCount, &Handles);
    if (!EFI_ERROR(Status)) {
        Diag.EnumeratedSimpleCount = HandleCount;
        for (I = 0; I < HandleCount && SimpleCount < MAX_POINTER_PROTOCOLS; ++I) {
            EFI_SIMPLE_POINTER_PROTOCOL *Protocol = NULL;

            if (Handles[I] == SystemTable->ConsoleInHandle)
                continue;
            if (!EFI_ERROR(gBS->HandleProtocol(
                    Handles[I], &gEfiSimplePointerProtocolGuid,
                    (VOID **)&Protocol)) && Protocol) {
                if (SimpleCount == 0)
                    CaptureDevicePathSignature(
                        Handles[I],
                        Diag.SimplePathType,
                        Diag.SimplePathSubType,
                        &Diag.SimplePathCount);
                Simple[SimpleCount++] = Protocol;
            }
        }
        FreePool(Handles);
    }

    HaveDirectPointer = (AbsoluteCount != 0 || SimpleCount != 0);

    /* If the firmware did not produce a real pointer protocol, consume the
     * standard EFI_USB_IO_PROTOCOL directly. This is still a UEFI service,
     * but it avoids depending on the firmware vendor shipping a mouse DXE. */
    if (!HaveDirectPointer) {
        Diag.UsbStatus = ArcsUsbHidPointerInit();
        Diag.UsbFallbackActive = ArcsUsbHidPointerPresent();
        Diag.UsbFallbackAbsolute =
            ArcsUsbHidPointerKind() == ArcsUsbPointerAbsolute;
        Diag.UsbIoCount = ArcsUsbHidPointerUsbIoCount();
        Diag.UsbHidCount = ArcsUsbHidPointerHidCount();
        Diag.UsbPackets = ArcsUsbHidPointerPacketCount();
    }

    /* Last resort: retain a console aggregate even if it is pathless. This
     * keeps compatibility with firmware that genuinely exposes its only PS/2
     * or legacy-USB pointer through ConsoleInHandle. */
    if (!HaveDirectPointer && !Diag.UsbFallbackActive) {
        if (ConsoleAbsolute && AbsoluteCount < MAX_POINTER_PROTOCOLS)
            Absolute[AbsoluteCount++] = ConsoleAbsolute;
        if (ConsoleSimple && SimpleCount < MAX_POINTER_PROTOCOLS)
            Simple[SimpleCount++] = ConsoleSimple;
    }

    Diag.AbsoluteCount = AbsoluteCount;
    Diag.SimpleCount = SimpleCount;
    Diag.AbsoluteStatus = AbsoluteCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.SimpleStatus = SimpleCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.AbsoluteWaitStatus = AbsoluteCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.SimpleWaitStatus = SimpleCount ? EFI_NOT_READY : EFI_NOT_FOUND;
    Diag.CurrentX = CurrentX;
    Diag.CurrentY = CurrentY;
    ArcsInputDiagDraw(SystemTable, &Diag);

    for (;;) {
        UINTN EventCount = 0;
        UINTN Which = 0;
        BOOLEAN DiagDirty = FALSE;

        if (SystemTable->ConIn->WaitForKey) {
            WaitEvents[EventCount] = SystemTable->ConIn->WaitForKey;
            WaitKinds[EventCount] = WAIT_KIND_KEY;
            WaitIndices[EventCount] = 0;
            ++EventCount;
        }

        for (I = 0; I < AbsoluteCount && EventCount < MAX_INPUT_EVENTS; ++I) {
            if (Absolute[I] && Absolute[I]->WaitForInput) {
                WaitEvents[EventCount] = Absolute[I]->WaitForInput;
                WaitKinds[EventCount] = WAIT_KIND_ABS;
                WaitIndices[EventCount] = I;
                ++EventCount;
            }
        }

        for (I = 0; I < SimpleCount && EventCount < MAX_INPUT_EVENTS; ++I) {
            if (Simple[I] && Simple[I]->WaitForInput) {
                WaitEvents[EventCount] = Simple[I]->WaitForInput;
                WaitKinds[EventCount] = WAIT_KIND_SIMPLE;
                WaitIndices[EventCount] = I;
                ++EventCount;
            }
        }

        if (Diag.UsbFallbackActive && ArcsUsbHidPointerEvent() &&
            EventCount < MAX_INPUT_EVENTS) {
            WaitEvents[EventCount] = ArcsUsbHidPointerEvent();
            WaitKinds[EventCount] = WAIT_KIND_USB;
            WaitIndices[EventCount] = 0;
            ++EventCount;
        }

        if (EventCount != 0) {
            Status = gBS->WaitForEvent(EventCount, WaitEvents, &Which);
            if (EFI_ERROR(Status)) {
                ReturnStatus = Status;
                break;
            }

            if (WaitKinds[Which] == WAIT_KIND_KEY) {
                while (!EFI_ERROR(SystemTable->ConIn->ReadKeyStroke(
                            SystemTable->ConIn, &Key))) {
                    if (Key.ScanCode == SCAN_ESC)
                        goto Exit;
                    if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= L'5')
                        ArcsMaintenanceMenuSelect((UINTN)(Key.UnicodeChar - L'0'));
                }
            } else if (WaitKinds[Which] == WAIT_KIND_ABS) {
                I = WaitIndices[Which];
                Diag.AbsoluteWaitStatus = EFI_SUCCESS;
                ++Diag.AbsoluteWaitSignals;
                (VOID)DrainAbsolute(
                    Absolute[I], Width, Height,
                    &CurrentX, &CurrentY, &Diag);
                DiagDirty = TRUE;
            } else if (WaitKinds[Which] == WAIT_KIND_SIMPLE) {
                I = WaitIndices[Which];
                Diag.SimpleWaitStatus = EFI_SUCCESS;
                ++Diag.SimpleWaitSignals;
                (VOID)DrainSimple(
                    Simple[I], Width, Height,
                    &CurrentX, &CurrentY, &Diag);
                DiagDirty = TRUE;
            } else if (WaitKinds[Which] == WAIT_KIND_USB) {
                (VOID)DrainUsbHid(
                    Width, Height, &CurrentX, &CurrentY, &Diag);
                DiagDirty = TRUE;
            }
        } else {
            for (I = 0; I < AbsoluteCount; ++I)
                if (DrainAbsolute(Absolute[I], Width, Height,
                                  &CurrentX, &CurrentY, &Diag))
                    DiagDirty = TRUE;
            for (I = 0; I < SimpleCount; ++I)
                if (DrainSimple(Simple[I], Width, Height,
                                &CurrentX, &CurrentY, &Diag))
                    DiagDirty = TRUE;
            if (Diag.UsbFallbackActive &&
                DrainUsbHid(Width, Height, &CurrentX, &CurrentY, &Diag))
                DiagDirty = TRUE;

            while (!EFI_ERROR(SystemTable->ConIn->ReadKeyStroke(
                        SystemTable->ConIn, &Key))) {
                if (Key.ScanCode == SCAN_ESC)
                    goto Exit;
                if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= L'5')
                    ArcsMaintenanceMenuSelect((UINTN)(Key.UnicodeChar - L'0'));
            }

            Status = gBS->Stall(1000);
            if (EFI_ERROR(Status)) {
                ReturnStatus = Status;
                break;
            }
        }

        if (Diag.UsbFallbackActive) {
            Diag.UsbPackets = ArcsUsbHidPointerPacketCount();
            Diag.UsbStatus = ArcsUsbHidPointerLastStatus();
        }
        if (DiagDirty)
            ArcsInputDiagDraw(SystemTable, &Diag);
    }

Exit:
    ArcsUsbHidPointerShutdown();
    return ReturnStatus;
}