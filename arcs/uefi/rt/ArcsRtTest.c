/*
 * Test payload for the ARCS resident runtime.
 *
 * Loads ArcsRt.efi from the same ESP, opens a Command Monitor window, writes
 * through the protocol's SPB before ExitBootServices, exits boot services, and
 * writes again through the SPB's firmware vector: the same call an OS kernel
 * makes.  Then spins so the screen can be inspected.
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>

#include "arcs_rt.h"

static CONST EFI_GUID mProtocolGuid = ARCS_RT_PROTOCOL_GUID;

static VOID
Say(ARCS_WRITE Write, CONST CHAR8 *S)
{
    UINT32 N = 0, Cnt = 0;

    while (S[N])
        N++;
    (VOID)Write(1, (VOID *)S, N, &Cnt);
}

static VOID
Frame(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 X, UINT32 Y, UINT32 W, UINT32 H)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Light = { 0xD0, 0xD0, 0xD0, 0 };
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Dark = { 0x30, 0x30, 0x30, 0 };

    /* two-pixel bevelled frame around the window */
    Gop->Blt(Gop, &Light, EfiBltVideoFill, 0, 0, X - 2, Y - 2, W + 4, 2, 0);
    Gop->Blt(Gop, &Light, EfiBltVideoFill, 0, 0, X - 2, Y - 2, 2, H + 4, 0);
    Gop->Blt(Gop, &Dark, EfiBltVideoFill, 0, 0, X - 2, Y + H, W + 4, 2, 0);
    Gop->Blt(Gop, &Dark, EfiBltVideoFill, 0, 0, X + W, Y - 2, 2, H + 4, 0);
}

EFI_STATUS EFIAPI
ArcsRtTestMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_LOADED_IMAGE_PROTOCOL *Img = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    ARCS_RT_PROTOCOL *Rt = NULL;
    EFI_DEVICE_PATH_PROTOCOL *Path;
    EFI_HANDLE Drv = NULL;
    EFI_STATUS Status;
    ARCS_TV *Tv;
    ARCS_WRITE Write;
    UINT32 X, Y, W, H, I;

    (VOID)SystemTable;
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Img);
    if (EFI_ERROR(Status))
        return Status;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status))
        return Status;

    Path = FileDevicePath(Img->DeviceHandle, L"\\EFI\\ARCS\\ArcsRt.efi");
    Status = gBS->LoadImage(FALSE, ImageHandle, Path, NULL, 0, &Drv);
    if (EFI_ERROR(Status))
        return Status;
    Status = gBS->StartImage(Drv, NULL, NULL);
    if (EFI_ERROR(Status))
        return Status;
    Status = gBS->LocateProtocol((EFI_GUID *)&mProtocolGuid, NULL, (VOID **)&Rt);
    if (EFI_ERROR(Status))
        return Status;

    W = Gop->Mode->Info->HorizontalResolution * 3 / 5;
    H = Gop->Mode->Info->VerticalResolution * 3 / 5;
    X = (Gop->Mode->Info->HorizontalResolution - W) / 2;
    Y = (Gop->Mode->Info->VerticalResolution - H) / 2;
    Frame(Gop, X, Y, W, H);
    Status = Rt->Configure(Rt, Gop, X, Y, W, H, 0xE8E8E8, 0x101830);
    if (EFI_ERROR(Status))
        return Status;

    Tv = (ARCS_TV *)(UINTN)Rt->Spb->TransferVector;
    Write = (ARCS_WRITE)(UINTN)Tv->Write;
    Say(Write, "ARCS resident console\r\n");
    Say(Write, "before ExitBootServices: boot services still available\r\n");

    {
        UINTN MapSize = 0, MapKey, DescSize;
        UINT32 DescVer;
        EFI_MEMORY_DESCRIPTOR *Map = NULL;

        Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
        MapSize += 4 * DescSize;
        Map = AllocatePool(MapSize);
        for (I = 0; I < 5; I++) {
            UINTN Sz = MapSize;

            Status = gBS->GetMemoryMap(&Sz, Map, &MapKey, &DescSize, &DescVer);
            if (EFI_ERROR(Status))
                continue;
            Status = gBS->ExitBootServices(ImageHandle, MapKey);
            if (!EFI_ERROR(Status))
                break;
        }
        if (EFI_ERROR(Status))
            return Status;
    }

    /* Boot services are gone: only the resident console works from here. */
    Say(Write, "after ExitBootServices: the resident ARCS Write() still works\r\n");
    for (I = 0; I < 60; I++) {
        CHAR8 Line[48];
        UINTN N = 0;
        CONST CHAR8 *Pre = "kernel message ";

        while (Pre[N]) {
            Line[N] = Pre[N];
            N++;
        }
        Line[N++] = '0' + (I / 10);
        Line[N++] = '0' + (I % 10);
        Line[N++] = '\r';
        Line[N++] = '\n';
        Line[N] = 0;
        Say(Write, Line);
    }
    Say(Write, "scrolling works; end of test\r\n");
    for (;;)
        CpuPause();
}
