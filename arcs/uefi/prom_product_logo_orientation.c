#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

extern unsigned char clogo_data[];
extern int clogo_w;
extern int clogo_h;
extern const char ArcsProductLogoBase64[];
extern const unsigned int ArcsProductLogoDecodedSize;

static int
Base64Value(char C)
{
    if (C >= 'A' && C <= 'Z')
        return C - 'A';
    if (C >= 'a' && C <= 'z')
        return C - 'a' + 26;
    if (C >= '0' && C <= '9')
        return C - '0' + 52;
    if (C == '+')
        return 62;
    if (C == '/')
        return 63;
    return -1;
}

static int
NextBase64Value(const char **Input)
{
    const char *S = *Input;
    int Value;

    while (*S == ' ' || *S == '\t' || *S == '\r' || *S == '\n')
        ++S;

    if (!*S)
        return -1;

    Value = Base64Value(*S++);
    *Input = S;
    return Value;
}

static BOOLEAN
DecodeProductLogo(void)
{
    const char *S = ArcsProductLogoBase64;
    UINTN Out = 0;

    while (*S) {
        int A, B, C, D;

        while (*S == ' ' || *S == '\t' || *S == '\r' || *S == '\n')
            ++S;
        if (!*S)
            break;

        A = NextBase64Value(&S);
        B = NextBase64Value(&S);
        C = NextBase64Value(&S);
        D = NextBase64Value(&S);
        if (A < 0 || B < 0 || C < 0 || D < 0)
            return FALSE;

        if (Out + 3 > (UINTN)ArcsProductLogoDecodedSize)
            return FALSE;

        clogo_data[Out++] = (unsigned char)((A << 2) | (B >> 4));
        clogo_data[Out++] = (unsigned char)((B << 4) | (C >> 2));
        clogo_data[Out++] = (unsigned char)((C << 6) | D);
    }

    return Out == (UINTN)ArcsProductLogoDecodedSize;
}

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

    if (!DecodeProductLogo())
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

        if (Px != (UINTN)clogo_w) {
            FreePool(RowOffsets);
            return;
        }
    }

    RowOffsets[clogo_h] = (UINTN)(P - clogo_data);
    TotalBytes = RowOffsets[clogo_h];
    if (TotalBytes != (UINTN)ArcsProductLogoDecodedSize) {
        FreePool(RowOffsets);
        return;
    }

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
