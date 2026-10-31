/*
 * Menu -> flyout transition.  See bootanim.h for the design.
 *
 * All arithmetic is 16.16 fixed point; the firmware environment has no
 * guaranteed floating point.  Rectangles are kept in GOP coordinates (origin
 * top-left); the GUI's own (y up) rectangles are converted on the way in.
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include "bootanim.h"
#include "htp_gop.h"
#include "maintenance_menu_bringup.h"

extern void drawBackground(void);

typedef EFI_GRAPHICS_OUTPUT_BLT_PIXEL PIX;

#define ONE         65536
#define NITEMS      5
#define T_TOTAL     850         /* one motion, start to finish */
#define ICON_W      50          /* a menu item's icon button; the label follows */

typedef struct { INTN X, Y, W, H; } GR;     /* GOP-coordinate rectangle */

/* ------------------------------------------------------------------ */
/* an in-memory GOP: everything the GUI draws can be sent here          */
/* ------------------------------------------------------------------ */

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL Proto;     /* must be first */
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
    PIX *Buf;
    UINTN W, H;
} SHADOW;

static EFI_STATUS EFIAPI
ShadowBlt(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,
          EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op, UINTN SX, UINTN SY, UINTN DX, UINTN DY,
          UINTN W, UINTN H, UINTN Delta)
{
    SHADOW *S = (SHADOW *)This;
    UINTN Y, X;

    if (Delta == 0)
        Delta = W * sizeof(PIX);
    switch (Op) {
    case EfiBltVideoFill:
        if (DX + W > S->W || DY + H > S->H)
            return EFI_INVALID_PARAMETER;
        for (Y = 0; Y < H; Y++)
            for (X = 0; X < W; X++)
                S->Buf[(DY + Y) * S->W + DX + X] = *Buffer;
        return EFI_SUCCESS;
    case EfiBltVideoToBltBuffer:
        if (SX + W > S->W || SY + H > S->H)
            return EFI_INVALID_PARAMETER;
        for (Y = 0; Y < H; Y++)
            CopyMem((UINT8 *)Buffer + (DY + Y) * Delta + DX * sizeof(PIX),
                    &S->Buf[(SY + Y) * S->W + SX], W * sizeof(PIX));
        return EFI_SUCCESS;
    case EfiBltBufferToVideo:
        if (DX + W > S->W || DY + H > S->H)
            return EFI_INVALID_PARAMETER;
        for (Y = 0; Y < H; Y++)
            CopyMem(&S->Buf[(DY + Y) * S->W + DX],
                    (UINT8 *)Buffer + (SY + Y) * Delta + SX * sizeof(PIX), W * sizeof(PIX));
        return EFI_SUCCESS;
    case EfiBltVideoToVideo:
        if (SX + W > S->W || SY + H > S->H || DX + W > S->W || DY + H > S->H)
            return EFI_INVALID_PARAMETER;
        for (Y = 0; Y < H; Y++)
            CopyMem(&S->Buf[(DY + Y) * S->W + DX], &S->Buf[(SY + Y) * S->W + SX],
                    W * sizeof(PIX));
        return EFI_SUCCESS;
    default:
        return EFI_UNSUPPORTED;
    }
}

/* ------------------------------------------------------------------ */
/* state                                                                */
/* ------------------------------------------------------------------ */

static SHADOW Sh;
static PIX *Full, *Back, *Final, *Scratch, *Work;
static UINTN SW, SH;                    /* screen size */
static GR Item[NITEMS], Title, Panel, Rgn;
static UINTN ClickedItem;
static UINTN Order[NITEMS];             /* the others, nearest first */
static UINTN OrderCount;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *RealGop;
static UINT64 TscPerMs;
static BOOLEAN Active;                  /* Full/Back/Final are valid */

/* ------------------------------------------------------------------ */
/* easing (x and results in 0..ONE)                                     */
/* ------------------------------------------------------------------ */

static INT32
Cube(INT32 X)
{
    return (INT32)((((INT64)X * X) >> 16) * X >> 16);
}

static INT32
EaseInOut(INT32 X)                      /* cubic ease-in-out */
{
    if (X < ONE / 2)
        return 4 * Cube(X);
    return ONE - 4 * Cube(ONE - X);
}

static INT32
EaseOut(INT32 X)                        /* cubic ease-out */
{
    return ONE - Cube(ONE - X);
}

/* A stretch of the master curve: 0 until A, 1 from B, linear between (A, B in
 * 0..ONE).  The master curve is already eased, so this adds no easing of its own. */
static INT32
Seg(INT32 U, INT32 A, INT32 B)
{
    if (U <= A)
        return 0;
    if (U >= B)
        return ONE;
    return (INT32)((((INT64)(U - A)) << 16) / (B - A));
}

static INT32
Progress(INTN T, INTN Start, INTN Dur)
{
    INT64 X = ((INT64)(T - Start) * ONE) / Dur;

    return X < 0 ? 0 : (X > ONE ? ONE : (INT32)X);
}

static INTN
Lerp(INTN A, INTN B, INT32 P)
{
    return A + (INTN)(((INT64)(B - A) * P) >> 16);
}

/* ------------------------------------------------------------------ */
/* compositing                                                          */
/* ------------------------------------------------------------------ */

static VOID
ToGop(CONST ARCS_RECT *R, GR *O)
{
    O->X = R->X1;
    O->Y = (INTN)SH - 1 - R->Y2;
    O->W = R->X2 - R->X1 + 1;
    O->H = R->Y2 - R->Y1 + 1;
}

static VOID
Blend(PIX *Dst, CONST PIX *Src, INT32 A256)
{
    Dst->Red   = (UINT8)(Dst->Red   + (((INT32)Src->Red   - Dst->Red)   * A256 >> 8));
    Dst->Green = (UINT8)(Dst->Green + (((INT32)Src->Green - Dst->Green) * A256 >> 8));
    Dst->Blue  = (UINT8)(Dst->Blue  + (((INT32)Src->Blue  - Dst->Blue)  * A256 >> 8));
}

/* A menu item cut out of Full (its pixels are those that differ from Back),
 * scaled about its own centre and drawn at (Cx,Cy) with the given alpha. */
static VOID
DrawSprite(CONST GR *S, INTN Cx, INTN Cy, INT32 Scale, INT32 A256)
{
    INTN Dw = (S->W * Scale) >> 16;
    INTN Dh = (S->H * Scale) >> 16;
    INTN X0 = Cx - Dw / 2;
    INTN Y0 = Cy - Dh / 2;
    INTN X, Y;

    if (Dw < 1 || Dh < 1 || A256 <= 0)
        return;
    for (Y = 0; Y < Dh; Y++) {
        INTN Wy = Y0 + Y - Rgn.Y;
        INTN Sy = S->Y + Y * S->H / Dh;

        if (Wy < 0 || Wy >= Rgn.H)
            continue;
        for (X = 0; X < Dw; X++) {
            INTN Wx = X0 + X - Rgn.X;
            INTN Sx = S->X + X * S->W / Dw;
            CONST PIX *P, *B;

            if (Wx < 0 || Wx >= Rgn.W)
                continue;
            P = &Full[Sy * SW + Sx];
            B = &Back[Sy * SW + Sx];
            if (P->Red == B->Red && P->Green == B->Green && P->Blue == B->Blue)
                continue;                   /* background: transparent */
            Blend(&Work[Wy * Rgn.W + Wx], P, A256);
        }
    }
}

/* One column of the raster line: a white-hot core with a cyan-to-blue glow
 * either side (visible on the light gradient and the grey panel alike).  Lit
 * (0..ONE) is the overall brightness. */
static VOID
LineColumn(INTN Wx, INTN Dist, INT32 Lit)
{
    /* B, G, R */
    static CONST PIX Beam[5] = {
        { 255, 255, 255, 0 }, { 255, 235, 140, 0 }, { 255, 190, 90, 0 },
        { 240, 140, 60, 0 }, { 230, 120, 40, 0 }
    };
    static CONST INT32 Alpha[5] = { 256, 210, 130, 70, 28 };
    INT32 A;
    INTN Y;

    if (Dist > 4)
        return;
    A = (Alpha[Dist] * Lit) >> 16;
    if (A <= 0 || Wx < 0 || Wx >= Rgn.W)
        return;
    for (Y = 0; Y < Panel.H; Y++) {
        INTN Wy = Panel.Y + Y - Rgn.Y;

        if (Wy < 0 || Wy >= Rgn.H)
            continue;
        Blend(&Work[Wy * Rgn.W + Wx], &Beam[Dist], A);
    }
}

/* The flyout panel (from Final), swept out from its left edge by a vertical
 * raster line: everything behind the line is the finished panel, full
 * brightness and unscaled; the line flashes in, sweeps, and goes out.  The
 * panel itself does not fade. */
static VOID
DrawPanel(INT32 Sweep, INT32 Lit)
{
    INTN Dw = (Panel.W * Sweep) >> 16;
    INTN X, Y;

    for (X = 0; X < Dw; X++) {
        INTN Wx = Panel.X + X - Rgn.X;

        if (Wx < 0 || Wx >= Rgn.W)
            continue;
        for (Y = 0; Y < Panel.H; Y++) {
            INTN Wy = Panel.Y + Y - Rgn.Y;

            if (Wy < 0 || Wy >= Rgn.H)
                continue;
            Work[Wy * Rgn.W + Wx] = Final[(Panel.Y + Y) * SW + Panel.X + X];
        }
    }
    if (Lit > 0 && Dw < Panel.W) {
        INTN D;

        for (D = -4; D <= 4; D++)
            LineColumn(Panel.X + Dw + D - Rgn.X, D < 0 ? -D : D, Lit);
    }
}

static VOID
Compose(INTN T)
{
    /*
     * The clock runs 0..ONE (X).  The clicked option travels first -- it drops
     * down, then slides across, landing at 45% -- and the others are pulled into
     * it by the drop (Pv): they are completely gone when the title reaches its
     * final height, then it slides on alone; the panel opens only after the title is in
     * place: the raster line flashes in as it settles and sweeps out over the
     * rest of the time.
     */
    INT32 X = Progress(T, 0, T_TOTAL);
    /* the title drops down to its height first, then slides across to its place */
    INT32 Pv = EaseInOut(Seg(X, 0, ONE * 22 / 100));
    INT32 Ph = EaseInOut(Seg(X, ONE * 22 / 100, ONE * 45 / 100));
    CONST GR *C = &Item[ClickedItem];
    INTN Y, I;
    /* the clicked option's icon centre, now: this is what pulls the others in */
    INTN ClkCx = Lerp(C->X + 25, Title.X + 25, Ph);
    INTN ClkCy = Lerp(C->Y + 25, Title.Y + 25, Pv);

    for (Y = 0; Y < Rgn.H; Y++)
        CopyMem(&Work[Y * Rgn.W], &Back[(Rgn.Y + Y) * SW + Rgn.X], Rgn.W * sizeof(PIX));

    /* the raster line flashes in at the panel's edge as the title settles, then
     * sweeps out across it; the panel itself never fades */
    {
        INT32 Flash = Seg(X, ONE * 45 / 100, ONE * 52 / 100);
        INT32 Out = Seg(X, ONE * 96 / 100, ONE);

        DrawPanel(EaseOut(Seg(X, ONE * 52 / 100, ONE)),
                  (INT32)(((INT64)Flash * (ONE - Out)) >> 16));
    }

    /*
     * The others are drawn into the clicked option, nearest first.  Their
     * opacity is tied to how far they have travelled (Q), so the fade is
     * progressive: it follows the motion and reaches zero on arrival.  The
     * labels slide in behind the clicked option and are gone by 60% of the
     * way; the icons are drawn on top of it and converge into its icon,
     * dissolving as they enter it.  Both at full size, farthest first.
     */
    for (I = (INTN)OrderCount - 1; I >= 0; I--) {
        CONST GR *S = &Item[Order[I]];
        INT32 Q = Seg(Pv, (INT32)I * (ONE / 25), ONE);
        GR L = { S->X + ICON_W, S->Y, S->W - ICON_W, S->H };
        /* the labels only drift a short way and are gone by half-way: they
         * must not be seen crossing the title's own text */
        INT32 Drift = (INT32)(((INT64)Q * (ONE / 4)) >> 16);
        INTN Cx = Lerp(L.X + L.W / 2, ClkCx, Drift);
        INTN Cy = Lerp(L.Y + L.H / 2, ClkCy, Drift);
        INT32 Gone = EaseOut(Seg(Q, 0, ONE / 2));

        DrawSprite(&L, Cx, Cy, ONE, 256 - ((Gone * 256) >> 16));
    }

    /* the clicked option travels the whole way on the master curve */
    DrawSprite(C, Lerp(C->X + C->W / 2, Title.X + C->W / 2, Ph),
               Lerp(C->Y + C->H / 2, Title.Y + C->H / 2, Pv), ONE, 256);

    /* the icons go inside it */
    for (I = (INTN)OrderCount - 1; I >= 0; I--) {
        CONST GR *S = &Item[Order[I]];
        INT32 Q = Seg(Pv, (INT32)I * (ONE / 25), ONE);
        GR Ic = { S->X, S->Y, ICON_W, S->H };
        INTN Cx = Lerp(Ic.X + ICON_W / 2, ClkCx, Q);
        INTN Cy = Lerp(Ic.Y + Ic.H / 2, ClkCy, Q);

        DrawSprite(&Ic, Cx, Cy, ONE, 256 - ((Q * 256) >> 16));
    }
}

static VOID
BlitWork(VOID)
{
    RealGop->Blt(RealGop, Work, EfiBltBufferToVideo, 0, 0, Rgn.X, Rgn.Y, Rgn.W, Rgn.H,
                 Rgn.W * sizeof(PIX));
}

static VOID
BlitImage(CONST PIX *Img)
{
    INTN Y;

    for (Y = 0; Y < Rgn.H; Y++)
        CopyMem(&Work[Y * Rgn.W], &Img[(Rgn.Y + Y) * SW + Rgn.X], Rgn.W * sizeof(PIX));
    BlitWork();
}

static BOOLEAN
KeyPending(EFI_SYSTEM_TABLE *St)
{
    EFI_INPUT_KEY Key;

    return St && St->ConIn && !EFI_ERROR(St->ConIn->ReadKeyStroke(St->ConIn, &Key));
}

/* Run the timeline forwards or backwards by the clock. */
static VOID
Play(BOOLEAN Forward, EFI_SYSTEM_TABLE *St)
{
    UINT64 T0 = AsmReadTsc();
    BOOLEAN FirstDone = FALSE;

    for (;;) {
        INTN Ms = (INTN)((AsmReadTsc() - T0) / TscPerMs);

        if (Ms >= T_TOTAL || KeyPending(St))
            break;
        Compose(Forward ? Ms : T_TOTAL - Ms);
        BlitWork();
        if (!FirstDone) {
            FirstDone = TRUE;
            /* a firmware whose Blt is this slow would stutter: just finish */
            if ((AsmReadTsc() - T0) / TscPerMs > 250)
                break;
        }
    }
    BlitImage(Forward ? Final : Full);
}

/* ------------------------------------------------------------------ */
/* buffers                                                              */
/* ------------------------------------------------------------------ */

VOID
ArcsAnimEnd(VOID)
{
    if (Full)    FreePool(Full);
    if (Back)    FreePool(Back);
    if (Final)   FreePool(Final);
    if (Scratch) FreePool(Scratch);
    if (Work)    FreePool(Work);
    Full = Back = Final = Scratch = Work = NULL;
    Active = FALSE;
}

static BOOLEAN
Prepare(ARCS_GOP_HTP *B)
{
    UINTN Bytes;
    UINT64 A, C;

    SW = B->Width;
    SH = B->Height;
    if (SW == 0 || SH == 0 || B->Gop == NULL || B->Gop->Mode == NULL ||
        B->Gop->Mode->Info == NULL)
        return FALSE;
    Bytes = SW * SH * sizeof(PIX);
    Full = AllocatePool(Bytes);
    Back = AllocatePool(Bytes);
    Final = AllocatePool(Bytes);
    Scratch = AllocatePool(Bytes);
    if (!Full || !Back || !Final || !Scratch) {
        ArcsAnimEnd();
        return FALSE;
    }
    if (TscPerMs == 0) {
        A = AsmReadTsc();
        gBS->Stall(20000);
        C = AsmReadTsc();
        TscPerMs = (C - A) / 20;
    }
    if (TscPerMs < 1000) {                  /* no usable clock */
        ArcsAnimEnd();
        return FALSE;
    }

    ZeroMem(&Sh, sizeof(Sh));
    Sh.Proto.Blt = ShadowBlt;
    Sh.Info = *B->Gop->Mode->Info;
    Sh.Mode.MaxMode = 1;
    Sh.Mode.Mode = 0;
    Sh.Mode.Info = &Sh.Info;
    Sh.Mode.SizeOfInfo = sizeof(Sh.Info);
    Sh.Proto.Mode = &Sh.Mode;
    Sh.W = SW;
    Sh.H = SH;
    return TRUE;
}

static VOID
SetRegion(VOID)
{
    INTN X1 = Title.X, Y1 = Title.Y, X2 = Title.X + Title.W, Y2 = Title.Y + Title.H;
    UINTN I;

#define UNION(R) do { \
        if ((R).X < X1) X1 = (R).X; \
        if ((R).Y < Y1) Y1 = (R).Y; \
        if ((R).X + (R).W > X2) X2 = (R).X + (R).W; \
        if ((R).Y + (R).H > Y2) Y2 = (R).Y + (R).H; \
    } while (0)
    UNION(Panel);
    for (I = 0; I < NITEMS; I++)
        UNION(Item[I]);
#undef UNION
    X1 -= 8; Y1 -= 8; X2 += 8; Y2 += 8;
    if (X1 < 0) X1 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X2 > (INTN)SW) X2 = (INTN)SW;
    if (Y2 > (INTN)SH) Y2 = (INTN)SH;
    Rgn.X = X1; Rgn.Y = Y1; Rgn.W = X2 - X1; Rgn.H = Y2 - Y1;
}

/* ------------------------------------------------------------------ */
/* public                                                               */
/* ------------------------------------------------------------------ */

BOOLEAN
ArcsAnimOpen(ARCS_ANIM_FN BuildFlyout, ARCS_ANIM_GEOM_FN GetGeom, UINTN Clicked,
             EFI_SYSTEM_TABLE *SystemTable)
{
    ARCS_GOP_HTP *B = ArcsGopBackend();
    ARCS_ANIM_GEOM G;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Old;
    BOOLEAN Vis;
    UINTN I, N;
    ARCS_RECT R[NITEMS];

    if (B == NULL || B->Gop == NULL || Clicked >= NITEMS)
        return FALSE;
    for (I = 0; I < NITEMS; I++)
        if (!ArcsMenuItemRect((int)I, &R[I].X1, &R[I].Y1, &R[I].X2, &R[I].Y2))
            return FALSE;
    if (!Prepare(B))
        return FALSE;

    RealGop = B->Gop;
    ClickedItem = Clicked;
    for (I = 0; I < NITEMS; I++)
        ToGop(&R[I], &Item[I]);

    /* Full: the screen as it is now (cursor taken off it first). */
    Old = ArcsGopRedirectBegin(B, &Sh.Proto, &Vis);
    RealGop->Blt(RealGop, Full, EfiBltVideoToBltBuffer, 0, 0, 0, 0, SW, SH, 0);

    /* Back: the background alone.  Final: the finished flyout on it.  Both are
     * drawn by the normal GUI code, into memory. */
    Sh.Buf = Back;
    drawBackground();
    ArcsMenuPark(1);
    CopyMem(Final, Back, SW * SH * sizeof(PIX));
    Sh.Buf = Final;
    BuildFlyout();
    ArcsGopRedirectEnd(B, Old, Vis, FALSE);

    GetGeom(&G);
    ToGop(&G.Title, &Title);
    ToGop(&G.Panel, &Panel);
    SetRegion();
    Work = AllocatePool(Rgn.W * Rgn.H * sizeof(PIX));
    if (Work == NULL) {
        /* the flyout exists (drawn in memory only): show it, no animation */
        RealGop->Blt(RealGop, Final, EfiBltBufferToVideo, 0, 0, 0, 0, SW, SH, 0);
        ArcsGopCursorRefresh(B);
        ArcsAnimEnd();
        return TRUE;
    }

    /* the other options, nearest to the clicked one first */
    OrderCount = 0;
    for (N = 1; N < NITEMS; N++) {
        if (Clicked + N < NITEMS)
            Order[OrderCount++] = Clicked + N;
        if (Clicked >= N)
            Order[OrderCount++] = Clicked - N;
    }

    Active = TRUE;
    Play(TRUE, SystemTable);
    ArcsGopCursorRefresh(B);
    return TRUE;
}

VOID
ArcsAnimClose(ARCS_ANIM_FN DestroyFlyout, EFI_SYSTEM_TABLE *SystemTable)
{
    ARCS_GOP_HTP *B = ArcsGopBackend();
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Old;
    BOOLEAN Vis;

    if (!Active || B == NULL || Work == NULL) {
        DestroyFlyout();
        return;
    }
    RealGop = B->Gop;
    /* Final: the flyout as it is now (the list may have scrolled). */
    Old = ArcsGopRedirectBegin(B, &Sh.Proto, &Vis);
    RealGop->Blt(RealGop, Final, EfiBltVideoToBltBuffer, 0, 0, 0, 0, SW, SH, 0);
    Sh.Buf = Scratch;
    DestroyFlyout();
    ArcsGopRedirectEnd(B, Old, Vis, FALSE);

    Play(FALSE, SystemTable);
    ArcsGopCursorRefresh(B);
}
