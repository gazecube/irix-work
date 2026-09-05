#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include "drawbackground_bringup.h"
#include "input_bringup.h"

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

    Gop = NULL;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status))
        return Status;

    Status = ArcsDrawBackgroundBringup(Gop);
    if (EFI_ERROR(Status))
        return Status;

    /* Feed firmware keyboard and pointer state into the original SGI GUI
     * event machinery.  Escape returns to the containing UEFI firmware. */
    return ArcsRunInputBringup(SystemTable);
}
