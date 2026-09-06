#include "htp_gop.h"
#include <Library/BaseMemoryLib.h>

#include <cursor.h>

extern unsigned char vc1_ptr_bits[];
extern unsigned char vc1_ptr_ol_bits[];

static VOID GopCursorRestore(ARCS_GOP_HTP *B);
static VOID GopCursorCompose(ARCS_GOP_HTP *B);

static VOID
GopCursorRestore(ARCS_GOP_HTP *B)
{
    if (!B || !B->Gop || !B->CursorDrawn ||
        B->CursorSaveW == 0 || B->CursorSaveH == 0)
        return;

    (VOID)B->Gop->Blt(B->Gop, B->CursorSave, EfiBltBufferToVideo,
                      0, 0,
                      B->CursorSaveX, B->CursorSaveY,
                      B->CursorSaveW, B->CursorSaveH,
                      B->CursorSaveW * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    B->CursorDrawn = FALSE;
}

static VOID
GopCursorCompose(ARCS_GOP_HTP *B)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Red = {0, 0, 0xff, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL White = {0xff, 0xff, 0xff, 0};
    INTN Left;
    INTN Top;
    INTN X0;
    INTN Y0;
    INTN X1;
    INTN Y1;
    UINTN W;
    UINTN H;
    UINTN Row;
    UINTN Col;

    if (!B || !B->Gop || !B->CursorVisible ||
        B->Width == 0 || B->Height == 0)
        return;

    Left = B->CursorX - tp_ptr_x_hot;
    Top = B->CursorY - tp_ptr_y_hot;

    X0 = Left < 0 ? 0 : Left;
    Y0 = Top < 0 ? 0 : Top;
    X1 = Left + tp_ptr_width;
    Y1 = Top + tp_ptr_height;

    if (X1 > (INTN)B->Width)
        X1 = (INTN)B->Width;
    if (Y1 > (INTN)B->Height)
        Y1 = (INTN)B->Height;

    if (X1 <= X0 || Y1 <= Y0)
        return;

    W = (UINTN)(X1 - X0);
    H = (UINTN)(Y1 - Y0);

    B->CursorSaveX = (UINTN)X0;
    B->CursorSaveY = (UINTN)Y0;
    B->CursorSaveW = W;
    B->CursorSaveH = H;

    if (EFI_ERROR(B->Gop->Blt(B->Gop, B->CursorSave,
                              EfiBltVideoToBltBuffer,
                              B->CursorSaveX, B->CursorSaveY,
                              0, 0, W, H,
                              W * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL))))
        return;

    CopyMem(B->CursorComposite, B->CursorSave,
            W * H * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

    /* Original SGI standalone PROM cursor data.  vc1cursor.c supplies a
     * 16x16 body mask and outline mask, MSB first.  LIGHT and EXPRESS PROM
     * initialization use a red foreground and white secondary color. */
    for (Row = 0; Row < tp_ptr_height; ++Row) {
        UINT16 Body = ((UINT16)vc1_ptr_bits[Row * 2] << 8) |
                      (UINT16)vc1_ptr_bits[Row * 2 + 1];
        UINT16 Outline = ((UINT16)vc1_ptr_ol_bits[Row * 2] << 8) |
                         (UINT16)vc1_ptr_ol_bits[Row * 2 + 1];

        for (Col = 0; Col < tp_ptr_width; ++Col) {
            UINT16 Mask = (UINT16)(0x8000U >> Col);
            INTN PX = Left + (INTN)Col;
            INTN PY = Top + (INTN)Row;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel;

            if (PX < X0 || PX >= X1 || PY < Y0 || PY >= Y1)
                continue;

            Pixel = &B->CursorComposite[
                (UINTN)(PY - Y0) * W + (UINTN)(PX - X0)];

            if (Outline & Mask)
                *Pixel = White;
            if (Body & Mask)
                *Pixel = Red;
        }
    }

    if (!EFI_ERROR(B->Gop->Blt(B->Gop, B->CursorComposite,
                               EfiBltBufferToVideo,
                               0, 0,
                               B->CursorSaveX, B->CursorSaveY,
                               W, H,
                               W * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL))))
        B->CursorDrawn = TRUE;
}

static VOID
GopBeginFramebufferWrite(ARCS_GOP_HTP *B)
{
    /* A real SGI graphics board keeps the cursor in a separate hardware
     * plane.  Remove our software plane before touching framebuffer pixels. */
    GopCursorRestore(B);
}

static VOID
GopEndFramebufferWrite(ARCS_GOP_HTP *B)
{
    /* Re-capture the updated pixels underneath the cursor and composite the
     * emulated hardware plane back over them. */
    GopCursorCompose(B);
}

static VOID
GopBlankScreen(VOID *Hw, INT32 Mode)
{
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = {0, 0, 0, 0};

    if (!B || !B->Gop)
        return;

    if (Mode) {
        GopBeginFramebufferWrite(B);
        (VOID)B->Gop->Blt(B->Gop, &Pixel, EfiBltVideoFill,
                          0, 0, 0, 0, B->Width, B->Height, 0);
        GopEndFramebufferWrite(B);
    }
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
    if (!B || X < 0 || Y < 0 ||
        (UINTN)X >= B->Width || (UINTN)Y >= B->Height)
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

    GopBeginFramebufferWrite(B);
    (VOID)B->Gop->Blt(B->Gop, &B->Palette[B->ColorIndex], EfiBltVideoFill,
                      0, 0, GX, GY, 1, 1, 0);
    GopEndFramebufferWrite(B);
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

    if (RX < 0 || TY < 0 ||
        LX >= (INTN)B->Width || BY >= (INTN)B->Height)
        return;

    if (LX < 0) LX = 0;
    if (BY < 0) BY = 0;
    if (RX >= (INTN)B->Width) RX = (INTN)B->Width - 1;
    if (TY >= (INTN)B->Height) TY = (INTN)B->Height - 1;

    GX = (UINTN)LX;
    GY = B->Height - 1 - (UINTN)TY;
    W = (UINTN)(RX - LX + 1);
    H = (UINTN)(TY - BY + 1);

    GopBeginFramebufferWrite(B);
    (VOID)B->Gop->Blt(B->Gop, &B->Palette[B->ColorIndex], EfiBltVideoFill,
                      0, 0, GX, GY, W, H, 0);
    GopEndFramebufferWrite(B);
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

/* Original SGI textport bitmap traversal, with GOP only replacing the final
 * framebuffer store. */
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

    GopBeginFramebufferWrite(B);

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
                if ((Mask & Pattern) &&
                    X >= 0 && Y >= 0 &&
                    X < (int)B->Width && Y < (int)B->Height) {
                    (VOID)B->Gop->Blt(B->Gop,
                                      &B->Palette[B->ColorIndex],
                                      EfiBltVideoFill,
                                      0, 0, (UINTN)X, (UINTN)Y,
                                      1, 1, 0);
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

    GopEndFramebufferWrite(B);
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
    ARCS_GOP_HTP *B = (ARCS_GOP_HTP *)Hw;

    if (!B || !B->Gop)
        return;

    /* Match the original htp_fncs contract: X/Y describe the PROM cursor
     * hotspot.  SGI hardware backends only move the independent cursor plane;
     * this software backend emulates that behavior around GOP. */
    GopCursorRestore(B);
    B->CursorX = X;
    B->CursorY = Y;
    B->CursorVisible = TRUE;
    GopCursorCompose(B);
}

VOID
ArcsGopCursorRefresh(ARCS_GOP_HTP *Backend)
{
    if (!Backend)
        return;

    GopCursorRestore(Backend);
    GopCursorCompose(Backend);
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

    /* SGI GUI mouse buttons are active-low. */
    Htp->buttons = 0x07;

    return EFI_SUCCESS;
}
