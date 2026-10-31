/*
 * ARCS boot manager: entry discovery and the graphical picker.
 *
 * The picker is built from the PROM's own GUI objects (createCanvas() for the
 * raised popout, createButton() for the tiles, dialogIntBoarder() for the icon
 * wells, putctext()/color()/sboxfi() for drawing) so it looks and behaves like
 * the rest of the PROM; mouse hover/click is serviced by the original
 * guiCheckMouse() machinery.
 *
 * Entries come from boot.conf, from scanning the ESPs for EFI-stub Linux
 * kernels (labelled from the kernel's own version string) and from other EFI
 * loaders found under \EFI\*.  See bootmenu.h.
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#include <stand_htport.h>
#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "bootmenu.h"
#include "input_bringup.h"

extern char *getenv(const char *Name);
extern int setenv(const char *Name, const char *Value);
extern void drawBackground(void);
extern void dialogIntBoarder(int x1, int y1, int x2, int y2, int leftshade);

extern VOID ArcsUefiWriteFile(CONST CHAR16 *Path, CONST CHAR8 *Data, UINTN Size);
extern EFI_HANDLE ArcsUefiOwnVolume(VOID);
extern VOID ArcsUefiConnectAll(VOID);
extern EFI_STATUS ArcsUefiStartImageAt(EFI_HANDLE Volume, CHAR16 *Path,
                                       const char *Options, BOOLEAN Kernel);


#define MAX_ENTRIES   16
#define TITLE_MAX     48
#define PATH_MAX_W    96
#define OPTS_MAX      320

enum { KIND_KERNEL, KIND_CHAIN };
enum { ICON_LINUX, ICON_CHAIN, ICON_WINDOWS, ICON_DISK };

typedef struct {
    CHAR8   Title[TITLE_MAX];
    CHAR16  Path[PATH_MAX_W];
    CHAR8   Options[OPTS_MAX];
    UINT8   Kind;
    UINT8   Icon;
    BOOLEAN Auto;        /* discovered, not from boot.conf */
    EFI_HANDLE Volume;   /* NULL = the PROM's own ESP */
} BOOT_ENTRY;

static BOOT_ENTRY Entries[MAX_ENTRIES];
static UINTN EntryCount;
static INTN DefaultIndex;
static UINTN TimeoutSec = 5;
static CHAR8 DefaultTitle[TITLE_MAX];

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static BOOLEAN
SameVolume(EFI_HANDLE A, EFI_HANDLE B)
{
    EFI_HANDLE Own = ArcsUefiOwnVolume();

    if (A == NULL) A = Own;
    if (B == NULL) B = Own;
    return A == B;
}

static VOID
PathToWide(CHAR16 *Dst, UINTN Max, CONST CHAR8 *Src)
{
    UINTN I = 0;

    while (Src[I] && I + 1 < Max) {
        Dst[I] = (Src[I] == '/') ? L'\\' : (CHAR16)(UINT8)Src[I];
        I++;
    }
    Dst[I] = 0;
}

static EFI_FILE_PROTOCOL *
OpenRoot(EFI_HANDLE Volume)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;

    if (Volume == NULL)
        Volume = ArcsUefiOwnVolume();
    if (Volume == NULL ||
        EFI_ERROR(gBS->HandleProtocol(Volume, &gEfiSimpleFileSystemProtocolGuid,
                                      (VOID **)&Fs)) || Fs == NULL ||
        EFI_ERROR(Fs->OpenVolume(Fs, &Root)))
        return NULL;
    return Root;
}

static BOOLEAN
FileExists(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path)
{
    EFI_FILE_PROTOCOL *F = NULL;

    if (EFI_ERROR(Root->Open(Root, &F, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0)) || F == NULL)
        return FALSE;
    F->Close(F);
    return TRUE;
}

/* Read up to *Size bytes (NUL-terminated).  Caller frees. */
static CHAR8 *
ReadFilePrefix(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, UINTN *Size)
{
    EFI_FILE_PROTOCOL *F = NULL;
    CHAR8 *Buf;
    UINTN Want = *Size;

    if (EFI_ERROR(Root->Open(Root, &F, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0)) || F == NULL)
        return NULL;
    Buf = AllocateZeroPool(Want + 1);
    if (Buf == NULL || EFI_ERROR(F->Read(F, &Want, Buf))) {
        if (Buf) FreePool(Buf);
        F->Close(F);
        return NULL;
    }
    F->Close(F);
    *Size = Want;
    return Buf;
}

static BOOT_ENTRY *
NewEntry(VOID)
{
    BOOT_ENTRY *E;

    if (EntryCount >= MAX_ENTRIES)
        return NULL;
    E = &Entries[EntryCount++];
    ZeroMem(E, sizeof(*E));
    return E;
}

static BOOLEAN
HaveKernel(EFI_HANDLE Volume, CONST CHAR16 *Path)
{
    UINTN I;

    for (I = 0; I < EntryCount; I++)
        if (Entries[I].Kind == KIND_KERNEL && SameVolume(Entries[I].Volume, Volume) &&
            StrCmp(Entries[I].Path, Path) == 0)
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* boot.conf                                                           */
/* ------------------------------------------------------------------ */

static CHAR8 *
Trim(CHAR8 *S)
{
    UINTN L;

    while (*S == ' ' || *S == '\t')
        S++;
    L = AsciiStrLen(S);
    while (L && (S[L - 1] == ' ' || S[L - 1] == '\t'))
        S[--L] = 0;
    return S;
}

static VOID
ParseConf(VOID)
{
    EFI_FILE_PROTOCOL *Root = OpenRoot(NULL);
    UINTN Size = 8191, I, Start = 0;
    CHAR8 *Buf;
    BOOT_ENTRY *Cur = NULL;

    if (Root == NULL)
        return;
    Buf = ReadFilePrefix(Root, L"\\EFI\\ARCS\\boot.conf", &Size);
    Root->Close(Root);
    if (Buf == NULL)
        return;

    for (I = 0; I <= Size; I++) {
        CHAR8 *Line, *Eq, *Key, *Val;

        if (Buf[I] != '\n' && Buf[I] != '\r' && Buf[I] != 0)
            continue;
        Buf[I] = 0;
        Line = Trim(Buf + Start);
        Start = I + 1;
        if (*Line == 0 || *Line == '#')
            continue;
        Eq = AsciiStrStr(Line, "=");
        if (Eq == NULL)
            continue;
        *Eq = 0;
        Key = Trim(Line);
        Val = Trim(Eq + 1);

        if (AsciiStrCmp(Key, "timeout") == 0) {
            TimeoutSec = AsciiStrDecimalToUintn(Val);
        } else if (AsciiStrCmp(Key, "default") == 0) {
            AsciiStrnCpyS(DefaultTitle, TITLE_MAX, Val, TITLE_MAX - 1);
        } else if (AsciiStrCmp(Key, "entry") == 0) {
            Cur = NewEntry();
            if (Cur) {
                AsciiStrnCpyS(Cur->Title, TITLE_MAX, Val, TITLE_MAX - 1);
                Cur->Icon = ICON_LINUX;
            }
        } else if (Cur) {
            if (AsciiStrCmp(Key, "kernel") == 0) {
                Cur->Kind = KIND_KERNEL;
                PathToWide(Cur->Path, PATH_MAX_W, Val);
            } else if (AsciiStrCmp(Key, "chainload") == 0) {
                Cur->Kind = KIND_CHAIN;
                Cur->Icon = ICON_CHAIN;
                PathToWide(Cur->Path, PATH_MAX_W, Val);
            } else if (AsciiStrCmp(Key, "options") == 0) {
                AsciiStrnCpyS(Cur->Options, OPTS_MAX, Val, OPTS_MAX - 1);
            } else if (AsciiStrCmp(Key, "icon") == 0) {
                if (AsciiStrCmp(Val, "linux") == 0) Cur->Icon = ICON_LINUX;
                else if (AsciiStrCmp(Val, "windows") == 0) Cur->Icon = ICON_WINDOWS;
                else if (AsciiStrCmp(Val, "chain") == 0) Cur->Icon = ICON_CHAIN;
                else Cur->Icon = ICON_DISK;
            }
        }
    }
    FreePool(Buf);

    /* drop entries that never got a path */
    for (I = 0; I < EntryCount; ) {
        if (Entries[I].Path[0] == 0) {
            CopyMem(&Entries[I], &Entries[I + 1], (EntryCount - I - 1) * sizeof(BOOT_ENTRY));
            EntryCount--;
        } else {
            I++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* discovery                                                           */
/* ------------------------------------------------------------------ */

/*
 * Is this an EFI-stub Linux kernel?  A bzImage with the EFI stub is a PE file
 * ("MZ") whose real-mode setup header, at 0x1f1, carries "HdrS" at 0x202.  The
 * version string the build embeds ("6.18.57-gentoo (user@host) #32 ...") is at
 * kernel_version (0x20e) + 0x200.
 */
static BOOLEAN
KernelVersion(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path, CHAR8 *Out, UINTN OutMax)
{
    UINTN Size = 0x10000, I;   /* the version string sits after the setup code */
    CHAR8 *B = ReadFilePrefix(Root, Path, &Size);
    BOOLEAN Ok = FALSE;

    if (B == NULL)
        return FALSE;
    if (Size > 0x210 && B[0] == 'M' && B[1] == 'Z' &&
        B[0x202] == 'H' && B[0x203] == 'd' && B[0x204] == 'r' && B[0x205] == 'S') {
        UINTN Ptr = (UINT8)B[0x20e] | ((UINTN)(UINT8)B[0x20f] << 8);

        Ok = TRUE;
        Out[0] = 0;
        if (Ptr && Ptr + 0x200 < Size) {
            CONST CHAR8 *V = B + 0x200 + Ptr;

            /* keep only the release: up to the first space */
            for (I = 0; V[I] && V[I] != ' ' && I + 1 < OutMax; I++)
                Out[I] = V[I];
            Out[I] = 0;
        }
    }
    FreePool(B);
    return Ok;
}

static VOID
AddKernel(EFI_HANDLE Volume, EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path,
          CONST CHAR16 *DirName)
{
    CHAR8 Ver[64];
    BOOT_ENTRY *E;
    CHAR8 Dir[32];
    UINTN I;

    if (HaveKernel(Volume, Path))
        return;
    if (!KernelVersion(Root, Path, Ver, sizeof(Ver)))
        return;
    E = NewEntry();
    if (E == NULL)
        return;
    E->Kind = KIND_KERNEL;
    E->Icon = ICON_LINUX;
    E->Auto = TRUE;
    E->Volume = Volume;
    StrnCpyS(E->Path, PATH_MAX_W, Path, PATH_MAX_W - 1);
    for (I = 0; DirName && DirName[I] && I + 1 < sizeof(Dir); I++)
        Dir[I] = (CHAR8)DirName[I];
    Dir[I] = 0;
    if (Ver[0])
        AsciiSPrint(E->Title, TITLE_MAX, "Linux %a", Ver);
    else
        AsciiSPrint(E->Title, TITLE_MAX, "Linux (%a)", Dir);
    /* same root as the configured kernel unless told otherwise */
    {
        CONST CHAR8 *O = getenv("OSLoadOptions");

        if (O)
            AsciiStrnCpyS(E->Options, OPTS_MAX, O, OPTS_MAX - 1);
    }
}

/* Visit every regular file in a directory (one level). */
typedef VOID (*FILE_CB)(EFI_HANDLE Vol, EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Dir, CONST CHAR16 *Name);

static VOID
ScanDir(EFI_HANDLE Vol, EFI_FILE_PROTOCOL *Root, CONST CHAR16 *DirPath, FILE_CB Cb)
{
    EFI_FILE_PROTOCOL *D = NULL;
    UINT8 Buf[512];

    if (EFI_ERROR(Root->Open(Root, &D, (CHAR16 *)DirPath, EFI_FILE_MODE_READ, 0)) || D == NULL)
        return;
    for (;;) {
        UINTN L = sizeof(Buf);
        EFI_FILE_INFO *Info = (EFI_FILE_INFO *)Buf;

        if (EFI_ERROR(D->Read(D, &L, Buf)) || L == 0)
            break;
        if (Info->Attribute & EFI_FILE_DIRECTORY)
            continue;
        Cb(Vol, Root, DirPath, Info->FileName);
    }
    D->Close(D);
}

static VOID
KernelCb(EFI_HANDLE Vol, EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Dir, CONST CHAR16 *Name)
{
    CHAR16 Path[PATH_MAX_W];
    UINTN L = StrLen(Name);
    CONST CHAR16 *Base = Dir;
    CONST CHAR16 *S;

    /* plausible kernel names only; the header check is the real test */
    if (!((L > 4 && StrCmp(Name + L - 4, L".efi") == 0) ||
          StrnCmp(Name, L"vmlinuz", 7) == 0 || StrnCmp(Name, L"bzImage", 7) == 0 ||
          StrnCmp(Name, L"kernel", 6) == 0))
        return;
    UnicodeSPrint(Path, sizeof(Path), L"%s\\%s", Dir[1] ? Dir : L"", Name);
    for (S = Dir; *S; S++)
        if (*S == L'\\')
            Base = S + 1;
    AddKernel(Vol, Root, Path, Base);
}

static CONST CHAR16 *LoaderNames[] = {
    L"shimx64.efi", L"grubx64.efi", L"systemd-bootx64.efi", L"bootmgfw.efi",
    L"refind_x64.efi", L"limine_x64.efi", L"BOOTX64.EFI", L"bootx64.efi", NULL
};

static VOID
AddLoadersIn(EFI_HANDLE Vol, EFI_FILE_PROTOCOL *Root, CONST CHAR16 *DirPath,
             CONST CHAR16 *DirName, BOOLEAN IsFallbackDir)
{
    UINTN I;
    CHAR16 Path[PATH_MAX_W];

    for (I = 0; LoaderNames[I]; I++) {
        BOOT_ENTRY *E;
        UINTN K;
        CONST CHAR8 *Kind;

        UnicodeSPrint(Path, sizeof(Path), L"%s\\%s", DirPath, LoaderNames[I]);
        if (!FileExists(Root, Path))
            continue;
        E = NewEntry();
        if (E == NULL)
            return;
        E->Kind = KIND_CHAIN;
        E->Icon = ICON_CHAIN;
        E->Auto = TRUE;
        E->Volume = Vol;
        StrnCpyS(E->Path, PATH_MAX_W, Path, PATH_MAX_W - 1);
        Kind = "";
        if (StrnCmp(LoaderNames[I], L"grub", 4) == 0 || StrnCmp(LoaderNames[I], L"shim", 4) == 0)
            Kind = "";
        for (K = 0; DirName[K] && K + 1 < TITLE_MAX; K++)
            E->Title[K] = (CHAR8)DirName[K];
        E->Title[K] = 0;
        if (IsFallbackDir)
            AsciiSPrint(E->Title, TITLE_MAX, "Boot loader");
        else if (StrCmp(LoaderNames[I], L"bootmgfw.efi") == 0) {
            AsciiSPrint(E->Title, TITLE_MAX, "Windows");
            E->Icon = ICON_WINDOWS;
        }
        (VOID)Kind;
        return;     /* one entry per directory: the first (best) loader */
    }
}

static BOOLEAN
IsBootDir(CONST CHAR16 *N)
{
    return StrCmp(N, L"BOOT") == 0 || StrCmp(N, L"Boot") == 0 || StrCmp(N, L"boot") == 0;
}

static VOID
DiscoverVolume(EFI_HANDLE Vol, BOOLEAN IsOwn)
{
    EFI_FILE_PROTOCOL *Root = OpenRoot(Vol);
    EFI_FILE_PROTOCOL *Efi = NULL;
    UINT8 Buf[512];

    if (Root == NULL)
        return;

    /* kernels: ESP root, \boot, and one level under \EFI */
    ScanDir(Vol, Root, L"\\", KernelCb);
    ScanDir(Vol, Root, L"\\boot", KernelCb);

    if (!EFI_ERROR(Root->Open(Root, &Efi, L"\\EFI", EFI_FILE_MODE_READ, 0)) && Efi) {
        for (;;) {
            UINTN L = sizeof(Buf);
            EFI_FILE_INFO *Info = (EFI_FILE_INFO *)Buf;
            CHAR16 Dir[PATH_MAX_W];

            if (EFI_ERROR(Efi->Read(Efi, &L, Buf)) || L == 0)
                break;
            if (!(Info->Attribute & EFI_FILE_DIRECTORY) || Info->FileName[0] == L'.')
                continue;
            UnicodeSPrint(Dir, sizeof(Dir), L"\\EFI\\%s", Info->FileName);
            ScanDir(Vol, Root, Dir, KernelCb);

            /* other OSes' loaders; never offer ourselves or the fallback path
             * on our own ESP (that is this PROM) */
            if (IsOwn && (StrCmp(Info->FileName, L"ARCS") == 0 ||
                          IsBootDir(Info->FileName)))
                continue;
            if (StrCmp(Info->FileName, L"ARCS") == 0)
                continue;
            if (StrCmp(Info->FileName, L"Microsoft") == 0) {
                AddLoadersIn(Vol, Root, L"\\EFI\\Microsoft\\Boot", Info->FileName, FALSE);
                continue;
            }
            AddLoadersIn(Vol, Root, Dir, Info->FileName,
                         IsBootDir(Info->FileName));
        }
        Efi->Close(Efi);
    }
    Root->Close(Root);
}

static VOID
DiscoverAll(VOID)
{
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0, I;
    EFI_HANDLE Own = ArcsUefiOwnVolume();

    DiscoverVolume(Own, TRUE);
    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                          NULL, &Count, &Handles)))
        return;
    for (I = 0; I < Count; I++)
        if (Handles[I] != Own)
            DiscoverVolume(Handles[I], FALSE);
    FreePool(Handles);
}

/* ------------------------------------------------------------------ */
/* icons: drawn with the same colour()/sboxfi() primitives as the GUI  */
/* ------------------------------------------------------------------ */

#define ICONBOX 48

static UINTN
Isqrt(UINTN N)
{
    UINTN R = 0;

    while ((R + 1) * (R + 1) <= N)
        R++;
    return R;
}

static VOID
Ellipse(int Cx, int Cy, int Rx, int Ry, int Col)
{
    int Dy;

    color(Col);
    for (Dy = -Ry; Dy <= Ry; Dy++) {
        int Hw = (int)Isqrt((UINTN)(Rx * Rx) * (UINTN)(Ry * Ry - Dy * Dy) / (UINTN)(Ry * Ry));

        sboxfi(Cx - Hw, Cy + Dy, Cx + Hw, Cy + Dy);
    }
}

static VOID
Box(int X1, int Y1, int X2, int Y2, int Col)
{
    color(Col);
    sboxfi(X1, Y1, X2, Y2);
}

static VOID
DrawGlyph(int Icon, int X, int Y)     /* X,Y = lower-left of the ICONBOX well */
{
    int Cx = X + ICONBOX / 2;
    int I;

    switch (Icon) {
    case ICON_LINUX:
        Ellipse(Cx, Y + 20, 12, 16, BLACK);          /* body */
        Ellipse(Cx, Y + 17, 7, 12, WHITE);           /* belly */
        Ellipse(Cx, Y + 33, 8, 7, BLACK);            /* head */
        Box(Cx - 5, Y + 33, Cx - 3, Y + 35, WHITE);  /* eyes */
        Box(Cx + 3, Y + 33, Cx + 5, Y + 35, WHITE);
        Box(Cx - 3, Y + 30, Cx + 3, Y + 31, YELLOW); /* beak */
        Box(Cx - 12, Y + 4, Cx - 3, Y + 6, YELLOW);  /* feet */
        Box(Cx + 3, Y + 4, Cx + 12, Y + 6, YELLOW);
        break;
    case ICON_WINDOWS:
        Box(X + 10, Y + 25, X + 22, Y + 37, RED);
        Box(X + 25, Y + 25, X + 37, Y + 37, GREEN);
        Box(X + 10, Y + 10, X + 22, Y + 22, BLUE);
        Box(X + 25, Y + 10, X + 37, Y + 22, YELLOW);
        break;
    case ICON_CHAIN:
        Box(X + 26, Y + 12, X + 40, Y + 36, VeryDarkGray);   /* the next stage */
        Box(X + 28, Y + 14, X + 38, Y + 34, White);
        Box(X + 6, Y + 22, X + 24, Y + 26, Black);          /* arrow shaft */
        for (I = 0; I < 7; I++)                             /* arrow head */
            Box(X + 18 + I, Y + 24 - (7 - I), X + 18 + I, Y + 24 + (7 - I), Black);
        break;
    default:
        Box(X + 6, Y + 12, X + 42, Y + 36, VeryDarkGray);   /* drive */
        Box(X + 8, Y + 14, X + 40, Y + 34, MediumGray);
        Box(X + 12, Y + 18, X + 36, Y + 20, Black);
        Box(X + 12, Y + 26, X + 16, Y + 28, GREEN);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* picker                                                              */
/* ------------------------------------------------------------------ */

#define TILE_W   156
#define TILE_H   88
#define GAP      8
#define MARG     (DIALOGBDW + DIALOGMARGIN)

static struct Canvas *Pop;
static struct Button *Tile[MAX_ENTRIES];
static void (*TileOrigHandler)(struct gui_obj *, int);
static INTN Clicked = -1;
static INTN Sel;

static INTN
TileIndex(struct gui_obj *o)
{
    UINTN I;

    for (I = 0; I < EntryCount; I++)
        if (guiobj(Tile[I]) == o)
            return (INTN)I;
    return -1;
}

static int WellColor[] = { lightBlue, lightGrayGreen, SlateBlue, lightGrayMagenta };

static VOID
DrawTileIcon(INTN I)
{
    struct gui_obj *g = guiobj(Tile[I]);
    int Wx = g->x1 + (TILE_W - (ICONBOX + 2 * DIALOGINTBDW)) / 2;
    int Wy = g->y1 + 26;

    color(WellColor[Entries[I].Icon]);
    sboxfi(Wx, Wy, Wx + ICONBOX + 2 * DIALOGINTBDW - 1, Wy + ICONBOX + 2 * DIALOGINTBDW - 1);
    DrawGlyph(Entries[I].Icon, Wx + DIALOGINTBDW, Wy + DIALOGINTBDW);
    dialogIntBoarder(Wx, Wy, Wx + ICONBOX + 2 * DIALOGINTBDW - 1,
                     Wy + ICONBOX + 2 * DIALOGINTBDW - 1, 0);
}

static void
TileHandler(struct gui_obj *o, int Event)
{
    TileOrigHandler(o, Event);
    if (Event == _REDRAW) {
        INTN I = TileIndex(o);

        if (I >= 0)
            DrawTileIcon(I);
    }
}

static void
TileClicked(struct Button *B, __scunsigned_t Value)
{
    (void)B;
    Clicked = (INTN)Value;
}

static VOID
FitTitle(CHAR8 *Dst, CONST CHAR8 *Src)
{
    UINTN L;

    AsciiStrnCpyS(Dst, TITLE_MAX, Src, TITLE_MAX - 1);
    L = AsciiStrLen(Dst);
    while (L > 3 && textWidth(Dst, BUTTONFONT) > TILE_W - 6) {
        Dst[--L] = 0;
        Dst[L - 1] = '.';
    }
}

static CHAR8 Fitted[MAX_ENTRIES][TITLE_MAX];

static VOID
SelectTile(INTN New)
{
    if (Sel >= 0 && Tile[Sel]) {
        stickButton(Tile[Sel], 0);
        drawObject(guiobj(Tile[Sel]));
    }
    Sel = New;
    stickButton(Tile[Sel], 1);
    drawObject(guiobj(Tile[Sel]));
}

static int StatusX1, StatusY1, StatusX2, StatusY2, StatusW;

static VOID
DrawStatus(CONST CHAR8 *Msg)
{
    color(VeryLightGray);
    sboxfi(StatusX1, StatusY1, StatusX2, StatusY2);
    color(BLACK);
    putctext(Msg, StatusX1, StatusY1 + 4, StatusW, helvR10);
}

static VOID
BuildPicker(UINTN Cols, UINTN Rows)
{
    int FontH = gfxgui.htp->fonts[ncenB18].height;
    int StatH = gfxgui.htp->fonts[helvR10].height + 10;
    int TitleH = FontH + 8;
    int W = (int)Cols * (TILE_W + GAP) - GAP + 2 * MARG;
    int H = 2 * MARG + TitleH + (int)Rows * (TILE_H + GAP) - GAP + StatH;
    UINTN I;
    int Top;

    Pop = createCanvas(W, H);
    drawObject(guiobj(Pop));

    color(BLACK);
    putctext("Select a system to start", Pop->gui.x1, Pop->gui.y2 - MARG - FontH + 4, W, ncenB18);

    Top = Pop->gui.y2 - MARG - TitleH;
    for (I = 0; I < EntryCount; I++) {
        UINTN R = I / Cols, C = I % Cols;
        int X = Pop->gui.x1 + MARG + (int)C * (TILE_W + GAP);
        int Y = Top - (int)R * (TILE_H + GAP) - TILE_H + 1;
        struct Button *B = createButton(X, Y, TILE_W, TILE_H);

        FitTitle(Fitted[I], Entries[I].Title);
        addButtonText(B, Fitted[I]);
        addButtonCallBack(B, TileClicked, (__scunsigned_t)I);
        if ((INTN)I == DefaultIndex)
            setDefaultButton(B, 0);
        TileOrigHandler = B->gui.eventHandler;
        B->gui.eventHandler = TileHandler;
        Tile[I] = B;
    }
    for (I = 0; I < EntryCount; I++)
        drawObject(guiobj(Tile[I]));

    StatusX1 = Pop->gui.x1 + MARG;
    StatusX2 = Pop->gui.x2 - MARG;
    StatusY1 = Pop->gui.y1 + MARG;
    StatusY2 = StatusY1 + StatH - 4;
    StatusW = StatusX2 - StatusX1 + 1;

    /* Snow-Leopard-style stack tail under the popout */
    {
        int Cx = (Pop->gui.x1 + Pop->gui.x2) / 2, K;

        for (K = 0; K < 14; K++) {
            int Y = Pop->gui.y1 - 1 - K;

            if (Y < 0)
                break;
            color(VeryLightGray);
            sboxfi(Cx - (14 - K), Y, Cx + (14 - K), Y);
            color(BLACK);
            sboxfi(Cx - (14 - K), Y, Cx - (14 - K), Y);
            sboxfi(Cx + (14 - K), Y, Cx + (14 - K), Y);
        }
    }
}

static VOID
DestroyPicker(VOID)
{
    UINTN I;

    for (I = 0; I < EntryCount; I++)
        if (Tile[I]) {
            deleteObject(guiobj(Tile[I]));
            Tile[I] = NULL;
        }
    if (Pop) {
        deleteObject(guiobj(Pop));
        Pop = NULL;
    }
    drawBackground();
}

/* returns the chosen index, or -1 for Escape (go to the maintenance menu) */
static INTN
PickLoop(EFI_SYSTEM_TABLE *St, UINTN Cols)
{
    EFI_EVENT Tick = NULL, Sec = NULL, Ev[3];
    UINTN Which, N;
    UINTN Remaining = TimeoutSec;
    BOOLEAN Counting = TimeoutSec > 0;
    CHAR8 Msg[128];
    INTN Result = -1;

    gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &Tick);
    gBS->SetTimer(Tick, TimerPeriodic, 100000ULL);          /* 10 ms: pointer */
    gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &Sec);
    gBS->SetTimer(Sec, TimerPeriodic, 10000000ULL);         /* 1 s: countdown */

    N = 0;
    Ev[N++] = St->ConIn->WaitForKey;
    Ev[N++] = Tick;
    Ev[N++] = Sec;

    SelectTile(DefaultIndex);
    if (Counting) {
        AsciiSPrint(Msg, sizeof(Msg), "%a starts in %d seconds.  Press any key to stop.",
                    Entries[Sel].Title, (INT32)Remaining);
        DrawStatus(Msg);
    } else {
        DrawStatus("Choose a system and press Return.");
    }

    for (;;) {
        EFI_INPUT_KEY Key;

        if (Clicked >= 0) {
            Result = Clicked;
            break;
        }
        if (EFI_ERROR(gBS->WaitForEvent(N, Ev, &Which)))
            break;
        if (Which == 1) {
            (VOID)ArcsInputPollPointers();
            continue;
        }
        if (Which == 2) {
            if (Counting) {
                if (Remaining > 0)
                    Remaining--;
                if (Remaining == 0) {
                    Result = Sel;
                    break;
                }
                AsciiSPrint(Msg, sizeof(Msg),
                            "%a starts in %d seconds.  Press any key to stop.",
                            Entries[Sel].Title, (INT32)Remaining);
                DrawStatus(Msg);
            }
            continue;
        }
        while (!EFI_ERROR(St->ConIn->ReadKeyStroke(St->ConIn, &Key))) {
            INTN New = Sel;

            if (Counting) {
                Counting = FALSE;
                DrawStatus("Choose a system and press Return.");
            }
            if (Key.ScanCode == SCAN_ESC) {
                Result = -1;
                goto out;
            }
            if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.UnicodeChar == L' ') {
                Result = Sel;
                goto out;
            }
            if (Key.ScanCode == SCAN_RIGHT && Sel + 1 < (INTN)EntryCount) New = Sel + 1;
            else if (Key.ScanCode == SCAN_LEFT && Sel > 0) New = Sel - 1;
            else if (Key.ScanCode == SCAN_DOWN && Sel + (INTN)Cols < (INTN)EntryCount) New = Sel + (INTN)Cols;
            else if (Key.ScanCode == SCAN_UP && Sel - (INTN)Cols >= 0) New = Sel - (INTN)Cols;
            else if (Key.ScanCode == SCAN_HOME) New = 0;
            else if (Key.ScanCode == SCAN_END) New = (INTN)EntryCount - 1;
            if (New != Sel)
                SelectTile(New);
        }
    }
out:
    gBS->CloseEvent(Tick);
    gBS->CloseEvent(Sec);
    return Result;
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */
/* ------------------------------------------------------------------ */

static VOID
Message(CONST CHAR8 *Text)
{
    struct Dialog *D = createDialog((char *)Text, DIALOGWARNING, 0);

    drawObject(guiobj(D));
    gBS->Stall(2500000);
    deleteObject(guiobj(D));
}

BOOLEAN
ArcsBootMenuRun(EFI_SYSTEM_TABLE *SystemTable)
{
    UINTN I, Cols, Rows;
    INTN Pick;
    BOOT_ENTRY *E;

    if (gfxgui.htp == NULL)
        return FALSE;

    ArcsUefiConnectAll();
    ParseConf();

    /* No boot.conf: one entry from arcs.env, as before the picker existed. */
    if (EntryCount == 0) {
        CONST CHAR8 *L = getenv("OSLoader");
        CONST CHAR8 *O = getenv("OSLoadOptions");

        E = NewEntry();
        if (E && L) {
            AsciiStrCpyS(E->Title, TITLE_MAX, "Gentoo Linux");
            E->Kind = KIND_KERNEL;
            E->Icon = ICON_LINUX;
            UnicodeSPrint(E->Path, sizeof(E->Path), L"\\");
            PathToWide(E->Path + 1, PATH_MAX_W - 1, L);
            if (O)
                AsciiStrnCpyS(E->Options, OPTS_MAX, O, OPTS_MAX - 1);
        } else if (E) {
            EntryCount--;
        }
    }
    DiscoverAll();

    /* discovery log on the ESP: real hardware has no console to read */
    {
        CHAR8 *Log = AllocateZeroPool(4096);

        if (Log) {
            UINTN Len = 0;

            for (I = 0; I < EntryCount && Len < 3500; I++)
                Len += AsciiSPrint(Log + Len, 4096 - Len, "%d: %a  [%a%a] %s  vol=%a\n", (INT32)I,
                                   Entries[I].Title, Entries[I].Kind == KIND_KERNEL ? "kernel" : "chain",
                                   Entries[I].Auto ? ",auto" : "", Entries[I].Path,
                                   SameVolume(Entries[I].Volume, NULL) ? "own" : "other");
            if (EntryCount == 0)
                AsciiSPrint(Log, 4096, "no entries\n");
            ArcsUefiWriteFile(L"\\EFI\\ARCS\\bootmenu.log", Log, AsciiStrLen(Log));
            FreePool(Log);
        }
    }

    if (EntryCount <= 1) {
        /* nothing to choose between: keep the old straight-through boot */
        return FALSE;
    }

    DefaultIndex = 0;
    for (I = 0; I < EntryCount; I++)
        if (DefaultTitle[0] && AsciiStrCmp(Entries[I].Title, DefaultTitle) == 0)
            DefaultIndex = (INTN)I;

    Cols = EntryCount < 4 ? EntryCount : 4;
    Rows = (EntryCount + Cols - 1) / Cols;
    Sel = -1;
    Clicked = -1;

    for (;;) {
        BuildPicker(Cols, Rows);
        Pick = PickLoop(SystemTable, Cols);
        DestroyPicker();
        if (Pick < 0)
            return FALSE;                   /* Escape: maintenance menu */

        E = &Entries[Pick];
        ArcsBootPickerChose = 1;
        if (E->Kind == KIND_CHAIN || !SameVolume(E->Volume, NULL)) {
            EFI_STATUS St = ArcsUefiStartImageAt(E->Volume ? E->Volume : ArcsUefiOwnVolume(),
                                                 E->Path, E->Options[0] ? E->Options : NULL,
                                                 E->Kind == KIND_KERNEL);

            drawBackground();
            if (EFI_ERROR(St)) {
                CHAR8 Msg[96];

                AsciiSPrint(Msg, sizeof(Msg), "\n%a could not be started (%r).", E->Title, St);
                Message(Msg);
                Clicked = -1;
                Sel = -1;
                continue;                   /* back to the picker */
            }
            return TRUE;
        }

        /* kernel on our own ESP: hand it to the normal autoboot path so the
         * "Starting up the system" dialog shows */
        {
            CHAR8 Loader[PATH_MAX_W];
            UINTN K = 0, S = (E->Path[0] == L'\\') ? 1 : 0;

            for (; E->Path[S] && K + 1 < sizeof(Loader); S++, K++)
                Loader[K] = (E->Path[S] == L'\\') ? '/' : (CHAR8)E->Path[S];
            Loader[K] = 0;
            setenv("OSLoader", Loader);
            if (E->Options[0])
                setenv("OSLoadOptions", E->Options);
        }
        return TRUE;
    }
}
