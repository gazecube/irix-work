#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include "drawbackground_bringup.h"
#include "input_bringup.h"
#include "maintenance_menu_bringup.h"
#include "prom_startup.h"
#include "uefi_boot.h"

EFI_STATUS
EFIAPI
UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
    )
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    ArcsUefiBootInit(ImageHandle);
    ArcsPromAutobootInit(SystemTable);

    Gop = NULL;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status))
        return Status;

    Status = ArcsDrawBackgroundBringup(Gop);
    if (EFI_ERROR(Status))
        return Status;

    /* Follow the original PROM startup policy before entering maintenance:
     * AutoLoad=Y and no NoAutoLoad -> five-second interruptible autoboot.
     * If Escape interrupts it, or a child returns/fails, startup() returns and
     * only then is the System Maintenance Menu displayed.
     */
    startup();

    ArcsDrawMaintenanceMenuBringup();

    /* Feed firmware keyboard and pointer state into the original SGI GUI
     * event machinery.  Escape returns to the containing UEFI firmware. */
    return ArcsRunInputBringup(SystemTable);
}
