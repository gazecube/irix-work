#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/UsbIo.h>

#include "usb_hid_pointer.h"

#define USB_HID_CLASS              0x03
#define USB_ENDPOINT_INTERRUPT     0x03
#define USB_ENDPOINT_DIR_IN        0x80
#define USB_REQ_GET_DESCRIPTOR     0x06
#define USB_DESC_TYPE_CONFIG       0x02
#define USB_DESC_TYPE_INTERFACE    0x04
#define USB_DESC_TYPE_ENDPOINT     0x05
#define USB_DESC_TYPE_HID          0x21
#define USB_DESC_TYPE_REPORT       0x22
#define USB_REQUEST_TYPE_STD_INTF  0x81

#define HID_ITEM_MAIN              0
#define HID_ITEM_GLOBAL            1
#define HID_ITEM_LOCAL             2

#define HID_MAIN_INPUT             8
#define HID_GLOBAL_USAGE_PAGE      0
#define HID_GLOBAL_LOGICAL_MIN     1
#define HID_GLOBAL_LOGICAL_MAX     2
#define HID_GLOBAL_REPORT_SIZE     7
#define HID_GLOBAL_REPORT_ID       8
#define HID_GLOBAL_REPORT_COUNT    9
#define HID_LOCAL_USAGE            0
#define HID_LOCAL_USAGE_MIN        1
#define HID_LOCAL_USAGE_MAX        2

#define HID_USAGE_PAGE_DESKTOP     0x01
#define HID_USAGE_PAGE_BUTTON      0x09
#define HID_USAGE_X                0x30
#define HID_USAGE_Y                0x31
#define HID_USAGE_WHEEL            0x38

#define MAX_LOCAL_USAGES           16
#define MAX_REPORT_IDS             256

typedef struct {
    BOOLEAN Valid;
    UINT16 BitOffset;
    UINT8 BitSize;
    UINT8 ReportId;
    BOOLEAN Relative;
    INT32 LogicalMin;
    INT32 LogicalMax;
} HID_FIELD;

typedef struct {
    EFI_USB_IO_PROTOCOL *UsbIo;
    EFI_EVENT Event;
    EFI_USB_INTERFACE_DESCRIPTOR Interface;
    EFI_USB_ENDPOINT_DESCRIPTOR Endpoint;
    HID_FIELD X;
    HID_FIELD Y;
    HID_FIELD Wheel;
    HID_FIELD Button1;
    HID_FIELD Button2;
    ARCS_USB_POINTER_KIND Kind;
    BOOLEAN Active;
    BOOLEAN Pending;
    UINT8 Buttons;
    INT32 Dx;
    INT32 Dy;
    UINT64 XValue;
    UINT64 YValue;
    UINT64 PacketCount;
    EFI_STATUS LastStatus;
    UINTN UsbIoCount;
    UINTN HidCount;
} ARCS_USB_HID_CONTEXT;

static ARCS_USB_HID_CONTEXT UsbPointer;

static UINT32
ReadUnsigned(const UINT8 *Data, UINTN Size)
{
    UINT32 Value = 0;
    UINTN I;

    for (I = 0; I < Size && I < 4; ++I)
        Value |= ((UINT32)Data[I]) << (I * 8);
    return Value;
}

static INT32
ReadSigned(const UINT8 *Data, UINTN Size)
{
    UINT32 Value = ReadUnsigned(Data, Size);
    UINTN Bits = Size * 8;

    if (Bits != 0 && Bits < 32 && (Value & (1U << (Bits - 1))))
        Value |= ~((1U << Bits) - 1U);
    return (INT32)Value;
}

static UINT32
ExtractBits(const UINT8 *Data, UINTN DataLength, UINT16 BitOffset, UINT8 BitSize)
{
    UINT32 Value = 0;
    UINTN Bit;

    if (!Data || BitSize == 0 || BitSize > 32)
        return 0;

    for (Bit = 0; Bit < BitSize; ++Bit) {
        UINTN SourceBit = (UINTN)BitOffset + Bit;
        UINTN ByteIndex = SourceBit >> 3;
        UINTN BitIndex = SourceBit & 7;

        if (ByteIndex >= DataLength)
            break;
        if (Data[ByteIndex] & (1U << BitIndex))
            Value |= 1U << Bit;
    }
    return Value;
}

static INT32
FieldValue(const HID_FIELD *Field, const UINT8 *Data, UINTN DataLength)
{
    UINT16 Offset;
    UINT32 Raw;

    if (!Field || !Field->Valid)
        return 0;

    if (Field->ReportId != 0) {
        if (DataLength == 0 || Data[0] != Field->ReportId)
            return 0;
    }

    Offset = Field->BitOffset;
    Raw = ExtractBits(Data, DataLength, Offset, Field->BitSize);

    if (Field->LogicalMin < 0 && Field->BitSize < 32 &&
        (Raw & (1U << (Field->BitSize - 1))))
        Raw |= ~((1U << Field->BitSize) - 1U);
    return (INT32)Raw;
}

static VOID
ResetLocalUsages(
    UINT32 *Usages,
    UINTN *UsageCount,
    BOOLEAN *HaveUsageMin,
    BOOLEAN *HaveUsageMax
    )
{
    ZeroMem(Usages, sizeof(UINT32) * MAX_LOCAL_USAGES);
    *UsageCount = 0;
    *HaveUsageMin = FALSE;
    *HaveUsageMax = FALSE;
}

static UINT32
UsageForIndex(
    const UINT32 *Usages,
    UINTN UsageCount,
    BOOLEAN HaveUsageMin,
    BOOLEAN HaveUsageMax,
    UINT32 UsageMin,
    UINT32 UsageMax,
    UINTN Index
    )
{
    if (Index < UsageCount)
        return Usages[Index];
    if (HaveUsageMin && HaveUsageMax && UsageMin + Index <= UsageMax)
        return UsageMin + (UINT32)Index;
    return 0;
}

static EFI_STATUS
ParseReportDescriptor(const UINT8 *Desc, UINTN Length)
{
    UINTN Offset = 0;
    UINT32 UsagePage = 0;
    INT32 LogicalMin = 0;
    INT32 LogicalMax = 0;
    UINT32 ReportSize = 0;
    UINT32 ReportCount = 0;
    UINT8 ReportId = 0;
    UINT16 ReportOffsets[MAX_REPORT_IDS];
    UINT32 Usages[MAX_LOCAL_USAGES];
    UINTN UsageCount = 0;
    BOOLEAN HaveUsageMin = FALSE;
    BOOLEAN HaveUsageMax = FALSE;
    UINT32 UsageMin = 0;
    UINT32 UsageMax = 0;

    ZeroMem(ReportOffsets, sizeof(ReportOffsets));
    ResetLocalUsages(Usages, &UsageCount, &HaveUsageMin, &HaveUsageMax);

    while (Offset < Length) {
        UINT8 Prefix = Desc[Offset++];
        UINTN Size;
        UINTN Type;
        UINTN Tag;
        UINT32 Value;
        INT32 SignedValue;

        if (Prefix == 0xfe) {
            if (Offset + 2 > Length)
                break;
            Size = Desc[Offset];
            Offset += 2;
            if (Offset + Size > Length)
                break;
            Offset += Size;
            continue;
        }

        Size = Prefix & 0x03;
        if (Size == 3)
            Size = 4;
        Type = (Prefix >> 2) & 0x03;
        Tag = (Prefix >> 4) & 0x0f;
        if (Offset + Size > Length)
            break;

        Value = ReadUnsigned(Desc + Offset, Size);
        SignedValue = ReadSigned(Desc + Offset, Size);
        Offset += Size;

        if (Type == HID_ITEM_GLOBAL) {
            switch (Tag) {
            case HID_GLOBAL_USAGE_PAGE:
                UsagePage = Value;
                break;
            case HID_GLOBAL_LOGICAL_MIN:
                LogicalMin = SignedValue;
                break;
            case HID_GLOBAL_LOGICAL_MAX:
                LogicalMax = (LogicalMin < 0) ? SignedValue : (INT32)Value;
                break;
            case HID_GLOBAL_REPORT_SIZE:
                ReportSize = Value;
                break;
            case HID_GLOBAL_REPORT_ID:
                ReportId = (UINT8)Value;
                if (ReportOffsets[ReportId] == 0)
                    ReportOffsets[ReportId] = 8;
                break;
            case HID_GLOBAL_REPORT_COUNT:
                ReportCount = Value;
                break;
            default:
                break;
            }
            continue;
        }

        if (Type == HID_ITEM_LOCAL) {
            switch (Tag) {
            case HID_LOCAL_USAGE:
                if (UsageCount < MAX_LOCAL_USAGES)
                    Usages[UsageCount++] = Value;
                break;
            case HID_LOCAL_USAGE_MIN:
                UsageMin = Value;
                HaveUsageMin = TRUE;
                break;
            case HID_LOCAL_USAGE_MAX:
                UsageMax = Value;
                HaveUsageMax = TRUE;
                break;
            default:
                break;
            }
            continue;
        }

        if (Type == HID_ITEM_MAIN) {
            if (Tag == HID_MAIN_INPUT && ReportSize != 0 && ReportCount != 0) {
                BOOLEAN Constant = (Value & 0x01) != 0;
                BOOLEAN Variable = (Value & 0x02) != 0;
                BOOLEAN Relative = (Value & 0x04) != 0;
                UINTN Index;

                for (Index = 0; Index < ReportCount; ++Index) {
                    UINT32 Usage = UsageForIndex(
                        Usages, UsageCount, HaveUsageMin, HaveUsageMax,
                        UsageMin, UsageMax, Index);
                    HID_FIELD *Field = NULL;

                    if (!Constant && Variable) {
                        if (UsagePage == HID_USAGE_PAGE_BUTTON) {
                            if (Usage == 1)
                                Field = &UsbPointer.Button1;
                            else if (Usage == 2)
                                Field = &UsbPointer.Button2;
                        } else if (UsagePage == HID_USAGE_PAGE_DESKTOP) {
                            if (Usage == HID_USAGE_X)
                                Field = &UsbPointer.X;
                            else if (Usage == HID_USAGE_Y)
                                Field = &UsbPointer.Y;
                            else if (Usage == HID_USAGE_WHEEL)
                                Field = &UsbPointer.Wheel;
                        }
                    }

                    if (Field && !Field->Valid && ReportSize <= 32) {
                        Field->Valid = TRUE;
                        Field->BitOffset = ReportOffsets[ReportId];
                        Field->BitSize = (UINT8)ReportSize;
                        Field->ReportId = ReportId;
                        Field->Relative = Relative;
                        Field->LogicalMin = LogicalMin;
                        Field->LogicalMax = LogicalMax;
                    }

                    ReportOffsets[ReportId] = (UINT16)(ReportOffsets[ReportId] + ReportSize);
                }
            }

            ResetLocalUsages(Usages, &UsageCount, &HaveUsageMin, &HaveUsageMax);
        }
    }

    if (!UsbPointer.X.Valid || !UsbPointer.Y.Valid)
        return EFI_UNSUPPORTED;
    if (UsbPointer.X.Relative != UsbPointer.Y.Relative)
        return EFI_UNSUPPORTED;

    UsbPointer.Kind = UsbPointer.X.Relative ?
        ArcsUsbPointerRelative : ArcsUsbPointerAbsolute;
    return EFI_SUCCESS;
}

static EFI_STATUS
FetchReportDescriptor(EFI_USB_IO_PROTOCOL *UsbIo, UINT8 InterfaceNumber)
{
    EFI_USB_CONFIG_DESCRIPTOR Config;
    EFI_USB_DEVICE_REQUEST Request;
    UINT32 Result;
    EFI_STATUS Status;
    UINT8 *ConfigBytes = NULL;
    UINT8 *Report = NULL;
    UINT16 ReportLength = 0;
    UINTN Pos;

    Status = UsbIo->UsbGetConfigDescriptor(UsbIo, &Config);
    if (EFI_ERROR(Status) || Config.TotalLength < sizeof(Config))
        return EFI_UNSUPPORTED;

    ConfigBytes = AllocateZeroPool(Config.TotalLength);
    if (!ConfigBytes)
        return EFI_OUT_OF_RESOURCES;

    ZeroMem(&Request, sizeof(Request));
    Request.RequestType = 0x80;
    Request.Request = USB_REQ_GET_DESCRIPTOR;
    Request.Value = (UINT16)(USB_DESC_TYPE_CONFIG << 8);
    Request.Index = 0;
    Request.Length = Config.TotalLength;

    Status = UsbIo->UsbControlTransfer(
        UsbIo, &Request, EfiUsbDataIn, 1000,
        ConfigBytes, Config.TotalLength, &Result);
    if (EFI_ERROR(Status))
        goto Exit;

    Pos = 0;
    while (Pos + 2 <= Config.TotalLength) {
        UINT8 DescLen = ConfigBytes[Pos];
        UINT8 DescType = ConfigBytes[Pos + 1];

        if (DescLen < 2 || Pos + DescLen > Config.TotalLength)
            break;

        if (DescType == USB_DESC_TYPE_INTERFACE && DescLen >= 9) {
            UINT8 ThisInterface = ConfigBytes[Pos + 2];
            UINT8 ThisAlt = ConfigBytes[Pos + 3];
            if (ThisInterface == InterfaceNumber && ThisAlt == 0) {
                UINTN Scan = Pos + DescLen;
                while (Scan + 2 <= Config.TotalLength) {
                    UINT8 L = ConfigBytes[Scan];
                    UINT8 T = ConfigBytes[Scan + 1];
                    if (L < 2 || Scan + L > Config.TotalLength)
                        break;
                    if (T == USB_DESC_TYPE_INTERFACE)
                        break;
                    if (T == USB_DESC_TYPE_HID && L >= 9) {
                        UINT8 NumClass = ConfigBytes[Scan + 5];
                        UINTN ClassPos = Scan + 6;
                        UINTN I;
                        for (I = 0; I < NumClass && ClassPos + 3 <= Scan + L; ++I) {
                            UINT8 ClassType = ConfigBytes[ClassPos];
                            UINT16 ClassLen = (UINT16)ConfigBytes[ClassPos + 1] |
                                              ((UINT16)ConfigBytes[ClassPos + 2] << 8);
                            if (ClassType == USB_DESC_TYPE_REPORT) {
                                ReportLength = ClassLen;
                                break;
                            }
                            ClassPos += 3;
                        }
                        break;
                    }
                    Scan += L;
                }
                break;
            }
        }
        Pos += DescLen;
    }

    if (ReportLength == 0) {
        Status = EFI_UNSUPPORTED;
        goto Exit;
    }

    Report = AllocateZeroPool(ReportLength);
    if (!Report) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Exit;
    }

    ZeroMem(&Request, sizeof(Request));
    Request.RequestType = USB_REQUEST_TYPE_STD_INTF;
    Request.Request = USB_REQ_GET_DESCRIPTOR;
    Request.Value = (UINT16)(USB_DESC_TYPE_REPORT << 8);
    Request.Index = InterfaceNumber;
    Request.Length = ReportLength;

    Status = UsbIo->UsbControlTransfer(
        UsbIo, &Request, EfiUsbDataIn, 1000,
        Report, ReportLength, &Result);
    if (!EFI_ERROR(Status))
        Status = ParseReportDescriptor(Report, ReportLength);

Exit:
    if (Report)
        FreePool(Report);
    if (ConfigBytes)
        FreePool(ConfigBytes);
    return Status;
}

static EFI_STATUS
EFIAPI
UsbPointerInterrupt(
    IN VOID *Data,
    IN UINTN DataLength,
    IN VOID *Context,
    IN UINT32 Result
    )
{
    ARCS_USB_HID_CONTEXT *Ctx = (ARCS_USB_HID_CONTEXT *)Context;
    INT32 X;
    INT32 Y;

    if (!Ctx)
        return EFI_INVALID_PARAMETER;

    if (Result != EFI_USB_NOERROR) {
        Ctx->LastStatus = EFI_DEVICE_ERROR;
        return EFI_DEVICE_ERROR;
    }
    if (!Data || DataLength == 0)
        return EFI_SUCCESS;

    if (Ctx->X.ReportId != 0 && ((UINT8 *)Data)[0] != Ctx->X.ReportId)
        return EFI_SUCCESS;

    X = FieldValue(&Ctx->X, Data, DataLength);
    Y = FieldValue(&Ctx->Y, Data, DataLength);

    if (Ctx->Kind == ArcsUsbPointerRelative) {
        Ctx->Dx += X;
        Ctx->Dy += Y;
    } else {
        Ctx->XValue = (UINT64)(INT64)X;
        Ctx->YValue = (UINT64)(INT64)Y;
    }

    Ctx->Buttons = 0x07;
    if (Ctx->Button1.Valid && FieldValue(&Ctx->Button1, Data, DataLength))
        Ctx->Buttons &= (UINT8)~0x04;
    if (Ctx->Button2.Valid && FieldValue(&Ctx->Button2, Data, DataLength))
        Ctx->Buttons &= (UINT8)~0x01;

    Ctx->Pending = TRUE;
    ++Ctx->PacketCount;
    Ctx->LastStatus = EFI_SUCCESS;
    if (Ctx->Event)
        (VOID)gBS->SignalEvent(Ctx->Event);
    return EFI_SUCCESS;
}

static EFI_STATUS
TryUsbIo(EFI_USB_IO_PROTOCOL *UsbIo)
{
    EFI_STATUS Status;
    EFI_USB_INTERFACE_DESCRIPTOR Interface;
    EFI_USB_ENDPOINT_DESCRIPTOR Endpoint;
    UINTN I;
    BOOLEAN FoundEndpoint = FALSE;

    ZeroMem(&UsbPointer.X, sizeof(UsbPointer.X));
    ZeroMem(&UsbPointer.Y, sizeof(UsbPointer.Y));
    ZeroMem(&UsbPointer.Wheel, sizeof(UsbPointer.Wheel));
    ZeroMem(&UsbPointer.Button1, sizeof(UsbPointer.Button1));
    ZeroMem(&UsbPointer.Button2, sizeof(UsbPointer.Button2));
    UsbPointer.Kind = ArcsUsbPointerNone;

    Status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &Interface);
    if (EFI_ERROR(Status) || Interface.InterfaceClass != USB_HID_CLASS)
        return EFI_UNSUPPORTED;

    ++UsbPointer.HidCount;

    for (I = 0; I < Interface.NumEndpoints; ++I) {
        Status = UsbIo->UsbGetEndpointDescriptor(UsbIo, (UINT8)I, &Endpoint);
        if (EFI_ERROR(Status))
            continue;
        if ((Endpoint.Attributes & 0x03) == USB_ENDPOINT_INTERRUPT &&
            (Endpoint.EndpointAddress & USB_ENDPOINT_DIR_IN)) {
            FoundEndpoint = TRUE;
            break;
        }
    }
    if (!FoundEndpoint)
        return EFI_UNSUPPORTED;

    Status = FetchReportDescriptor(UsbIo, Interface.InterfaceNumber);
    if (EFI_ERROR(Status))
        return Status;

    UsbPointer.UsbIo = UsbIo;
    CopyMem(&UsbPointer.Interface, &Interface, sizeof(Interface));
    CopyMem(&UsbPointer.Endpoint, &Endpoint, sizeof(Endpoint));
    UsbPointer.Buttons = 0x07;
    UsbPointer.LastStatus = EFI_NOT_READY;

    Status = gBS->CreateEvent(0, TPL_APPLICATION, NULL, NULL, &UsbPointer.Event);
    if (EFI_ERROR(Status))
        return Status;

    Status = UsbIo->UsbAsyncInterruptTransfer(
        UsbIo,
        Endpoint.EndpointAddress,
        TRUE,
        Endpoint.Interval ? Endpoint.Interval : 1,
        Endpoint.MaxPacketSize,
        UsbPointerInterrupt,
        &UsbPointer);
    if (EFI_ERROR(Status)) {
        gBS->CloseEvent(UsbPointer.Event);
        UsbPointer.Event = NULL;
        return Status;
    }

    UsbPointer.Active = TRUE;
    return EFI_SUCCESS;
}

EFI_STATUS
ArcsUsbHidPointerInit(VOID)
{
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0;
    UINTN I;
    EFI_STATUS Status;

    ArcsUsbHidPointerShutdown();
    ZeroMem(&UsbPointer, sizeof(UsbPointer));
    UsbPointer.Buttons = 0x07;
    UsbPointer.LastStatus = EFI_NOT_FOUND;

    Status = gBS->LocateHandleBuffer(
        ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status)) {
        UsbPointer.LastStatus = Status;
        return Status;
    }

    UsbPointer.UsbIoCount = Count;
    for (I = 0; I < Count; ++I) {
        EFI_USB_IO_PROTOCOL *UsbIo = NULL;

        Status = gBS->HandleProtocol(
            Handles[I], &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo);
        if (EFI_ERROR(Status) || !UsbIo)
            continue;

        Status = TryUsbIo(UsbIo);
        if (!EFI_ERROR(Status)) {
            FreePool(Handles);
            return EFI_SUCCESS;
        }
    }

    FreePool(Handles);
    UsbPointer.LastStatus = EFI_NOT_FOUND;
    return EFI_NOT_FOUND;
}

VOID
ArcsUsbHidPointerShutdown(VOID)
{
    if (UsbPointer.Active && UsbPointer.UsbIo) {
        (VOID)UsbPointer.UsbIo->UsbAsyncInterruptTransfer(
            UsbPointer.UsbIo,
            UsbPointer.Endpoint.EndpointAddress,
            FALSE,
            0,
            0,
            NULL,
            NULL);
    }
    if (UsbPointer.Event)
        (VOID)gBS->CloseEvent(UsbPointer.Event);
    ZeroMem(&UsbPointer, sizeof(UsbPointer));
}

BOOLEAN
ArcsUsbHidPointerPresent(VOID)
{
    return UsbPointer.Active;
}

EFI_EVENT
ArcsUsbHidPointerEvent(VOID)
{
    return UsbPointer.Event;
}

EFI_STATUS
ArcsUsbHidPointerGetState(ARCS_USB_POINTER_STATE *State)
{
    EFI_TPL OldTpl;

    if (!State)
        return EFI_INVALID_PARAMETER;
    if (!UsbPointer.Active)
        return EFI_NOT_FOUND;

    OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
    if (!UsbPointer.Pending) {
        gBS->RestoreTPL(OldTpl);
        return EFI_NOT_READY;
    }

    ZeroMem(State, sizeof(*State));
    State->Kind = UsbPointer.Kind;
    State->Buttons = UsbPointer.Buttons;
    if (UsbPointer.Kind == ArcsUsbPointerRelative) {
        State->Dx = UsbPointer.Dx;
        State->Dy = UsbPointer.Dy;
        UsbPointer.Dx = 0;
        UsbPointer.Dy = 0;
    } else {
        State->X = UsbPointer.XValue;
        State->Y = UsbPointer.YValue;
        State->MinX = (UINT64)(INT64)UsbPointer.X.LogicalMin;
        State->MaxX = (UINT64)(INT64)UsbPointer.X.LogicalMax;
        State->MinY = (UINT64)(INT64)UsbPointer.Y.LogicalMin;
        State->MaxY = (UINT64)(INT64)UsbPointer.Y.LogicalMax;
    }
    UsbPointer.Pending = FALSE;
    gBS->RestoreTPL(OldTpl);
    return EFI_SUCCESS;
}

UINTN ArcsUsbHidPointerUsbIoCount(VOID) { return UsbPointer.UsbIoCount; }
UINTN ArcsUsbHidPointerHidCount(VOID) { return UsbPointer.HidCount; }
UINT64 ArcsUsbHidPointerPacketCount(VOID) { return UsbPointer.PacketCount; }
ARCS_USB_POINTER_KIND ArcsUsbHidPointerKind(VOID) { return UsbPointer.Kind; }
EFI_STATUS ArcsUsbHidPointerLastStatus(VOID) { return UsbPointer.LastStatus; }
