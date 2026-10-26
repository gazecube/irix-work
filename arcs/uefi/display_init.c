#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EdidActive.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#include "display_init.h"
#include "uefi_boot.h"

/* Reconstructed UEFI display glue.
 *
 * The SGI GUI itself remains resolution-independent and uses gfxWidth()/
 * gfxHeight().  This code only translates the platform's preferred display
 * timing into the GOP mode that should be active before the original SGI
 * drawing code starts.
 */

static BOOLEAN
ArcsPreferredEdidMode(UINT32 *Width, UINT32 *Height)
{
    EFI_STATUS Status;
    EFI_EDID_ACTIVE_PROTOCOL *Edid;
    CONST UINT8 *Dtd;
    UINT32 HActive;
    UINT32 VActive;

    Edid = NULL;
    Status = gBS->LocateProtocol(
        &gEfiEdidActiveProtocolGuid,
        NULL,
        (VOID **)&Edid
        );
    if (EFI_ERROR(Status) || Edid == NULL || Edid->Edid == NULL ||
        Edid->SizeOfEdid < 72)
        return FALSE;

    /* EDID 1.x preferred timing is the first detailed timing descriptor,
     * starting at byte 54.  A zero pixel clock means this is not a timing
     * descriptor, so leave the GOP mode unchanged in that case.
     */
    Dtd = Edid->Edid + 54;
    if (Dtd[0] == 0 && Dtd[1] == 0)
        return FALSE;

    HActive = (UINT32)Dtd[2] | ((UINT32)(Dtd[4] & 0xF0) << 4);
    VActive = (UINT32)Dtd[5] | ((UINT32)(Dtd[7] & 0xF0) << 4);

    if (HActive == 0 || VActive == 0)
        return FALSE;

    *Width = HActive;
    *Height = VActive;
    return TRUE;
}

static EFI_STATUS
ArcsSelectPreferredMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    UINT32 Width;
    UINT32 Height;
    UINT32 Mode;

    if (!ArcsPreferredEdidMode(&Width, &Height))
        return EFI_NOT_FOUND;

    for (Mode = 0; Mode < Gop->Mode->MaxMode; ++Mode) {
        EFI_STATUS Status;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
        UINTN InfoSize;

        Info = NULL;
        InfoSize = 0;
        Status = Gop->QueryMode(Gop, Mode, &InfoSize, &Info);
        if (EFI_ERROR(Status))
            continue;

        if (Info->HorizontalResolution == Width &&
            Info->VerticalResolution == Height) {
            FreePool(Info);
            if (Gop->Mode->Mode == Mode)
                return EFI_SUCCESS;
            return Gop->SetMode(Gop, Mode);
        }

        FreePool(Info);
    }

    return EFI_NOT_FOUND;
}

static EFI_STATUS
ArcsClearFramebuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;

    Black.Blue = 0;
    Black.Green = 0;
    Black.Red = 0;
    Black.Reserved = 0;

    return Gop->Blt(
        Gop,
        &Black,
        EfiBltVideoFill,
        0,
        0,
        0,
        0,
        Gop->Mode->Info->HorizontalResolution,
        Gop->Mode->Info->VerticalResolution,
        0
        );
}

EFI_STATUS
ArcsUefiPrepareDisplay(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_STATUS Status;

    if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL)
        return EFI_INVALID_PARAMETER;

    /* A missing/unsupported EDID mode is not fatal.  Keep the firmware's
     * current GOP mode and still clear it before the SGI GUI is drawn.
     */
    Status = ArcsSelectPreferredMode(Gop);
    if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND)
        return Status;

    return ArcsClearFramebuffer(Gop);
}

#define ARCS_LOGSZ 16384

EFI_GRAPHICS_OUTPUT_PROTOCOL *
ArcsUefiPickGop(void)
{
    EFI_HANDLE *H = NULL;
    UINTN N = 0, i;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Best = NULL;
    UINT32 BestMode = 0;
    UINT64 BestScore = 0;
    UINTN BestIdx = 0;
    CHAR8 *Log = AllocateZeroPool(ARCS_LOGSZ);
    UINTN L = 0;

    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                          NULL, &N, &H)) || N == 0) {
        if (Log) {
            AsciiSPrint(Log, ARCS_LOGSZ, "no GOP handles found\n");
            ArcsUefiWriteFile(L"\\EFI\\ARCS\\arcs.log", Log, AsciiStrLen(Log));
            FreePool(Log);
        }
        return NULL;
    }
    if (Log)
        L += AsciiSPrint(Log + L, ARCS_LOGSZ - L, "GOP handles: %u\n", (UINT32)N);

    for (i = 0; i < N; i++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *G = NULL;
        UINT32 M;
        UINT64 HBest = 0;
        UINT32 HBestMode = 0;

        if (EFI_ERROR(gBS->HandleProtocol(H[i], &gEfiGraphicsOutputProtocolGuid,
                                          (VOID **)&G)) || !G || !G->Mode)
            continue;
        if (Log)
            L += AsciiSPrint(Log + L, ARCS_LOGSZ - L,
                             "gop %u: MaxMode=%u current=%u %ux%u pixfmt=%u fb=0x%lx\n",
                             (UINT32)i, G->Mode->MaxMode, G->Mode->Mode,
                             G->Mode->Info->HorizontalResolution,
                             G->Mode->Info->VerticalResolution,
                             (UINT32)G->Mode->Info->PixelFormat,
                             (UINT64)G->Mode->FrameBufferBase);
        for (M = 0; M < G->Mode->MaxMode; M++) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = NULL;
            UINTN Sz = 0;
            UINT64 Score;

            if (EFI_ERROR(G->QueryMode(G, M, &Sz, &Info)) || !Info)
                continue;
            if (Log && M < 64)
                L += AsciiSPrint(Log + L, ARCS_LOGSZ - L, "  mode %u: %ux%u pixfmt=%u\n",
                                 M, Info->HorizontalResolution,
                                 Info->VerticalResolution, (UINT32)Info->PixelFormat);
            if (Info->PixelFormat == PixelBltOnly)
                Score = 0;
            else if (Info->HorizontalResolution == 1280 && Info->VerticalResolution == 1024)
                Score = (1ULL << 40);
            else
                Score = (UINT64)Info->HorizontalResolution * Info->VerticalResolution;
            if (Score > HBest) {
                HBest = Score;
                HBestMode = M;
            }
            FreePool(Info);
        }
        if (HBest > BestScore) {
            BestScore = HBest;
            Best = G;
            BestMode = HBestMode;
            BestIdx = i;
        }
    }

    if (Best) {
        if (Best->Mode->Mode != BestMode)
            (VOID)Best->SetMode(Best, BestMode);
        if (Log)
            L += AsciiSPrint(Log + L, ARCS_LOGSZ - L, "chosen: gop %u mode %u -> now %ux%u\n",
                             (UINT32)BestIdx, BestMode,
                             Best->Mode->Info->HorizontalResolution,
                             Best->Mode->Info->VerticalResolution);
    }
    if (Log) {
        ArcsUefiWriteFile(L"\\EFI\\ARCS\\arcs.log", Log, AsciiStrLen(Log));
        FreePool(Log);
    }
    FreePool(H);
    return Best;
}
