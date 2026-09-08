#include <Uefi.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>

#include "uefi_boot.h"

/* Reconstructed x86-64/UEFI backend.
 *
 * The original PROM's autoboot path eventually calls ARCS Execute() on the
 * bootfile assembled from SystemPartition and OSLoader.  On UEFI there is no
 * MIPS standalone loader to hand that path to, so this backend performs the
 * platform-specific final step with LoadImage()/StartImage().  The SGI menu
 * and boot-policy layer remain above this boundary.
 */

static EFI_HANDLE ArcsImageHandle;

void
ArcsUefiBootInit(EFI_HANDLE ImageHandle)
{
    ArcsImageHandle = ImageHandle;
}

EFI_STATUS
ArcsUefiBootDefault(void)
{
    EFI_STATUS Status;
    EFI_STATUS RestoreStatus;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath;
    EFI_HANDLE ChildImage;
    UINT32 ArcsGopMode;
    BOOLEAN HaveArcsGopMode;

    if (ArcsImageHandle == NULL)
        return EFI_NOT_READY;

    LoadedImage = NULL;
    Status = gBS->HandleProtocol(
        ArcsImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage
        );
    if (EFI_ERROR(Status))
        return Status;

    /* Preserve the PROM's active graphics mode across a returned EFI child.
     * EFI applications are allowed to change GOP mode; Tetrefis deliberately
     * exercises this by switching to 640x480.  A real OS will normally never
     * return, but a utility that does must hand the display back to ARCS in
     * the mode ARCS was using before StartImage().
     */
    Gop = NULL;
    ArcsGopMode = 0;
    HaveArcsGopMode = FALSE;
    Status = gBS->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop
        );
    if (!EFI_ERROR(Status) && Gop != NULL && Gop->Mode != NULL) {
        ArcsGopMode = Gop->Mode->Mode;
        HaveArcsGopMode = TRUE;
    }

    /* First boot milestone: ARCS itself occupies the standard removable-media
     * fallback path (\\EFI\\BOOT\\BOOTX64.EFI) in the QEMU test ESP, so use a
     * distinct payload path to avoid recursively launching ARCS again.
     *
     * The QEMU wrapper stages a detected test application here.  Later, the
     * preserved SGI SystemPartition/OSLoader policy will translate to this
     * backend instead of hard-coding this pathname.
     */
    DevicePath = FileDevicePath(
        LoadedImage->DeviceHandle,
        L"\\EFI\\ARCS\\BOOT.EFI"
        );
    if (DevicePath == NULL)
        return EFI_OUT_OF_RESOURCES;

    ChildImage = NULL;
    Status = gBS->LoadImage(
        FALSE,
        ArcsImageHandle,
        DevicePath,
        NULL,
        0,
        &ChildImage
        );
    FreePool(DevicePath);

    if (EFI_ERROR(Status))
        return Status;

    Status = gBS->StartImage(ChildImage, NULL, NULL);

    /* If the child returned after changing display mode, put GOP back exactly
     * where the PROM left it.  The maintenance-menu layer repaints the SGI UI
     * after this function returns.
     */
    RestoreStatus = EFI_SUCCESS;
    if (HaveArcsGopMode && Gop != NULL && Gop->Mode != NULL &&
        Gop->Mode->Mode != ArcsGopMode) {
        RestoreStatus = Gop->SetMode(Gop, ArcsGopMode);
    }

    /* A successfully booted OS should not return.  If a UEFI application or
     * failed loader does return, release the child and hand control back to
     * the original SGI menu loop.
     */
    if (EFI_ERROR(Status))
        (VOID)gBS->UnloadImage(ChildImage);

    if (!EFI_ERROR(Status) && EFI_ERROR(RestoreStatus))
        return RestoreStatus;

    return Status;
}
