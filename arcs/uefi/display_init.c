#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EdidActive.h>
#include <Protocol/EdidDiscovered.h>
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
#define ARCS_MAXRES 96

extern char *getenv(const char *Name);

typedef struct { UINT32 W, H; } ARCS_RES;

typedef struct {
    BOOLEAN Valid;
    BOOLEAN HaveNative;
    ARCS_RES Native;
    UINTN Count;
    ARCS_RES Res[ARCS_MAXRES];
} ARCS_EDID_INFO;

static VOID
ArcsEdidAdd(ARCS_EDID_INFO *I, UINT32 W, UINT32 H)
{
    UINTN k;

    if (W < 320 || H < 200 || W > 8192 || H > 8192)
        return;
    for (k = 0; k < I->Count; k++)
        if (I->Res[k].W == W && I->Res[k].H == H)
            return;
    if (I->Count < ARCS_MAXRES) {
        I->Res[I->Count].W = W;
        I->Res[I->Count].H = H;
        I->Count++;
    }
}

static BOOLEAN
ArcsEdidDtd(CONST UINT8 *D, UINT32 *W, UINT32 *H)
{
    if (D[0] == 0 && D[1] == 0)
        return FALSE;
    *W = (UINT32)D[2] | ((UINT32)(D[4] & 0xF0) << 4);
    *H = (UINT32)D[5] | ((UINT32)(D[7] & 0xF0) << 4);
    return *W != 0 && *H != 0;
}

/* Parse what the monitor says it supports: preferred (native) timing, all
 * detailed timings (base block and CEA extensions), standard and established
 * timings. */
static VOID
ArcsParseEdid(CONST UINT8 *E, UINTN Size, ARCS_EDID_INFO *I)
{
    static CONST UINT8 Hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    UINT32 W, H, Sum = 0;
    UINTN k, blk;

    ZeroMem(I, sizeof(*I));
    if (!E || Size < 128 || CompareMem(E, Hdr, 8) != 0)
        return;
    for (k = 0; k < 128; k++)
        Sum += E[k];
    if ((Sum & 0xFF) != 0)
        return;
    I->Valid = TRUE;

    if (ArcsEdidDtd(E + 54, &W, &H)) {
        I->HaveNative = TRUE;
        I->Native.W = W;
        I->Native.H = H;
        ArcsEdidAdd(I, W, H);
    }
    for (k = 72; k <= 108; k += 18)
        if (ArcsEdidDtd(E + k, &W, &H))
            ArcsEdidAdd(I, W, H);

    /* established timings */
    if (E[35] & 0x20) ArcsEdidAdd(I, 640, 480);
    if (E[35] & 0x01) ArcsEdidAdd(I, 800, 600);
    if (E[35] & 0x02) ArcsEdidAdd(I, 800, 600);
    if (E[36] & 0x08) ArcsEdidAdd(I, 1024, 768);
    if (E[36] & 0x04) ArcsEdidAdd(I, 1024, 768);
    if (E[36] & 0x02) ArcsEdidAdd(I, 1024, 768);
    if (E[36] & 0x01) ArcsEdidAdd(I, 1280, 1024);
    if (E[37] & 0x80) ArcsEdidAdd(I, 1152, 870);

    /* standard timings */
    for (k = 38; k < 54; k += 2) {
        UINT32 X, Y;

        if (E[k] == 0x01 && E[k + 1] == 0x01)
            continue;
        X = ((UINT32)E[k] + 31) * 8;
        switch (E[k + 1] >> 6) {
        case 0:  Y = X * 10 / 16; break;
        case 1:  Y = X * 3 / 4;   break;
        case 2:  Y = X * 4 / 5;   break;
        default: Y = X * 9 / 16;  break;
        }
        ArcsEdidAdd(I, X, Y);
    }

    /* CEA-861 extension blocks carry more detailed timings */
    for (blk = 1; blk * 128 + 128 <= Size && blk <= (UINTN)E[126]; blk++) {
        CONST UINT8 *X = E + blk * 128;
        UINTN d = X[2];

        if (X[0] != 0x02 || d < 4)
            continue;
        for (; d + 18 <= 127; d += 18)
            if (ArcsEdidDtd(X + d, &W, &H))
                ArcsEdidAdd(I, W, H);
    }
}

static BOOLEAN
ArcsEdidHas(CONST ARCS_EDID_INFO *I, UINT32 W, UINT32 H)
{
    UINTN k;

    for (k = 0; k < I->Count; k++)
        if (I->Res[k].W == W && I->Res[k].H == H)
            return TRUE;
    return FALSE;
}

/* The EDID the monitor itself sent (Discovered), never an override. */
static CONST UINT8 *
ArcsGetMonitorEdid(EFI_HANDLE Handle, UINTN *Size)
{
    EFI_EDID_DISCOVERED_PROTOCOL *D = NULL;

    if (!EFI_ERROR(gBS->HandleProtocol(Handle, &gEfiEdidDiscoveredProtocolGuid,
                                       (VOID **)&D)) &&
        D && D->Edid && D->SizeOfEdid >= 128) {
        *Size = D->SizeOfEdid;
        return D->Edid;
    }
    return NULL;
}

/* arcs.env ArcsResolution=WxH forces a mode; "auto" or unset uses the monitor. */
static BOOLEAN
ArcsForcedResolution(UINT32 *W, UINT32 *H)
{
    CONST CHAR8 *V = getenv("ArcsResolution");
    UINT32 A = 0, B = 0;

    if (!V || !*V || !AsciiStrCmp(V, "auto"))
        return FALSE;
    while (*V >= '0' && *V <= '9')
        A = A * 10 + (*V++ - '0');
    if (*V != 'x' && *V != 'X')
        return FALSE;
    V++;
    while (*V >= '0' && *V <= '9')
        B = B * 10 + (*V++ - '0');
    if (!A || !B)
        return FALSE;
    *W = A;
    *H = B;
    return TRUE;
}

/* Score a GOP and find its best mode.  Higher is better; 0 means unusable. */
static UINT64
ArcsScoreGop(EFI_GRAPHICS_OUTPUT_PROTOCOL *G, EFI_HANDLE Handle,
             CONST UINT32 *ForceW, CONST UINT32 *ForceH,
             UINT32 *BestMode, CHAR8 *Log, UINTN *L)
{
    ARCS_EDID_INFO Info;
    CONST UINT8 *E;
    UINTN EdidSize = 0;
    UINT32 M;
    UINT64 BestScore = 0;

    E = ArcsGetMonitorEdid(Handle, &EdidSize);
    ArcsParseEdid(E, EdidSize, &Info);
    if (Log)
        *L += AsciiSPrint(Log + *L, ARCS_LOGSZ - *L,
                          "  monitor EDID: %a, %u bytes, native %ux%u, %u supported timings\n",
                          Info.Valid ? "valid" : (E ? "INVALID" : "none"),
                          (UINT32)EdidSize, Info.Native.W, Info.Native.H,
                          (UINT32)Info.Count);

    for (M = 0; M < G->Mode->MaxMode; M++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *MI = NULL;
        UINTN Sz = 0;
        UINT64 Score = 0;
        UINT32 W, H;

        if (EFI_ERROR(G->QueryMode(G, M, &Sz, &MI)) || !MI)
            continue;
        W = MI->HorizontalResolution;
        H = MI->VerticalResolution;
        if (Log && M < 64)
            *L += AsciiSPrint(Log + *L, ARCS_LOGSZ - *L, "  mode %u: %ux%u pixfmt=%u\n",
                              M, W, H, (UINT32)MI->PixelFormat);
        if (MI->PixelFormat != PixelBltOnly) {
            if (ForceW && W == *ForceW && H == *ForceH)
                Score = (1ULL << 50);                            /* explicit request */
            else if (Info.Valid && Info.HaveNative && W == Info.Native.W && H == Info.Native.H)
                Score = (1ULL << 48);                            /* monitor native */
            else if (Info.Valid && ArcsEdidHas(&Info, W, H))
                Score = (1ULL << 40) + (UINT64)W * H;            /* largest EDID-supported */
            else if (!Info.Valid && W == 1280 && H == 1024)
                Score = (1ULL << 20);                            /* no EDID: PROM target */
            else if (!Info.Valid)
                Score = (UINT64)W * H / 4096 + 1;                /* no EDID: largest */
        }
        if (Score > BestScore) {
            BestScore = Score;
            *BestMode = M;
        }
        FreePool(MI);
    }
    /* A GOP with a real monitor behind it beats one without. */
    if (Info.Valid && BestScore)
        BestScore += (1ULL << 52);
    return BestScore;
}

EFI_GRAPHICS_OUTPUT_PROTOCOL *
ArcsUefiPickGop(void)
{
    EFI_HANDLE *H = NULL;
    UINTN N = 0, i, BestIdx = 0;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Best = NULL;
    EFI_HANDLE ConOut = gST ? gST->ConsoleOutHandle : NULL;
    UINT32 BestMode = 0, FW = 0, FH = 0;
    UINT64 BestScore = 0;
    BOOLEAN Forced = ArcsForcedResolution(&FW, &FH);
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
        L += AsciiSPrint(Log + L, ARCS_LOGSZ - L, "GOP handles: %u  ArcsResolution=%a\n",
                         (UINT32)N, Forced ? getenv("ArcsResolution") : "auto");

    for (i = 0; i < N; i++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *G = NULL;
        UINT32 Mode = 0;
        UINT64 Score;

        if (EFI_ERROR(gBS->HandleProtocol(H[i], &gEfiGraphicsOutputProtocolGuid,
                                          (VOID **)&G)) || !G || !G->Mode)
            continue;
        if (Log)
            L += AsciiSPrint(Log + L, ARCS_LOGSZ - L,
                             "gop %u%a: MaxMode=%u current=%u %ux%u pixfmt=%u fb=0x%lx\n",
                             (UINT32)i, (H[i] == ConOut) ? " (firmware console)" : "",
                             G->Mode->MaxMode, G->Mode->Mode,
                             G->Mode->Info->HorizontalResolution,
                             G->Mode->Info->VerticalResolution,
                             (UINT32)G->Mode->Info->PixelFormat,
                             (UINT64)G->Mode->FrameBufferBase);
        Score = ArcsScoreGop(G, H[i], Forced ? &FW : NULL, Forced ? &FH : NULL,
                             &Mode, Log, &L);
        /* The firmware's own console display wins ties: that is the screen
         * the user is already looking at. */
        if (Score && H[i] == ConOut)
            Score += 1;
        if (Score > BestScore) {
            BestScore = Score;
            Best = G;
            BestMode = Mode;
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
