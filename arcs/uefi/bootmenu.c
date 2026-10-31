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
#include <Protocol/SimpleTextInEx.h>
#include <Guid/FileInfo.h>

#include <stand_htport.h>
#include <gfxgui.h>
#include <guicore.h>
#include <style.h>
#include <pcbm/pcbm.h>

#include "bootmenu.h"
#include "bootanim.h"
#include "maintenance_menu_bringup.h"
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

#define ROW_W    460
#define ROW_H    62
#define MAXROWS  7
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

static CHAR8 Fitted[MAX_ENTRIES][TITLE_MAX];

#define WELLW  (ICONBOX + 2 * DIALOGINTBDW)

static VOID
DrawTileIcon(INTN I)
{
    struct gui_obj *g = guiobj(Tile[I]);
    int Wx = g->x1 + 8;
    int Wy = g->y1 + (ROW_H - WELLW) / 2;
    struct htp_font *F = &gfxgui.htp->fonts[ncenB18];

    color(WellColor[Entries[I].Icon]);
    sboxfi(Wx, Wy, Wx + WELLW - 1, Wy + WELLW - 1);
    DrawGlyph(Entries[I].Icon, Wx + DIALOGINTBDW, Wy + DIALOGINTBDW);
    dialogIntBoarder(Wx, Wy, Wx + WELLW - 1, Wy + WELLW - 1, 0);

    color(BLACK);
    cmov2i(Wx + WELLW + 14, g->y1 + (ROW_H - (F->height - F->descender)) / 2 + F->descender);
    puttext(Fitted[I], ncenB18);
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
    int Max = ROW_W - 8 - WELLW - 14 - 36;

    AsciiStrnCpyS(Dst, TITLE_MAX, Src, TITLE_MAX - 1);
    if (textWidth(Dst, ncenB18) <= Max)
        return;
    /* too long: cut it and say so with an ellipsis */
    L = AsciiStrLen(Dst);
    while (L > 1) {
        Dst[--L] = 0;
        if (L + 3 < TITLE_MAX) {
            Dst[L] = '.';
            Dst[L + 1] = '.';
            Dst[L + 2] = '.';
            Dst[L + 3] = 0;
        }
        if (textWidth(Dst, ncenB18) <= Max)
            return;
        Dst[L] = 0;
    }
}

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

/* The clicked menu item becomes the flyout's side title: same icon and
 * label as the maintenance menu entry, here at the panel's left end. */
extern struct pcbm sysstart;
static CONST char *TitleText = "Start System";
static struct Button *TitleBtn;
static int TilesX;

static void
TitleClicked(struct Button *B, __scunsigned_t Value)
{
    (void)B;
    (void)Value;
    Clicked = -2;                   /* click the title: back to the menu */
}

/* One width for every option's flyout (the longest of the five names), so the
 * geometry does not change with which item opened it; the label centres in
 * the space beside the icon. */
static CONST char *MenuNames[] = {
    "Start System", "Install System Software", "Run Diagnostics",
    "Recover System", "Enter Command Monitor"
};

static int
TitleLabelW(VOID)
{
    int W = 0;
    UINTN I;

    for (I = 0; I < sizeof(MenuNames) / sizeof(MenuNames[0]); I++)
        if (textWidth(MenuNames[I], ncenB18) > W)
            W = textWidth(MenuNames[I], ncenB18);
    return W;
}

static int
TitleCellW(VOID)
{
    return MENUBUTTW + 14 + TitleLabelW();
}

static int PX, PY;
static UINTN Offset, Visible;       /* the window of entries shown (scrolls) */

/* Layout: [ title floating on the gradient ]  [ raised panel: a list ] */
static VOID
BuildPicker(VOID)
{
    int StatH = gfxgui.htp->fonts[helvR10].height + 10;
    int ListH, PanelW, PanelH, CellW, W;
    /* centred on the menu block, which sits in the upper two thirds, so the
     * logo below stays clear */
    int CenterY = gfxHeight() - (((gfxHeight() * 2) / 3) >> 1);
    int PairW = MENUBUTTW + 10 + textWidth(TitleText, ncenB18);
    int Tx, Ty, Top, PanelX;
    UINTN I;

    Visible = EntryCount - Offset;
    if (Visible > MAXROWS)
        Visible = MAXROWS;
    ListH = (int)Visible * ROW_H + ((int)Visible - 1) * GAP;
    PanelW = 2 * MARG + ROW_W;
    PanelH = 2 * MARG + ListH + StatH;
    CellW = TitleCellW();
    W = CellW + 2 * MARG + PanelW;

    PX = (gfxWidth() - W) / 2;
    PY = CenterY - PanelH / 2;
    PanelX = PX + CellW + 2 * MARG;

    /* the panel holds only the choices */
    Pop = createCanvas(PanelW, PanelH);
    moveObject(guiobj(Pop), PanelX, PY);
    drawObject(guiobj(Pop));

    /* the clicked option, icon + label as one unit, centred in its cell */
    Tx = PX + (CellW - PairW) / 2;
    Ty = PY + PanelH / 2 - MENUBUTTH / 2;
    TitleBtn = createButton(Tx, Ty, MENUBUTTW, MENUBUTTH);
    addButtonImage(TitleBtn, &sysstart);
    addButtonCallBack(TitleBtn, TitleClicked, 0);
    drawObject(guiobj(TitleBtn));
    color(BLACK);
    cmov2i(Tx + MENUBUTTW + 10, Ty + 15);
    puttext((char *)TitleText, ncenB18);

    TilesX = PanelX + MARG;
    Top = PY + PanelH - 1 - MARG;
    for (I = Offset; I < Offset + Visible; I++) {
        int Y = Top - (int)(I - Offset) * (ROW_H + GAP) - ROW_H + 1;
        struct Button *B = createButton(TilesX, Y, ROW_W, ROW_H);

        FitTitle(Fitted[I], Entries[I].Title);
        addButtonCallBack(B, TileClicked, (__scunsigned_t)I);
        TileOrigHandler = B->gui.eventHandler;
        B->gui.eventHandler = TileHandler;
        Tile[I] = B;
    }
    for (I = Offset; I < Offset + Visible; I++)
        drawObject(guiobj(Tile[I]));

    StatusX1 = TilesX;
    StatusX2 = TilesX + ROW_W - 1;
    StatusY1 = PY + MARG;
    StatusY2 = StatusY1 + StatH - 4;
    StatusW = ROW_W;
}

static VOID
DestroyPicker(VOID)
{
    UINTN I;

    if (TitleBtn) {
        deleteObject(guiobj(TitleBtn));
        TitleBtn = NULL;
    }
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

static VOID DrawStatus(CONST CHAR8 *Msg);

/* Select an entry, scrolling the list window if it is outside it. */
static VOID
ScrollTo(INTN New)
{
    if (Sel < 0 || (UINTN)New < Offset || (UINTN)New >= Offset + Visible) {
        UINTN Want = (UINTN)New;

        if (Sel < 0 || Want < Offset)
            Offset = Want < MAXROWS / 2 ? 0 : Want - MAXROWS / 2;
        else
            Offset = Want + 1 > MAXROWS ? Want + 1 - MAXROWS : 0;
        if (Offset + MAXROWS > EntryCount)
            Offset = EntryCount > MAXROWS ? EntryCount - MAXROWS : 0;
        if (Sel >= 0) {
            DestroyPicker();
            BuildPicker();
            DrawStatus("Choose a system and press Return.");
        }
        Sel = -1;
    }
    SelectTile(New);
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

    ScrollTo(DefaultIndex);
    if (Counting) {
        AsciiSPrint(Msg, sizeof(Msg), "%a starts in %d seconds.  Press any key to stop.",
                    Entries[Sel].Title, (INT32)Remaining);
        DrawStatus(Msg);
    } else {
        DrawStatus("Choose a system and press Return.");
    }

    for (;;) {
        EFI_INPUT_KEY Key;

        if (Clicked == -2) {
            Result = -1;
            break;
        }
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
            if ((Key.ScanCode == SCAN_DOWN || Key.ScanCode == SCAN_RIGHT) && Sel + 1 < (INTN)EntryCount)
                New = Sel + 1;
            else if ((Key.ScanCode == SCAN_UP || Key.ScanCode == SCAN_LEFT) && Sel > 0)
                New = Sel - 1;
            else if (Key.ScanCode == SCAN_PAGE_DOWN)
                New = (Sel + (INTN)Visible < (INTN)EntryCount) ? Sel + (INTN)Visible : (INTN)EntryCount - 1;
            else if (Key.ScanCode == SCAN_PAGE_UP)
                New = (Sel > (INTN)Visible) ? Sel - (INTN)Visible : 0;
            else if (Key.ScanCode == SCAN_HOME) New = 0;
            else if (Key.ScanCode == SCAN_END) New = (INTN)EntryCount - 1;
            if (New != Sel)
                ScrollTo(New);
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
    drawBackground();       /* the dialog leaves its pixels behind */
}

static BOOLEAN Loaded;
static BOOLEAN ExplicitDefault;     /* boot.conf names a default= entry */

static VOID
LoadEntries(VOID)
{
    UINTN I;
    BOOT_ENTRY *E;

    if (Loaded)
        return;
    Loaded = TRUE;
    TimeoutSec = 0;                 /* the picker is manual: Start System */

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

    /* identical labels (several kernels of one version) get the directory
     * they were found in, then a number */
    for (I = 0; I < EntryCount; I++) {
        UINTN J, Dup = 0;

        for (J = 0; J < EntryCount; J++)
            if (J != I && AsciiStrCmp(Entries[I].Title, Entries[J].Title) == 0)
                Dup++;
        if (Dup && Entries[I].Auto) {
            CHAR16 *Last = Entries[I].Path, *P;
            CHAR16 *Prev = Entries[I].Path;
            CHAR8 Suffix[24];
            UINTN K = 0;

            for (P = Entries[I].Path; *P; P++)
                if (*P == L'\\') {
                    Prev = Last;
                    Last = P;
                }
            /* Prev..Last is the parent directory name (with its leading '\\') */
            for (P = Prev + (*Prev == L'\\'); P < Last && K + 1 < sizeof(Suffix); P++)
                Suffix[K++] = (CHAR8)*P;
            Suffix[K] = 0;
            if (K && AsciiStrLen(Entries[I].Title) + K + 4 < TITLE_MAX)
                AsciiSPrint(Entries[I].Title + AsciiStrLen(Entries[I].Title),
                            TITLE_MAX - AsciiStrLen(Entries[I].Title), " (%a)", Suffix);
        }
    }
    {
        UINTN J, N;

        for (I = 0; I < EntryCount; I++) {
            N = 1;
            for (J = 0; J < I; J++)
                if (AsciiStrCmp(Entries[I].Title, Entries[J].Title) == 0)
                    N++;
            if (N > 1 && AsciiStrLen(Entries[I].Title) + 5 < TITLE_MAX)
                AsciiSPrint(Entries[I].Title + AsciiStrLen(Entries[I].Title),
                            TITLE_MAX - AsciiStrLen(Entries[I].Title), " #%d", (INT32)N);
        }
    }

    DefaultIndex = 0;
    for (I = 0; I < EntryCount; I++)
        if (DefaultTitle[0] && AsciiStrCmp(Entries[I].Title, DefaultTitle) == 0)
            DefaultIndex = (INTN)I;

    ExplicitDefault = FALSE;
    for (I = 0; I < EntryCount; I++)
        if (DefaultTitle[0] && AsciiStrCmp(Entries[I].Title, DefaultTitle) == 0)
            ExplicitDefault = TRUE;
}

/* Point the normal autoboot path (OSLoader / OSLoadOptions) at a kernel on
 * the PROM's own ESP. */
static VOID
PrepareKernelEnv(BOOT_ENTRY *E)
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

static BOOLEAN
NeedsDirectStart(BOOT_ENTRY *E)
{
    return E->Kind == KIND_CHAIN || !SameVolume(E->Volume, NULL);
}

static EFI_STATUS
StartDirect(BOOT_ENTRY *E)
{
    EFI_STATUS St = ArcsUefiStartImageAt(E->Volume ? E->Volume : ArcsUefiOwnVolume(),
                                         E->Path, E->Options[0] ? E->Options : NULL,
                                         E->Kind == KIND_KERNEL);

    drawBackground();
    return St;
}

/*
 * Power-on policy.  One entry (or an explicit default= in boot.conf): the
 * normal autoboot path runs and boots it.  Several entries and no explicit
 * default: autoboot is skipped and the maintenance menu is shown, where
 * Start System opens the picker.
 */
BOOLEAN
ArcsBootMenuPowerOn(VOID)
{
    BOOT_ENTRY *E;

    if (gfxgui.htp == NULL)
        return TRUE;
    LoadEntries();
    if (EntryCount > 1 && !ExplicitDefault) {
        setenv("NoAutoLoad", "y");
        return FALSE;
    }
    if (EntryCount == 0)
        return TRUE;

    E = &Entries[DefaultIndex];
    if (NeedsDirectStart(E)) {
        /* not something the SGI autoboot path can start: do it here */
        if (!EFI_ERROR(StartDirect(E)))
            return FALSE;
        setenv("NoAutoLoad", "y");
        return FALSE;
    }
    PrepareKernelEnv(E);
    return TRUE;
}

/* Best effort: is Shift held right now?  Uses the extended text input
 * protocol; not every firmware reports a bare modifier. */
BOOLEAN
ArcsBootMenuShiftHeld(EFI_SYSTEM_TABLE *St)
{
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *Ex = NULL;
    EFI_KEY_DATA Key;
    BOOLEAN Shift = FALSE;
    UINTN Guard = 0;

    if (EFI_ERROR(gBS->HandleProtocol(St->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid,
                                      (VOID **)&Ex)) || Ex == NULL)
        return FALSE;
    while (Guard++ < 16 && !EFI_ERROR(Ex->ReadKeyStrokeEx(Ex, &Key)))
        if ((Key.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) &&
            (Key.KeyState.KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)))
            Shift = TRUE;
    return Shift;
}

/*
 * Start System: the picker when there is a choice, then hand the chosen
 * kernel to the normal autoboot path.
 */
static VOID
GetGeom(ARCS_ANIM_GEOM *G)
{
    struct gui_obj *T = guiobj(TitleBtn);
    int PairW = MENUBUTTW + 10 + textWidth(TitleText, ncenB18);

    G->Title.X1 = T->x1;
    G->Title.Y1 = T->y1;
    G->Title.X2 = T->x1 + PairW - 1;
    G->Title.Y2 = T->y2;
    G->Panel.X1 = Pop->gui.x1;
    G->Panel.Y1 = Pop->gui.y1;
    G->Panel.X2 = Pop->gui.x2;
    G->Panel.Y2 = Pop->gui.y2;
}

static VOID
DestroyPickerFn(VOID)
{
    DestroyPicker();
}

static ARCS_START_RESULT
StartSystemInner(EFI_SYSTEM_TABLE *SystemTable)
{
    INTN Pick;
    BOOT_ENTRY *E;
    BOOLEAN AnimTried = FALSE, AnimOn = FALSE;

    if (gfxgui.htp == NULL)
        return ARCS_START_PROCEED;
    LoadEntries();
    if (EntryCount == 0)
        return ARCS_START_PROCEED;

    for (;;) {
        if (EntryCount == 1) {
            Pick = 0;
        } else {
            BOOLEAN Built = FALSE;

            Sel = -1;
            Clicked = -1;
            Offset = 0;
            if (!AnimTried) {               /* the menu -> flyout transition, once */
                AnimTried = TRUE;
                AnimOn = Built = ArcsAnimOpen(BuildPicker, GetGeom, 0, SystemTable);
            }
            if (!Built)
                BuildPicker();
            Pick = PickLoop(SystemTable, 1);
            if (Pick < 0) {                 /* Escape or the title: back to the menu */
                if (AnimOn)
                    ArcsAnimClose(DestroyPickerFn, SystemTable);
                else
                    DestroyPicker();
                return ARCS_START_CANCELLED;
            }
            DestroyPicker();
        }

        E = &Entries[Pick];
        if (NeedsDirectStart(E)) {
            EFI_STATUS St = StartDirect(E);

            if (!EFI_ERROR(St))
                return ARCS_START_RAN;
            {
                CHAR8 Msg[96];

                AsciiSPrint(Msg, sizeof(Msg), "\n%a could not be started (%r).", E->Title, St);
                Message(Msg);
            }
            if (EntryCount == 1)
                return ARCS_START_CANCELLED;
            AnimOn = FALSE;                 /* the picker returns without animation */
            continue;                       /* back to the picker */
        }
        PrepareKernelEnv(E);
        return ARCS_START_PROCEED;
    }
}

/*
 * Start System: the picker when there is a choice, then hand the chosen
 * kernel to the normal autoboot path.
 */
ARCS_START_RESULT
ArcsBootMenuStartSystem(EFI_SYSTEM_TABLE *SystemTable)
{
    ARCS_START_RESULT R = StartSystemInner(SystemTable);

    ArcsMenuPark(0);                        /* the menu items back where they were */
    ArcsAnimEnd();
    return R;
}
