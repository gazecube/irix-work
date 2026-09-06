#include <Uefi.h>
#include <Protocol/SimpleTextOut.h>

#include "input_diag.h"

static VOID
AppendChar(CHAR16 **P, CHAR16 C)
{
    **P = C;
    ++(*P);
}

static VOID
AppendText(CHAR16 **P, const CHAR16 *S)
{
    while (*S)
        AppendChar(P, *S++);
}

static VOID
AppendDecU64(CHAR16 **P, UINT64 Value)
{
    CHAR16 Tmp[24];
    UINTN N = 0;

    if (Value == 0) {
        AppendChar(P, L'0');
        return;
    }

    while (Value != 0 && N < sizeof(Tmp) / sizeof(Tmp[0])) {
        Tmp[N++] = (CHAR16)(L'0' + (Value % 10));
        Value /= 10;
    }

    while (N != 0)
        AppendChar(P, Tmp[--N]);
}

static VOID
AppendDecI32(CHAR16 **P, INT32 Value)
{
    if (Value < 0) {
        AppendChar(P, L'-');
        AppendDecU64(P, (UINT64)(-(INT64)Value));
    } else {
        AppendDecU64(P, (UINT64)Value);
    }
}

static VOID
AppendHexU64(CHAR16 **P, UINT64 Value)
{
    INTN Shift;
    BOOLEAN Started = FALSE;

    AppendText(P, L"0x");
    for (Shift = 60; Shift >= 0; Shift -= 4) {
        UINTN Digit = (UINTN)((Value >> Shift) & 0xf);
        if (!Started && Digit == 0 && Shift != 0)
            continue;
        Started = TRUE;
        AppendChar(P, (CHAR16)(Digit < 10 ? L'0' + Digit : L'a' + Digit - 10));
    }
}

static VOID
EmitLine(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out, UINTN Row, CHAR16 *Line)
{
    if (!Out || !Out->OutputString)
        return;

    if (Out->SetCursorPosition)
        (VOID)Out->SetCursorPosition(Out, 0, Row);
    (VOID)Out->OutputString(Out, Line);
}

VOID
ArcsInputDiagDraw(
    EFI_SYSTEM_TABLE *SystemTable,
    const ARCS_INPUT_DIAG *Diag
    )
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out;
    CHAR16 Line[96];
    CHAR16 *P;

    if (!SystemTable || !Diag || !SystemTable->ConOut)
        return;

    Out = SystemTable->ConOut;
    if (Out->SetAttribute)
        (VOID)Out->SetAttribute(Out, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
    if (Out->EnableCursor)
        (VOID)Out->EnableCursor(Out, FALSE);

    P = Line;
    AppendText(&P, L"ARCS UEFI POINTER DIAG  ABS=");
    AppendDecU64(&P, Diag->AbsoluteCount);
    AppendText(&P, L" SIMPLE=");
    AppendDecU64(&P, Diag->SimpleCount);
    AppendText(&P, L"                         ");
    *P = 0;
    EmitLine(Out, 0, Line);

    P = Line;
    AppendText(&P, L"ABS status=");
    AppendHexU64(&P, (UINT64)Diag->AbsoluteStatus);
    AppendText(&P, L" events=");
    AppendDecU64(&P, Diag->AbsoluteEvents);
    AppendText(&P, L" raw=");
    AppendDecU64(&P, Diag->RawAbsoluteX);
    AppendChar(&P, L',');
    AppendDecU64(&P, Diag->RawAbsoluteY);
    AppendText(&P, L"                    ");
    *P = 0;
    EmitLine(Out, 1, Line);

    P = Line;
    AppendText(&P, L"SIMPLE status=");
    AppendHexU64(&P, (UINT64)Diag->SimpleStatus);
    AppendText(&P, L" events=");
    AppendDecU64(&P, Diag->SimpleEvents);
    AppendText(&P, L" delta=");
    AppendDecI32(&P, Diag->LastSimpleDx);
    AppendChar(&P, L',');
    AppendDecI32(&P, Diag->LastSimpleDy);
    AppendText(&P, L"                    ");
    *P = 0;
    EmitLine(Out, 2, Line);

    P = Line;
    AppendText(&P, L"mapped cursor=");
    AppendDecI32(&P, Diag->CurrentX);
    AppendChar(&P, L',');
    AppendDecI32(&P, Diag->CurrentY);
    AppendText(&P, L"   ESC exits                                  ");
    *P = 0;
    EmitLine(Out, 3, Line);
}
