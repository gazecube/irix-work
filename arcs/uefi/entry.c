#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include "drawbackground_bringup.h"

EFI_STATUS
EFIAPI
UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
    )
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    (VOID)ImageHandle;
    (VOID)SystemTable;

    Gop = NULL;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status))
        return Status;

    return ArcsDrawBackgroundBringup(Gop);
}
