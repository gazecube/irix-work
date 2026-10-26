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
#define PAD		2		/* XBORDER */
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
    BOOLEAN Visible;		/* the window has been shown by the OS */
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
    if (!mCon.Visible) {		/* dialog phase: kernel text is not shown */
        if (Cnt)
            *Cnt = N;
        return 0;
    }
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


/*
 * The Command Monitor view, drawn exactly as IRIX's libsk/graphics/htport.c
 * does: drawShadedBackground() ramp over the whole screen, then the textport
 * outline of txShdOutline() around the 80x40 text area.  Colours are the
 * original grays and ramp ends (stand_htport.h, style.h, htport.c txMap()).
 * y is screen y (down); the original's GL y (up) is mirrored.
 */
#define LRT_WIDTH	16
#define IN_WIDTH	3
#define OUT_WIDTH	4
#define LRT_BDR		(LRT_WIDTH - OUT_WIDTH)
#define XBORDER		2
#define YBORDER		2
#define TP_XBORDER	96
#define TP_YBORDER	90
#define G_WHITE		0xF0F0F0
#define G_VLIGHT	0xD5D5D5	/* GRAY213 */
#define G_LIGHT		0xAAAAAA	/* GRAY170 */
#define G_MEDIUM	0x808080	/* GRAY128 */
#define G_DARK		0x555555	/* GRAY85  */
#define G_VDARK		0x2A2A2A	/* GRAY42  */
#define G_BLACK		0x000000

static VOID
Box(INT32 X1, INT32 Y1, INT32 X2, INT32 Y2, UINT32 Rgb)
{
    UINT32 V = PixelValue(Rgb);
    INT32 X, Y;

    if (X1 > X2) { X = X1; X1 = X2; X2 = X; }
    if (Y1 > Y2) { Y = Y1; Y1 = Y2; Y2 = Y; }
    if (X1 < 0) X1 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X2 >= (INT32)mPriv.FbWidth) X2 = mPriv.FbWidth - 1;
    if (Y2 >= (INT32)mPriv.FbHeight) Y2 = mPriv.FbHeight - 1;
    for (Y = Y1; Y <= Y2; Y++) {
        UINT32 *P = (UINT32 *)(mCon.Fb + (UINTN)Y * mCon.Pitch);

        for (X = X1; X <= X2; X++)
            P[X] = V;
    }
}

static VOID
ShadedBackground(VOID)
{
    UINT32 H = mPriv.FbHeight, Y;
    /* RAMPSIZE ramp entries from BOTTOMCOLOR (GL y = 0) to TOPCOLOR */
    INT32 Inc = (INT32)(((UINT32)160 << 16) / (H - 1));
    INT32 R0 = 0x5f << 16, G0 = 0x5f << 16, B0 = 0x8f << 16;
    INT32 IR = ((0xb0 - 0x5f) << 16) / 160;
    INT32 IG = ((0xd2 - 0x5f) << 16) / 160;
    INT32 IB = ((0xff - 0x8f) << 16) / 160;

    for (Y = 0; Y < H; Y++) {
        UINT32 Rgb;

        /* GL row y has index (y * 160/(H-1)); screen row Y is GL y = H-1-Y */
        {
            INT32 Idx = (INT32)(((UINT64)(H - 1 - Y) * (UINT64)Inc) >> 16);

            Rgb = (UINT32)(((R0 + IR * Idx) >> 16) & 0xFF) << 16 |
                  (UINT32)(((G0 + IG * Idx) >> 16) & 0xFF) << 8 |
                  (UINT32)(((B0 + IB * Idx) >> 16) & 0xFF);
        }
        Box(0, Y, mPriv.FbWidth - 1, Y, Rgb);
    }
}

/* txShdOutline() with the done button off: x1,x2 are the outline's left and
 * right, yt (GL y2) and yb (GL y1) its top and bottom, in screen y. */
static VOID
ShdOutline(INT32 x1, INT32 yt, INT32 x2, INT32 yb)
{
    INT32 bottomy = yb + LRT_BDR + 1;	/* GL: y1 - LRT_BDR - 1 */
    INT32 i;

    /* thick gray border: right, left, top, bottom */
    Box(x2 + IN_WIDTH, yt - IN_WIDTH, x2 + LRT_BDR - 1, yb + IN_WIDTH, G_VLIGHT);
    Box(x1 - LRT_BDR + 1, yt - IN_WIDTH, x1 - IN_WIDTH, yb + IN_WIDTH, G_VLIGHT);
    Box(x1 - LRT_BDR + 1, yt - (LRT_BDR - 1), x2 + LRT_BDR - 1, yt - IN_WIDTH, G_VLIGHT);
    Box(x1 - LRT_BDR + 1, yb + IN_WIDTH, x2 + LRT_BDR - 1, bottomy, G_VLIGHT);

    {
        CONST UINT32 BR[4] = { G_LIGHT, G_MEDIUM, G_DARK, G_BLACK };
        CONST UINT32 TL[4] = { G_WHITE, G_WHITE, G_WHITE, G_BLACK };

        for (i = 0; i < OUT_WIDTH; i++) {	/* bottom and right */
            Box(x1 - LRT_BDR - i, bottomy + i, x2 + LRT_BDR + i, bottomy + i, BR[i]);
            Box(x2 + LRT_BDR + i, bottomy + i, x2 + LRT_BDR + i, yt - LRT_BDR - i, BR[i]);
        }
        for (i = 0; i < OUT_WIDTH; i++) {	/* top and left */
            Box(x1 - LRT_BDR - i, yt - LRT_BDR - i, x2 + LRT_BDR + i, yt - LRT_BDR - i, TL[i]);
            Box(x1 - LRT_BDR - i, bottomy + i, x1 - LRT_BDR - i, yt - LRT_BDR - i, TL[i]);
        }
    }
    {
        CONST UINT32 BR[3] = { G_DARK, G_VLIGHT, G_WHITE };
        CONST UINT32 TL[3] = { G_VDARK, G_DARK, G_MEDIUM };

        for (i = 0; i < IN_WIDTH; i++) {	/* inner: bottom and right */
            Box(x1 - i, yb + i, x2 + i, yb + i, BR[i]);
            Box(x2 + i, yb + i, x2 + i, yt - i, BR[i]);
        }
        for (i = 0; i < IN_WIDTH; i++) {	/* inner: top and left */
            Box(x1 - i, yt - i, x2 + i, yt - i, TL[i]);
            Box(x1 - i, yb + i, x1 - i, yt - i, TL[i]);
        }
    }
}

/* Switch from the PROM dialog to the Command Monitor view. */
static VOID EFIAPI
ArcsRtShowWindow(VOID)
{
    UINT32 Row;
    INT32 Llx, Top, W, H;

    if (!mCon.Ready)
        return;
    ShadedBackground();
    Llx = mPriv.WinX - XBORDER;		/* original llx; text at llx + XBORDER */
    Top = mPriv.WinY - YBORDER;
    W = (INT32)(mCon.Cols * CELLW) + XBORDER * 2;
    H = (INT32)(mCon.Rows * CELLH) + YBORDER * 2;
    ShdOutline(Llx - 2, Top - 2, Llx + W + 1, Top + H + 1);
    /* page colour, including the 1 pixel ring the original paints inside the outline */
    Box(Llx - 1, Top - 1, Llx + W, Top + H, 0x606070);	/* TP_DF 96,96,112 */
    mCon.Col = mCon.Row = 0;
    for (Row = 0; Row < MAXROWS; Row++)
        SetMem(mCon.Grid[Row], MAXCOLS, ' ');
    mCon.Visible = TRUE;
    mCon.Redraw = FALSE;
    RedrawAll();
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
    /* The published window is the text area; the outline surrounds it. */
    mPriv.WinX = X + PAD;
    mPriv.WinY = Y + PAD;
    mPriv.WinW = W - 2 * PAD;
    mPriv.WinH = H - 2 * PAD;
    mPriv.Cols = mCon.Cols;
    mPriv.Rows = mCon.Rows;

    mCon.Visible = FALSE;	/* the PROM dialog stays up until ShowFn */
    mPriv.ShowFn = (UINT64)(UINTN)ArcsRtShowWindow;
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
