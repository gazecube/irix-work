#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

extern unsigned char clogo_data[];
extern int clogo_w;
extern int clogo_h;

/*
 * Original SGI drawCalligraphy() advances logical Y upward while walking
 * clogo_data forward.  Our SVG rasterizer emits conventional top-to-bottom
 * rows, so reverse the encoded row order once at the X64 product boundary.
 *
 * Do not change the pristine renderer: original SGI product headers already
 * contain their spans in the orientation drawCalligraphy() expects.
 */
void
ArcsPrepareProductLogo(void)
{
    static BOOLEAN Prepared;
    UINTN *RowOffsets;
    unsigned char *Scratch;
    unsigned char *P;
    UINTN TotalBytes;
    int Y;

    if (Prepared || clogo_w <= 0 || clogo_h <= 0)
        return;

    RowOffsets = AllocatePool(((UINTN)clogo_h + 1) * sizeof(*RowOffsets));
    if (!RowOffsets)
        return;

    P = clogo_data;
    for (Y = 0; Y < clogo_h; ++Y) {
        UINTN Px = 0;

        RowOffsets[Y] = (UINTN)(P - clogo_data);
        while (Px < (UINTN)clogo_w)
            Px += *P++;

        /* A malformed span stream must never be allowed to walk arbitrarily
         * through firmware memory. */
        if (Px != (UINTN)clogo_w) {
            FreePool(RowOffsets);
            return;
        }
    }

    RowOffsets[clogo_h] = (UINTN)(P - clogo_data);
    TotalBytes = RowOffsets[clogo_h];
    Scratch = AllocatePool(TotalBytes);
    if (!Scratch) {
        FreePool(RowOffsets);
        return;
    }

    P = Scratch;
    for (Y = clogo_h - 1; Y >= 0; --Y) {
        UINTN Start = RowOffsets[Y];
        UINTN Bytes = RowOffsets[Y + 1] - Start;

        CopyMem(P, clogo_data + Start, Bytes);
        P += Bytes;
    }

    CopyMem(clogo_data, Scratch, TotalBytes);
    Prepared = TRUE;

    FreePool(Scratch);
    FreePool(RowOffsets);
}
