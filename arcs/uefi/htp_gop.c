#include "htp_gop.h"
#include <Library/BaseMemoryLib.h>

static VOID
GopBlankScreen(VOID *Hw, INT32 Mode)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = {0, 0, 0, 0};

    if (!B || !B->Gop)
        return;

    /* UEFI GOP has no portable DPMS equivalent. Preserve the PROM
     * blanking contract as closely as possible by blacking the display
     * when asked to blank; unblanking is satisfied by the next redraw. */
    if (Mode)
        B->Gop->Blt(B->Gop, &Pixel, EfiBltVideoFill,
                    0, 0, 0, 0, B->Width, B->Height, 0);
}

static VOID
GopColor(VOID *Hw, INT32 Color)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;

    if (!B)
        return;
    if (Color < 0)
        Color = 0;
    if (Color > 255)
        Color = 255;
    B->ColorIndex = (UINT8)Color;
}

static VOID
GopMapColor(VOID *Hw, INT32 Index, INT32 R, INT32 G, INT32 Bl)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;

    if (!B || Index < 0 || Index > 255)
        return;

    B->Palette[Index].Red      = (UINT8)R;
    B->Palette[Index].Green    = (UINT8)G;
    B->Palette[Index].Blue     = (UINT8)Bl;
    B->Palette[Index].Reserved = 0;
}

static BOOLEAN
ClipLogicalPoint(ARCS_GOP_HTP *B, INTN X, INTN Y, UINTN *GopX, UINTN *GopY)
{
    if (!B || X < 0 || Y < 0 || (UINTN)X >= B->Width || (UINTN)Y >= B->Height)
        return FALSE;

    *GopX = (UINTN)X;
    *GopY = B->Height - 1 - (UINTN)Y;
    return TRUE;
}

static VOID
GopPoint(VOID *Hw, INT32 X, INT32 Y)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    UINTN GX, GY;

    if (!B || !B->Gop || !ClipLogicalPoint(B, X, Y, &GX, &GY))
        return;

    B->Gop->Blt(B->Gop, &B->Palette[B->ColorIndex], EfiBltVideoFill,
                0, 0, GX, GY, 1, 1, 0);
}

static VOID
GopSboxfi(VOID *Hw, INT32 X1, INT32 Y1, INT32 X2, INT32 Y2)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    INTN LX = X1, RX = X2, BY = Y1, TY = Y2;
    UINTN GX, GY, W, H;

    if (!B || !B->Gop)
        return;

    if (LX > RX) { INTN T = LX; LX = RX; RX = T; }
    if (BY > TY) { INTN T = BY; BY = TY; TY = T; }

    if (RX < 0 || TY < 0 || LX >= (INTN)B->Width || BY >= (INTN)B->Height)
        return;

    if (LX < 0) LX = 0;
    if (BY < 0) BY = 0;
    if (RX >= (INTN)B->Width)  RX = (INTN)B->Width - 1;
    if (TY >= (INTN)B->Height) TY = (INTN)B->Height - 1;

    GX = (UINTN)LX;
    GY = B->Height - 1 - (UINTN)TY;
    W = (UINTN)(RX - LX + 1);
    H = (UINTN)(TY - BY + 1);

    B->Gop->Blt(B->Gop, &B->Palette[B->ColorIndex], EfiBltVideoFill,
                0, 0, GX, GY, W, H, 0);
}

static VOID
GopCmov2i(VOID *Hw, INT32 X, INT32 Y)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;

    if (!B)
        return;
    B->CposX = X;
    B->CposY = Y;
}

static VOID
DrawBitmapRun(ARCS_GOP_HTP *B, INTN X, INTN GopY, UINTN Width)
{
    INTN Start = X;
    INTN End = X + (INTN)Width - 1;

    if (GopY < 0 || GopY >= (INTN)B->Height || End < 0 || Start >= (INTN)B->Width)
        return;

    if (Start < 0) Start = 0;
    if (End >= (INTN)B->Width) End = (INTN)B->Width - 1;
    if (End < Start)
        return;

    B->Gop->Blt(B->Gop, &B->Palette[B->ColorIndex], EfiBltVideoFill,
                0, 0, (UINTN)Start, (UINTN)GopY,
                (UINTN)(End - Start + 1), 1, 0);
}

static VOID
GopDrawBitmap(VOID *Hw, struct htp_bitmap *Bitmap)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    INTN XLeft, GopY0;
    INT32 Row;

    if (!B || !B->Gop || !Bitmap || !Bitmap->buf || Bitmap->xsize <= 0 || Bitmap->ysize <= 0)
        return;

    /* Match the original standalone textport raster semantics:
     * xleft = cpos_x - xorig and raster Y begins at
     * (height - 1 - cpos_y + yorig). */
    XLeft = B->CposX - Bitmap->xorig;
    GopY0 = (INTN)B->Height - 1 - B->CposY + Bitmap->yorig;

    for (Row = 0; Row < Bitmap->ysize; Row++) {
        INT32 Col = 0;
        while (Col < Bitmap->xsize) {
            INT32 WordIndex = Row * Bitmap->sper + (Col >> 4);
            UINT16 Word = Bitmap->buf[WordIndex];
            UINT16 Mask = (UINT16)(0x8000U >> (Col & 15));

            if ((Word & Mask) == 0) {
                Col++;
                continue;
            }

            {
                INT32 RunStart = Col;
                do {
                    Col++;
                    if (Col >= Bitmap->xsize)
                        break;
                    WordIndex = Row * Bitmap->sper + (Col >> 4);
                    Word = Bitmap->buf[WordIndex];
                    Mask = (UINT16)(0x8000U >> (Col & 15));
                } while (Word & Mask);

                DrawBitmapRun(B, XLeft + RunStart, GopY0 + Row,
                              (UINTN)(Col - RunStart));
            }
        }
    }

    B->CposX += Bitmap->xmove;
    B->CposY += Bitmap->ymove;
}

static VOID
GopInit(VOID *Hw, INT32 *X, INT32 *Y)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;

    if (!B || !B->Gop || !B->Gop->Mode || !B->Gop->Mode->Info)
        return;

    B->Width  = B->Gop->Mode->Info->HorizontalResolution;
    B->Height = B->Gop->Mode->Info->VerticalResolution;

    if (X) *X = (INT32)B->Width;
    if (Y) *Y = (INT32)B->Height;
}

static VOID
GopMoveCursor(VOID *Hw, INT32 X, INT32 Y)
{
    /* The historical htp_fncs cursor hook controls the hardware pointer.
     * UEFI GOP has no cursor primitive; the input port will supply a
     * software cursor later. */
    (VOID)Hw;
    (VOID)X;
    (VOID)Y;
}

struct htp_fncs ArcsGopHtpFncs = {
    GopBlankScreen,
    GopColor,
    GopMapColor,
    GopSboxfi,
    GopPoint,
    GopCmov2i,
    GopDrawBitmap,
    0,
    GopInit,
    GopMoveCursor
};

EFI_STATUS
ArcsGopHtpAttach(
    ARCS_GOP_HTP *Backend,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    struct htp_state *Htp
    )
{
    INT32 X = 0, Y = 0;

    if (!Backend || !Gop || !Htp || !Gop->Mode || !Gop->Mode->Info)
        return EFI_INVALID_PARAMETER;

    ZeroMem(Backend, sizeof(*Backend));
    Backend->Gop = Gop;
    GopInit(Backend, &X, &Y);

    Htp->fncs = &ArcsGopHtpFncs;
    Htp->hw = Backend;
    Htp->xscreensize = X;
    Htp->yscreensize = Y;

    return EFI_SUCCESS;
}
