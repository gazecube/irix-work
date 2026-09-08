#include <Uefi.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
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
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath;
    EFI_HANDLE ChildImage;

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

    /* A successfully booted OS should not return.  If a UEFI application or
     * failed loader does return, release the child and hand control back to
     * the original SGI menu loop.
     */
    if (EFI_ERROR(Status))
        (VOID)gBS->UnloadImage(ChildImage);

    return Status;
}
