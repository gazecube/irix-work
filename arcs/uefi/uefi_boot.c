#include <Uefi.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

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

static EFI_STATUS
ArcsFindFileSystemImage(
    IN CHAR16 *Path,
    OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath
    )
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles;
    UINTN HandleCount;
    UINTN I;

    if (Path == NULL || DevicePath == NULL)
        return EFI_INVALID_PARAMETER;

    *DevicePath = NULL;
    Handles = NULL;
    HandleCount = 0;

    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &HandleCount,
        &Handles
        );
    if (EFI_ERROR(Status))
        return Status;

    Status = EFI_NOT_FOUND;

    for (I = 0; I < HandleCount; ++I) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
        EFI_FILE_PROTOCOL *Root;
        EFI_FILE_PROTOCOL *File;
        EFI_DEVICE_PATH_PROTOCOL *CandidatePath;
        EFI_STATUS OpenStatus;

        FileSystem = NULL;
        Root = NULL;
        File = NULL;
        CandidatePath = NULL;

        OpenStatus = gBS->HandleProtocol(
            Handles[I],
            &gEfiSimpleFileSystemProtocolGuid,
            (VOID **)&FileSystem
            );
        if (EFI_ERROR(OpenStatus) || FileSystem == NULL)
            continue;

        OpenStatus = FileSystem->OpenVolume(FileSystem, &Root);
        if (EFI_ERROR(OpenStatus) || Root == NULL)
            continue;

        OpenStatus = Root->Open(
            Root,
            &File,
            Path,
            EFI_FILE_MODE_READ,
            0
            );
        if (!EFI_ERROR(OpenStatus) && File != NULL) {
            (VOID)File->Close(File);
            CandidatePath = FileDevicePath(Handles[I], Path);
            if (CandidatePath != NULL) {
                *DevicePath = CandidatePath;
                Status = EFI_SUCCESS;
            } else {
                Status = EFI_OUT_OF_RESOURCES;
            }
        }

        (VOID)Root->Close(Root);

        if (!EFI_ERROR(Status))
            break;
    }

    if (Handles != NULL)
        FreePool(Handles);

    return Status;
}

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

    /* Prefer a FreeBSD loader on its native filesystem.  In the current QEMU
     * test setup /dev/sda1 is exposed as a standalone FAT filesystem whose
     * host mount point is /boot; therefore the path visible through UEFI is
     * \\EFI\\FreeBSD\\loader.efi.  Loading it from that filesystem handle keeps
     * LoadedImage->DeviceHandle tied to the real FreeBSD boot filesystem so
     * loader.efi can find loader.env and the rest of its boot tree normally.
     *
     * This filesystem search is reconstructed UEFI glue.  The eventual path
     * selection policy still belongs to the preserved ARCS
     * SystemPartition/OSLoader layer.
     */
    DevicePath = NULL;
    Status = ArcsFindFileSystemImage(
        L"\\EFI\\FreeBSD\\loader.efi",
        &DevicePath
        );

    /* Generic bringup fallback: ARCS itself occupies the standard removable
     * media path in the temporary QEMU ESP, while the wrapper stages an EFI
     * test payload at \\EFI\\ARCS\\BOOT.EFI on that same filesystem.
     */
    if (EFI_ERROR(Status)) {
        DevicePath = FileDevicePath(
            LoadedImage->DeviceHandle,
            L"\\EFI\\ARCS\\BOOT.EFI"
            );
        if (DevicePath == NULL)
            return EFI_OUT_OF_RESOURCES;
    }

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
