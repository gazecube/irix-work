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
 * The original PROM autoboot path eventually calls ARCS Execute() on the
 * bootfile assembled from SystemPartition and OSLoader.  This file is the
 * machine-specific boundary: SGI policy remains above it, while EFI image
 * discovery and LoadImage()/StartImage() live here.
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

static EFI_STATUS
ArcsStartDevicePath(EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
    EFI_STATUS Status;
    EFI_STATUS RestoreStatus;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_HANDLE ChildImage;
    UINT32 ArcsGopMode;
    BOOLEAN HaveArcsGopMode;

    if (ArcsImageHandle == NULL || DevicePath == NULL)
        return EFI_INVALID_PARAMETER;

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

    ChildImage = NULL;
    Status = gBS->LoadImage(
        FALSE,
        ArcsImageHandle,
        DevicePath,
        NULL,
        0,
        &ChildImage
        );
    if (EFI_ERROR(Status))
        return Status;

    Status = gBS->StartImage(ChildImage, NULL, NULL);

    RestoreStatus = EFI_SUCCESS;
    if (HaveArcsGopMode && Gop != NULL && Gop->Mode != NULL &&
        Gop->Mode->Mode != ArcsGopMode) {
        RestoreStatus = Gop->SetMode(Gop, ArcsGopMode);
    }

    if (EFI_ERROR(Status))
        (VOID)gBS->UnloadImage(ChildImage);

    if (!EFI_ERROR(Status) && EFI_ERROR(RestoreStatus))
        return RestoreStatus;

    return Status;
}

static EFI_STATUS
ArcsBootEfiPath(CHAR16 *Path)
{
    EFI_STATUS Status;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath;

    DevicePath = NULL;
    Status = ArcsFindFileSystemImage(Path, &DevicePath);
    if (EFI_ERROR(Status))
        return Status;

    Status = ArcsStartDevicePath(DevicePath);
    FreePool(DevicePath);
    return Status;
}

void
ArcsUefiBootInit(EFI_HANDLE ImageHandle)
{
    ArcsImageHandle = ImageHandle;
}

EFI_STATUS
ArcsUefiBootArcsPath(const char *Bootfile)
{
    CHAR16 Path[260];
    const char *Slash;
    UINTN I;

    if (Bootfile == NULL)
        return EFI_INVALID_PARAMETER;

    /*
     * Original make_bootfile() produces:
     *
     *   SystemPartition + "/" + OSLoader
     *
     * For the UEFI port the left side remains an ARCS device identifier.  The
     * right side is translated to an EFI filesystem path at the Execute()
     * boundary.  Device-handle matching for the ARCS identifier is the next
     * layer; for this bringup checkpoint the filesystem containing the named
     * loader is selected by exact path presence, preserving the loader's
     * native DeviceHandle.
     */
    Slash = Bootfile;
    while (*Slash && *Slash != '/')
        ++Slash;
    if (*Slash != '/')
        return EFI_NOT_FOUND;

    I = 0;
    Path[I++] = L'\\';
    ++Slash;
    while (*Slash && I + 1 < (sizeof(Path) / sizeof(Path[0]))) {
        Path[I++] = (*Slash == '/') ? L'\\' : (CHAR16)(UINT8)*Slash;
        ++Slash;
    }
    Path[I] = L'\0';

    return ArcsBootEfiPath(Path);
}

INT32
Execute(char *Path, INT32 Argc, char **Argv, char **Envp)
{
    EFI_STATUS Status;

    (void)Argc;
    (void)Argv;
    (void)Envp;

    Status = ArcsUefiBootArcsPath(Path);
    if (EFI_ERROR(Status))
        return 1;
    return 0;
}

EFI_STATUS
ArcsUefiBootDefault(void)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath;

    if (ArcsImageHandle == NULL)
        return EFI_NOT_READY;

    /* Compatibility fallback retained for generic bringup payloads. */
    Status = ArcsBootEfiPath(L"\\EFI\\FreeBSD\\loader.efi");
    if (!EFI_ERROR(Status))
        return Status;

    LoadedImage = NULL;
    Status = gBS->HandleProtocol(
        ArcsImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage
        );
    if (EFI_ERROR(Status))
        return Status;

    DevicePath = FileDevicePath(
        LoadedImage->DeviceHandle,
        L"\\EFI\\ARCS\\BOOT.EFI"
        );
    if (DevicePath == NULL)
        return EFI_OUT_OF_RESOURCES;

    Status = ArcsStartDevicePath(DevicePath);
    FreePool(DevicePath);
    return Status;
}
