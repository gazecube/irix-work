#include <Uefi.h>
#include <Protocol/SimpleTextOut.h>

#include "input_diag.h"

static VOID AppendChar(CHAR16 **P, CHAR16 C) { **P = C; ++(*P); }
static VOID AppendText(CHAR16 **P, const CHAR16 *S) { while (*S) AppendChar(P, *S++); }

static VOID
AppendDecU64(CHAR16 **P, UINT64 Value)
{
    CHAR16 Tmp[24];
    UINTN N = 0;
    if (Value == 0) { AppendChar(P, L'0'); return; }
    while (Value != 0 && N < sizeof(Tmp) / sizeof(Tmp[0])) {
        Tmp[N++] = (CHAR16)(L'0' + (Value % 10));
        Value /= 10;
    }
    while (N != 0) AppendChar(P, Tmp[--N]);
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
        if (!Started && Digit == 0 && Shift != 0) continue;
        Started = TRUE;
        AppendChar(P, (CHAR16)(Digit < 10 ? L'0' + Digit : L'a' + Digit - 10));
    }
}

static VOID
AppendHexByte(CHAR16 **P, UINT8 Value)
{
    UINTN Hi = (Value >> 4) & 0xf;
    UINTN Lo = Value & 0xf;
    AppendChar(P, (CHAR16)(Hi < 10 ? L'0' + Hi : L'a' + Hi - 10));
    AppendChar(P, (CHAR16)(Lo < 10 ? L'0' + Lo : L'a' + Lo - 10));
}

static VOID
AppendPathSignature(CHAR16 **P, const UINT8 *Types, const UINT8 *SubTypes, UINTN Count)
{
    UINTN I;
    if (Count == 0) { AppendText(P, L"none"); return; }
    for (I = 0; I < Count; ++I) {
        if (I != 0) AppendChar(P, L'-');
        AppendHexByte(P, Types[I]);
        AppendChar(P, L'/');
        AppendHexByte(P, SubTypes[I]);
    }
}

static VOID
EmitLine(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out, UINTN Row, CHAR16 *Line)
{
    if (!Out || !Out->OutputString) return;
    if (Out->SetCursorPosition) (VOID)Out->SetCursorPosition(Out, 0, Row);
    (VOID)Out->OutputString(Out, Line);
}

VOID
ArcsInputDiagDraw(EFI_SYSTEM_TABLE *SystemTable, const ARCS_INPUT_DIAG *Diag)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out;
    CHAR16 Line[144];
    CHAR16 *P;

    if (!SystemTable || !Diag || !SystemTable->ConOut) return;
    Out = SystemTable->ConOut;
    if (Out->SetAttribute) (VOID)Out->SetAttribute(Out, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
    if (Out->EnableCursor) (VOID)Out->EnableCursor(Out, FALSE);

    P = Line;
    AppendText(&P, L"ARCS UEFI POINTER DIAG  active ABS=");
    AppendDecU64(&P, Diag->AbsoluteCount);
    AppendText(&P, L" SIMPLE=");
    AppendDecU64(&P, Diag->SimpleCount);
    AppendText(&P, L"   enum ABS=");
    AppendDecU64(&P, Diag->EnumeratedAbsoluteCount);
    AppendText(&P, L" SIMPLE=");
    AppendDecU64(&P, Diag->EnumeratedSimpleCount);
    AppendText(&P, L"        ");
    *P = 0;
    EmitLine(Out, 0, Line);

    P = Line;
    AppendText(&P, L"ConsoleIn: ABS=");
    AppendText(&P, Diag->ConsoleAbsolute ? L"yes" : L"no");
    AppendText(&P, L" SIMPLE=");
    AppendText(&P, Diag->ConsoleSimple ? L"yes" : L"no");
    AppendText(&P, L"   selected path follows                         ");
    *P = 0;
    EmitLine(Out, 1, Line);

    P = Line;
    AppendText(&P, L"ABS state=");
    AppendHexU64(&P, (UINT64)Diag->AbsoluteStatus);
    AppendText(&P, L" ev="); AppendDecU64(&P, Diag->AbsoluteEvents);
    AppendText(&P, L" wait="); AppendHexU64(&P, (UINT64)Diag->AbsoluteWaitStatus);
    AppendText(&P, L" sig="); AppendDecU64(&P, Diag->AbsoluteWaitSignals);
    AppendText(&P, L" path=");
    AppendPathSignature(&P, Diag->AbsolutePathType, Diag->AbsolutePathSubType, Diag->AbsolutePathCount);
    AppendText(&P, L"          ");
    *P = 0;
    EmitLine(Out, 2, Line);

    P = Line;
    AppendText(&P, L"ABS raw="); AppendDecU64(&P, Diag->RawAbsoluteX);
    AppendChar(&P, L','); AppendDecU64(&P, Diag->RawAbsoluteY);
    AppendText(&P, L"                                                   ");
    *P = 0;
    EmitLine(Out, 3, Line);

    P = Line;
    AppendText(&P, L"SIMPLE state=");
    AppendHexU64(&P, (UINT64)Diag->SimpleStatus);
    AppendText(&P, L" ev="); AppendDecU64(&P, Diag->SimpleEvents);
    AppendText(&P, L" wait="); AppendHexU64(&P, (UINT64)Diag->SimpleWaitStatus);
    AppendText(&P, L" sig="); AppendDecU64(&P, Diag->SimpleWaitSignals);
    AppendText(&P, L" path=");
    AppendPathSignature(&P, Diag->SimplePathType, Diag->SimplePathSubType, Diag->SimplePathCount);
    AppendText(&P, L"      ");
    *P = 0;
    EmitLine(Out, 4, Line);

    P = Line;
    AppendText(&P, L"SIMPLE delta="); AppendDecI32(&P, Diag->LastSimpleDx);
    AppendChar(&P, L','); AppendDecI32(&P, Diag->LastSimpleDy);
    AppendText(&P, L" mapped="); AppendDecI32(&P, Diag->CurrentX);
    AppendChar(&P, L','); AppendDecI32(&P, Diag->CurrentY);
    AppendText(&P, L"   ESC exits                              ");
    *P = 0;
    EmitLine(Out, 5, Line);
}
