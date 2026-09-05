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
    UINTN Index;
    EFI_INPUT_KEY Key;

    (VOID)ImageHandle;

    Gop = NULL;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status))
        return Status;

    Status = ArcsDrawBackgroundBringup(Gop);
    if (EFI_ERROR(Status))
        return Status;

    /* First visual checkpoint: keep the rendered PROM screen visible until
     * the tester explicitly presses a key.  Returning immediately hands
     * control back to OVMF and hides the frame before it can be inspected. */
    if (SystemTable && SystemTable->ConIn && SystemTable->ConIn->WaitForKey) {
        Status = gBS->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &Index);
        if (!EFI_ERROR(Status))
            (VOID)SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
    }

    return EFI_SUCCESS;
}
