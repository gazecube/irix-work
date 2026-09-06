#ifndef ARCS_UEFI_USB_HID_POINTER_H
#define ARCS_UEFI_USB_HID_POINTER_H

#include <Uefi.h>

typedef enum {
    ArcsUsbPointerNone = 0,
    ArcsUsbPointerRelative,
    ArcsUsbPointerAbsolute
} ARCS_USB_POINTER_KIND;

typedef struct {
    ARCS_USB_POINTER_KIND Kind;
    INT32 Dx;
    INT32 Dy;
    UINT64 X;
    UINT64 Y;
    UINT64 MinX;
    UINT64 MaxX;
    UINT64 MinY;
    UINT64 MaxY;
    UINT8 Buttons;
} ARCS_USB_POINTER_STATE;

/*
 * Standard-UEFI USB HID pointer fallback. This deliberately consumes only
 * EFI_USB_IO_PROTOCOL and does not depend on a firmware-specific mouse DXE.
 * It is used when no real Simple/Absolute Pointer handle exists beneath the
 * console splitter.
 */
EFI_STATUS ArcsUsbHidPointerInit(VOID);
VOID ArcsUsbHidPointerShutdown(VOID);
BOOLEAN ArcsUsbHidPointerPresent(VOID);
EFI_EVENT ArcsUsbHidPointerEvent(VOID);
EFI_STATUS ArcsUsbHidPointerGetState(ARCS_USB_POINTER_STATE *State);

UINTN ArcsUsbHidPointerUsbIoCount(VOID);
UINTN ArcsUsbHidPointerHidCount(VOID);
UINT64 ArcsUsbHidPointerPacketCount(VOID);
ARCS_USB_POINTER_KIND ArcsUsbHidPointerKind(VOID);
EFI_STATUS ArcsUsbHidPointerLastStatus(VOID);

#endif
