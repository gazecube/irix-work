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

/*
 * This is the original SGI software textport bitmap walk, taken from the
 * RAD1 framebuffer fallback and adapted only at the final pixel store.
 *
 * Original operation:
 *     *dest = lut_flag;
 *
 * UEFI operation:
 *     GOP Blt of the currently selected palette entry.
 *
 * Keep the SGI x/y origin handling, row order, shorts-per-row traversal,
 * and MSB-first 0x8000 mask semantics intact here.  GOP is only the sink.
 */
static VOID
GopDrawBitmap(VOID *Hw, struct htp_bitmap *Bitmap)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    unsigned short *BufPtr;
    unsigned short Pattern, Mask;
    int XSize, YSize, ShortsRemaining;
    int X, Y;

    if (!B || !B->Gop || !Bitmap || !Bitmap->buf ||
        Bitmap->xsize <= 0 || Bitmap->ysize <= 0)
        return;

    BufPtr = Bitmap->buf;
    Y = (int)B->Height - (B->CposY - Bitmap->yorig);
    YSize = Bitmap->ysize;

    while (YSize--) {
        X = B->CposX - Bitmap->xorig;
        XSize = Bitmap->xsize;
        ShortsRemaining = Bitmap->sper;

        while (ShortsRemaining-- && XSize) {
            Mask = 0x8000;
            Pattern = *(BufPtr++);

            while (Mask && XSize) {
                if (Mask & Pattern) {
                    if (X >= 0 && Y >= 0 &&
                        X < (int)B->Width && Y < (int)B->Height) {
                        B->Gop->Blt(B->Gop,
                                    &B->Palette[B->ColorIndex],
                                    EfiBltVideoFill,
                                    0, 0, (UINTN)X, (UINTN)Y,
                                    1, 1, 0);
                    }
                }

                Mask >>= 1;
                XSize--;
                X++;
            }
        }

        Y--;
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

    /* SGI GUI mouse buttons are active-low.  A zero-initialized htp_state
     * therefore means "all buttons held", which prevents the first real
     * UEFI pointer transition from being interpreted correctly. */
    Htp->buttons = 0x07;

    return EFI_SUCCESS;
}
