/*
 * ARCS resident runtime driver (DXE_RUNTIME_DRIVER).
 *
 * Stays in memory after ExitBootServices and gives the operating system an
 * IRIX-style System Parameter Block with a firmware vector.  The first entry
 * implemented is Write(): it renders into the Command Monitor window of the
 * framebuffer using the original ScrB18 textport font, exactly the path the
 * IRIX kernel's arcs_write() takes into the PROM textport.
 *
 * Runtime rules observed here: no boot services after ExitBootServices, all
 * state in this image's own .data/.bss reached RIP-relatively (the image moves
 * as one block when the OS calls SetVirtualAddressMap), the framebuffer
 * pointer is converted in the address-change handler.
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>

#include "arcs_rt.h"

/* The SGI font tables (GL Fontchar layout, as in stand_htport.h). */
struct fontinfo {
    short offset;
    char xsize;
    char ysize;
    signed char xorig;
    signed char yorig;
    short xmove;
};
#include <fonts/scrB18.h>

#define CELLW		9		/* ScrB18 xmove */
#define CELLH		ScrB18_ht	/* 17 */
#define PAD		4
#define MAXCOLS		200
#define MAXROWS		100

static CONST EFI_GUID mProtocolGuid = ARCS_RT_PROTOCOL_GUID;
static CONST EFI_GUID mSpbGuid = ARCS_SPB_TABLE_GUID;

static ARCS_SPB mSpb;
static ARCS_TV mTv;
static ARCS_RT_PRIVATE mPriv;
static ARCS_RT_PROTOCOL mProto;

static struct {
    UINT8 *Fb;			/* framebuffer (physical, then virtual) */
    UINT32 Pitch;		/* bytes per scanline */
    UINT32 Format;		/* EFI_GRAPHICS_PIXEL_FORMAT */
    UINT32 WinX, WinY, Cols, Rows;
    UINT32 Fg, Bg;		/* device pixel values */
    UINT32 Col, Row;
    BOOLEAN Ready;
    BOOLEAN Redraw;		/* a scroll happened during this Write() */
    BOOLEAN Deferred;		/* inside Write(): redraw once at the end */
    CHAR8 Grid[MAXROWS][MAXCOLS];
} mCon;

static UINT32
PixelValue(UINT32 Rgb)
{
    UINT32 R = (Rgb >> 16) & 0xFF, G = (Rgb >> 8) & 0xFF, B = Rgb & 0xFF;

    if (mCon.Format == PixelRedGreenBlueReserved8BitPerColor)
        return (B << 16) | (G << 8) | R;
    return (R << 16) | (G << 8) | B;	/* BlueGreenRedReserved */
}

static VOID
DrawCell(UINT32 Col, UINT32 Row, CHAR8 Ch)
{
    UINT32 X0 = mCon.WinX + PAD + Col * CELLW;
    UINT32 Y0 = mCon.WinY + PAD + Row * CELLH;
    UINT32 X, Y, R, C;

    for (Y = 0; Y < CELLH; Y++) {
        UINT32 *P = (UINT32 *)(mCon.Fb + (UINTN)(Y0 + Y) * mCon.Pitch + (UINTN)X0 * 4);

        for (X = 0; X < CELLW; X++)
            P[X] = mCon.Bg;
    }
    if (Ch <= ' ' || Ch >= 0x7f)
        return;
    {
        CONST struct fontinfo *I = &ScrB18Info[Ch - ' '];
        INT32 Baseline = (INT32)Y0 + (ScrB18_ht - ScrB18_ds - 1);

        for (R = 0; R < (UINT32)I->ysize; R++) {
            UINT16 Bits = ScrB18Data[I->offset + R];
            INT32 Py = Baseline - I->yorig - (INT32)R;

            if (Py < (INT32)Y0 || Py >= (INT32)(Y0 + CELLH))
                continue;
            for (C = 0; C < (UINT32)I->xsize; C++) {
                INT32 Px = (INT32)X0 + I->xorig + (INT32)C;

                if ((Bits & (0x8000 >> C)) && Px >= (INT32)X0 && Px < (INT32)(X0 + CELLW))
                    *(UINT32 *)(mCon.Fb + (UINTN)Py * mCon.Pitch + (UINTN)Px * 4) = mCon.Fg;
            }
        }
    }
}

static VOID
RedrawAll(VOID)
{
    UINT32 Row, Col;

    for (Row = 0; Row < mCon.Rows; Row++)
        for (Col = 0; Col < mCon.Cols; Col++)
            DrawCell(Col, Row, mCon.Grid[Row][Col]);
}

static VOID
Newline(VOID)
{
    mCon.Col = 0;
    if (mCon.Row + 1 < mCon.Rows) {
        mCon.Row++;
        return;
    }
    {
        UINT32 Row;

        for (Row = 1; Row < mCon.Rows; Row++)
            CopyMem(mCon.Grid[Row - 1], mCon.Grid[Row], MAXCOLS);
        SetMem(mCon.Grid[mCon.Rows - 1], MAXCOLS, ' ');
        /* Framebuffer memory is slow: during a Write() call redraw once at
         * the end instead of once per scrolled line. */
        if (mCon.Deferred)
            mCon.Redraw = TRUE;
        else
            RedrawAll();
    }
}

static VOID
PutChar(CHAR8 Ch)
{
    switch (Ch) {
    case '\n':
        Newline();
        return;
    case '\r':
        mCon.Col = 0;
        return;
    case '\b':
        if (mCon.Col)
            mCon.Col--;
        return;
    case '\t':
        do {
            if (mCon.Col >= mCon.Cols)
                break;
            mCon.Grid[mCon.Row][mCon.Col] = ' ';
            if (!mCon.Redraw)
                DrawCell(mCon.Col, mCon.Row, ' ');
            mCon.Col++;
        } while (mCon.Col & 7);
        return;
    default:
        break;
    }
    if (Ch < ' ' || Ch >= 0x7f)
        return;
    if (mCon.Col >= mCon.Cols)
        Newline();
    mCon.Grid[mCon.Row][mCon.Col] = Ch;
    if (!mCon.Redraw)
        DrawCell(mCon.Col, mCon.Row, Ch);
    mCon.Col++;
}

/* ARCS Write(fd, buf, n, cnt): the console service the OS kernel prints with. */
static INT32 EFIAPI
ArcsRtWrite(UINT32 Fd, VOID *Buf, UINT32 N, UINT32 *Cnt)
{
    UINT32 I;
    CONST CHAR8 *P = Buf;

    (VOID)Fd;
    if (Cnt)
        *Cnt = 0;
    if (!mCon.Ready || !Buf)
        return 13;			/* ENODEV */
    mCon.Deferred = TRUE;
    mCon.Redraw = FALSE;
    for (I = 0; I < N; I++)
        PutChar(P[I]);
    mCon.Deferred = FALSE;
    if (mCon.Redraw) {
        RedrawAll();
        mCon.Redraw = FALSE;
    }
    if (Cnt)
        *Cnt = N;
    return 0;
}

static EFI_STATUS EFIAPI
ArcsRtConfigure(ARCS_RT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
                UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Fg, UINT32 Bg)
{
    EFI_PHYSICAL_ADDRESS Base;
    UINT64 Len;
    EFI_GCD_MEMORY_SPACE_DESCRIPTOR D;
    UINT32 Row;

    (VOID)This;
    if (!Gop || !Gop->Mode || !Gop->Mode->Info)
        return EFI_INVALID_PARAMETER;
    if (Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor &&
        Gop->Mode->Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor)
        return EFI_UNSUPPORTED;
    if (X + W > Gop->Mode->Info->HorizontalResolution ||
        Y + H > Gop->Mode->Info->VerticalResolution || W < 2 * PAD + CELLW ||
        H < 2 * PAD + CELLH)
        return EFI_INVALID_PARAMETER;

    mCon.Ready = FALSE;
    mCon.Fb = (UINT8 *)(UINTN)Gop->Mode->FrameBufferBase;
    mCon.Pitch = Gop->Mode->Info->PixelsPerScanLine * 4;
    mCon.Format = Gop->Mode->Info->PixelFormat;
    mCon.WinX = X;
    mCon.WinY = Y;
    mCon.Cols = (W - 2 * PAD) / CELLW;
    mCon.Rows = (H - 2 * PAD) / CELLH;
    if (mCon.Cols > MAXCOLS)
        mCon.Cols = MAXCOLS;
    if (mCon.Rows > MAXROWS)
        mCon.Rows = MAXROWS;
    mCon.Fg = PixelValue(Fg);
    mCon.Bg = PixelValue(Bg);
    mCon.Col = mCon.Row = 0;
    for (Row = 0; Row < MAXROWS; Row++)
        SetMem(mCon.Grid[Row], MAXCOLS, ' ');

    /* The framebuffer is MMIO; make it reachable after ExitBootServices. */
    Base = Gop->Mode->FrameBufferBase & ~(EFI_PHYSICAL_ADDRESS)0xFFF;
    Len = (Gop->Mode->FrameBufferSize + (Gop->Mode->FrameBufferBase & 0xFFF) + 0xFFF) &
          ~(UINT64)0xFFF;
    if (!EFI_ERROR(gDS->GetMemorySpaceDescriptor(Base, &D)))
        (VOID)gDS->SetMemorySpaceAttributes(Base, Len, D.Attributes | EFI_MEMORY_RUNTIME);

    mPriv.FbPhys = Gop->Mode->FrameBufferBase;
    mPriv.FbSize = Gop->Mode->FrameBufferSize;
    mPriv.FbPitch = mCon.Pitch;
    mPriv.FbWidth = Gop->Mode->Info->HorizontalResolution;
    mPriv.FbHeight = Gop->Mode->Info->VerticalResolution;
    mPriv.FbPixelFormat = Gop->Mode->Info->PixelFormat;
    mPriv.WinX = X;
    mPriv.WinY = Y;
    mPriv.WinW = W;
    mPriv.WinH = H;
    mPriv.Cols = mCon.Cols;
    mPriv.Rows = mCon.Rows;

    RedrawAll();		/* clears the window to the background colour */
    mCon.Ready = TRUE;
    return EFI_SUCCESS;
}

static VOID EFIAPI
ArcsRtVirtualAddressChange(EFI_EVENT Event, VOID *Context)
{
    (VOID)Event;
    (VOID)Context;
    if (mCon.Fb)
        (VOID)EfiConvertPointer(0, (VOID **)&mCon.Fb);
}

EFI_STATUS EFIAPI
ArcsRtEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_LOADED_IMAGE_PROTOCOL *Img = NULL;
    EFI_HANDLE Handle = NULL;
    EFI_EVENT Evt = NULL;
    EFI_STATUS Status;

    (VOID)SystemTable;
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Img);
    if (EFI_ERROR(Status))
        return Status;

    mPriv.Magic = ARCS_SPBMAGIC;
    mPriv.Revision = ARCS_RT_REVISION;
    mPriv.ImageBase = (UINT64)(UINTN)Img->ImageBase;
    mPriv.ImageSize = Img->ImageSize;

    mTv.Write = (UINT64)(UINTN)ArcsRtWrite;

    mSpb.Signature = ARCS_SPBMAGIC;
    mSpb.Length = sizeof(mSpb);
    mSpb.Version = ARCS_RT_VERSION;
    mSpb.Revision = ARCS_RT_REVISION;
    mSpb.TVLength = sizeof(mTv);
    mSpb.TransferVector = (UINT64)(UINTN)&mTv;
    mSpb.PTVLength = sizeof(mPriv);
    mSpb.PrivateVector = (UINT64)(UINTN)&mPriv;

    mProto.Revision = ARCS_RT_REVISION;
    mProto.Spb = &mSpb;
    mProto.Configure = ArcsRtConfigure;

    Status = gBS->CreateEvent(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_NOTIFY,
                              ArcsRtVirtualAddressChange, NULL, &Evt);
    if (EFI_ERROR(Status))
        return Status;
    Status = gBS->InstallConfigurationTable((EFI_GUID *)&mSpbGuid, &mSpb);
    if (EFI_ERROR(Status))
        return Status;
    return gBS->InstallProtocolInterface(&Handle, (EFI_GUID *)&mProtocolGuid,
                                         EFI_NATIVE_INTERFACE, &mProto);
}
