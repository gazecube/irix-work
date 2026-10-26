#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
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

extern char *getenv(const char *Name);
extern int setenv(char *Name, char *Value);


/* UEFI machine glue: make sure driver/controller trees have been started
 * before looking for boot filesystems.  Interactive input bringup also does
 * this for pointer discovery, but startup() runs before the interactive loop.
 * Boot discovery must therefore be self-contained rather than depending on
 * input initialization having happened first.
 */
static VOID
ArcsConnectAllControllers(VOID)
{
    EFI_HANDLE *Handles;
    UINTN HandleCount;
    UINTN I;

    Handles = NULL;
    HandleCount = 0;

    if (EFI_ERROR(gBS->LocateHandleBuffer(
            AllHandles,
            NULL,
            NULL,
            &HandleCount,
            &Handles)))
        return;

    for (I = 0; I < HandleCount; ++I)
        (VOID)gBS->ConnectController(Handles[I], NULL, NULL, TRUE);

    if (Handles != NULL)
        FreePool(Handles);
}

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

    /* startup() reaches Execute() before the interactive input layer has had
     * any reason to enumerate devices.  Connect the firmware controller tree
     * here so early autoboot and manual Start System see the same disks.
     */
    ArcsConnectAllControllers();

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

    /*
     * The ARCS OSLoadOptions variable is the boot option string.  Hand it to
     * the child as its EFI load options; for a Linux EFI-stub kernel this is
     * the kernel command line.
     */
    {
        EFI_LOADED_IMAGE_PROTOCOL *Child = NULL;
        char *Opts = getenv("OSLoadOptions");

        if (Opts && *Opts &&
            !EFI_ERROR(gBS->HandleProtocol(ChildImage, &gEfiLoadedImageProtocolGuid,
                                           (VOID **)&Child)) && Child) {
            UINTN Len = AsciiStrLen(Opts);
            CHAR16 *Wide = AllocateZeroPool((Len + 1) * sizeof(CHAR16));

            if (Wide) {
                UINTN K;

                for (K = 0; K < Len; K++)
                    Wide[K] = (CHAR16)(UINT8)Opts[K];
                Child->LoadOptions = Wide;
                Child->LoadOptionsSize = (UINT32)((Len + 1) * sizeof(CHAR16));
            }
        }
    }

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

/*
 * Persistent ARCS environment.  A real PROM keeps variables in NVRAM; here
 * NAME=value lines in \EFI\ARCS\arcs.env on the ESP the PROM was loaded
 * from override the compiled-in defaults.  '#' starts a comment.
 */
void
ArcsUefiLoadEnvFile(void)
{
    EFI_LOADED_IMAGE_PROTOCOL *Img = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
    UINTN Size = 4095, I, Start;
    char *Buf;

    if (!ArcsImageHandle ||
        EFI_ERROR(gBS->HandleProtocol(ArcsImageHandle, &gEfiLoadedImageProtocolGuid,
                                      (VOID **)&Img)) || !Img ||
        EFI_ERROR(gBS->HandleProtocol(Img->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid,
                                      (VOID **)&Fs)) || !Fs ||
        EFI_ERROR(Fs->OpenVolume(Fs, &Root)) ||
        EFI_ERROR(Root->Open(Root, &File, L"\\EFI\\ARCS\\arcs.env",
                             EFI_FILE_MODE_READ, 0)))
        return;

    Buf = AllocateZeroPool(Size + 1);
    if (Buf && !EFI_ERROR(File->Read(File, &Size, Buf))) {
        Buf[Size] = 0;
        for (I = 0, Start = 0; I <= Size; I++) {
            if (Buf[I] == '\n' || Buf[I] == '\r' || Buf[I] == 0) {
                char *Eq;

                Buf[I] = 0;
                if (Buf[Start] && Buf[Start] != '#' &&
                    (Eq = AsciiStrStr(Buf + Start, "=")) != NULL) {
                    *Eq = 0;
                    (void)setenv(Buf + Start, Eq + 1);
                }
                Start = I + 1;
            }
        }
    }
    if (Buf)
        FreePool(Buf);
    File->Close(File);
    Root->Close(Root);
}

/*
 * Write (replace) a file on the ESP the PROM was loaded from.  Used for the
 * diagnostic log, since real hardware has no serial console to read.
 */
void
ArcsUefiWriteFile(CONST CHAR16 *Path, CONST CHAR8 *Data, UINTN Size)
{
    EFI_LOADED_IMAGE_PROTOCOL *Img = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;

    if (!ArcsImageHandle ||
        EFI_ERROR(gBS->HandleProtocol(ArcsImageHandle, &gEfiLoadedImageProtocolGuid,
                                      (VOID **)&Img)) || !Img ||
        EFI_ERROR(gBS->HandleProtocol(Img->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid,
                                      (VOID **)&Fs)) || !Fs ||
        EFI_ERROR(Fs->OpenVolume(Fs, &Root)))
        return;

    /* Remove any previous copy so a shorter log does not leave old tail bytes. */
    if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16 *)Path,
                              EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) && File)
        File->Delete(File);
    File = NULL;
    if (!EFI_ERROR(Root->Open(Root, &File, (CHAR16 *)Path,
                              EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                              0)) && File) {
        File->Write(File, &Size, (VOID *)Data);
        File->Flush(File);
        File->Close(File);
    }
    Root->Close(Root);
}
