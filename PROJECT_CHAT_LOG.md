# IRIX / IID / ARCS Project Chat Continuity Log

Last updated: 2026-09-08

This file is a durable handoff/log for the ChatGPT-assisted reverse-engineering and porting work in `gazecube/irix-work`. It exists specifically so a locked or exhausted chat does not require another manual chat export before work can continue.

This first version intentionally records the project history and current technical state up through 2026-09-08. Later sessions should append/update it when substantial progress is made.

## Ground rules

- Fidelity first. Original SGI/IRIX source, binaries, symbols, DWARF, resources, disassembly, and observed behavior are primary evidence.
- For reconstruction work, recover original names and semantics from binaries/debug information before inventing names. If a name truly cannot be recovered, a descriptive reconstructed name is acceptable but should be marked as such.
- Avoid drastic speculative rewrites. Changes should be supported by original binaries/source/behavior whenever possible.
- For ARCS specifically: `Port first. Reconstruct only where necessary.` Keep machine-independent SGI code as intact as physically possible and put UEFI/Open Firmware/platform substitutions underneath it.
- ASCII-only in project discussion and project-authored compatibility material where practical.
- Do not treat old plans as current truth when later testing/source work superseded them. Superseded decisions are explicitly noted below.

# 1. IID / Interactive Desktop reconstruction

## Overall objective

Reimplement the SGI IRIX Interactive Desktop closely enough that the original desktop/file-manager/login experience and feature set can run on modern UNIX/Linux systems, especially MaXX Interactive Desktop, while preserving original behavior rather than creating a visual imitation.

The work grew from the SGI login-manager effort into a larger reconstruction of the Interactive Desktop stack, including the file manager, desktop, ViewKit/ViewClass/private widget infrastructure, icon/FTR behavior, and supporting SGI libraries.

Primary working branch for this effort: `irix-re`.

## Source archaeology and recovered material

A major source inventory established that the available IRIX material is unusually rich in debug information. One inventory covered 959 ELF objects, with 491 containing full SGI DWARF and 533 containing `.debug_info`. This made symbol/DWARF-backed reconstruction a central strategy rather than guessing APIs from screenshots.

Important uploaded/extracted source material has included IRIX filesystem/install-set trees such as `desktop_eoe.tar.gz`, `sysadmdesktop.tar.gz`, SWE material, raw application binaries, preferences/resources, icons, and historical chat exports. When available, the actual binaries are treated as the reference implementation.

## Libraries and application integration

The reconstructed application/library stack reached the point where `libimdApp.so.1` links against reconstructed `libSgm.so.1` and `libimdIconPanel.so.1`. Compilation and ELF linkage checks passed.

An early application-level integration failure occurred while realizing nested `ImdViewPanel`: `ImdIconFinder` reached `XDeleteProperty` with window `0`. The debugging policy was to trace the original IRIX binaries first rather than patching around the symptom by judgment.

`ImdIconFinder` subsequently became functional.

The file manager (`fm`) reached its first meaningful launch on Linux: it displayed directory contents and exercised reconstructed application logic. Drawing was initially rough, but the important milestone was that this was no longer a static mockup - original/reconstructed SGI logic was actually operating.

## FTR / file typing / differentiated icons

A binary-backed FTR path was recovered:

```text
ImdModelBasedIconView::getType()
  -> ImdTypedModel::getAttr(0xEFFF0080)
  -> typeFromName("imdFTR_Type", filename)
  -> imdFtrLoadDB("/usr/lib/filetype/desktop.otr")
  -> imdFtrIconTypeFromFileName()
  -> imdFtrTypeName()
```

This produced differentiated SGI icons in `fm` and `iid-desktop` rather than generic placeholder icons. The next deeper fidelity target from that checkpoint was the full `desktop.otr` matcher.

A recovered checkpoint for `irix-re` recorded tip `ded2cf5f9fb9d8661ef7da8e2f5c1ad9e6167d8c` at that stage.

## Desktop/root-window integration

A later visible desktop integration problem was traced away from icon lookup and into desktop/root-window ownership. `iid-desktop` was painting/clearing its own background with `XSetWindowBackground` / `XClearWindow`, stomping on MaXX/5Dwm's root presentation and producing blue rectangular islands behind desktop icons.

The converged fix was to stop owning/clearing that background and have the relevant desktop/icon container inherit with `ParentRelative`. The immediate validation was rebuilding and running:

```sh
./iid-desktop ~/Desktop
```

with the expected result that the blue rectangles disappear while icons remain intact.

## UI/widget archaeology

The project has recovered and/or reconstructed private SGI/ViewKit/ViewClass widget behavior rather than replacing it with modern approximations. One detailed target was the side-pane thumbwheel control: original bitmap/highlight behavior, center fade, hover/drag overlays, and directional mapping were studied from assets/screenshots/binaries.

Other planned/ongoing file-manager areas include tree/gallery behavior, distill, shelf behavior, Pathfinder/iconic layout, and eventually removing ROX-Filer once the reconstructed SGI file manager is sufficiently viable.

## Login manager

A substantial SGI-style login-manager effort preceded/overlapped IID reconstruction. The goal is an SGI greeter/session experience based on the original dtlogin/xdm lineage rather than a separate modern greeter. The reconstructed panel/widget stack progressed far enough to populate users and apply original panel/background resources. This work remains part of the broader fidelity-first desktop effort.

# 2. ARCS/PROM native x86-64 UEFI port

## Goal and philosophy

Build an SGI ARCS/PROM-compatible environment as a native x86-64 UEFI application. It should faithfully reproduce the SGI boot/maintenance experience while providing a firmware abstraction suitable for a future IRIX/x86-64 port and also function as a general boot manager capable of launching Linux EFI-stub kernels and other UEFI applications.

The IRIX 6.5.7m `stand/arcs` source is to be used as much as possible. This is fundamentally a port of SGI code, not a clean-room redesign.

Generic EFI branch: `arcs-uefi`.
Physical Apple test branch: `arcs-macbook`.

Historical local paths:

```text
repo clone: ~/Projects/ARCS-work
EDK II:    /home/nekron/src/edk2
```

Typical build:

```sh
cd ~/Projects/ARCS-work
git pull

WORKSPACE=$HOME/src/edk2 \
TOOLCHAIN=GCC \
TARGET=DEBUG \
./arcs/build-edk2.sh

mv ~/src/edk2/Build/ArcsPkg/DEBUG_GCC/X64/ArcsGfxBringup.efi ~/QEMU_TEMP/
```

`build-edk2.sh` removes `$WORKSPACE/ArcsPkg` and recopies repo `arcs/`, so stale EDK package state should not be blamed for build failures.

Existing EDK library mapping fixes include:

- DevicePathLib: `de9a9f8b80ca26482970bb95dca9984fdd4ed77a`
- PrintLib: `ef56e5f40ccf642a5f519d2301ecc632727356c1`

## Graphics and input

The original SGI cursor was recovered from `arcs/sgisrc/include/cursor.h` and `vc1cursor.c`: 16x16, hotspot `(3,1)`.

A GOP software cursor with save-under works. Hover/click behavior works, and USB HID fallback works in QEMU. Keyboard input works.

The original SGI background is rendered and scales to widescreen through the source-derived `drawShadedBackground(... gfxWidth()-1, gfxHeight()-1)` path.

`input_bringup.c` contains Escape handling and controller connection at input-loop startup.

The graphical startup/maintenance UI reached very high visual fidelity and was explicitly considered visually correct before later autoboot integration work. Do not disturb this known-good rendering casually.

## Original maintenance menu

The canonical source is `stand/arcs/lib/libsk/cmd/pmenu_cmd.c`.

The five original entries are preserved:

```text
1. Start System             -> menu_boot_sys, icon sysstart
2. Install System Software -> menu_doinstall
3. Run Diagnostics         -> menu_boot_ide
4. Recover System          -> menu_recovery
5. Enter Command Monitor   -> menu_manualmode
```

`Start System` was corrected to call original-style `menu_boot_sys()` rather than directly invoking a UEFI-specific default boot helper.

# 3. Original SGI autoboot policy restored

An important correction occurred when the original SGI PROM source showed that autoboot already existed. A proposed second/custom autoboot subsystem was rejected.

Original `startup.c` gate:

```c
char *autoboot;
if ((autoboot = getenv("AutoLoad")) &&
    (*autoboot == 'Y' || *autoboot == 'y') &&
    !getenv("NoAutoLoad"))
    autoboot(5,0,0);
```

Original `menu_boot_sys()` calls `autoboot(0,0,0)`.

Original `auto.c` loops boot candidates produced by `make_bootfile()` and calls ARCS `Execute()`.

Original `bootname.c` builds the bootfile from `SystemPartition` + `OSLoader`.

The intended architecture is therefore:

```text
original SGI startup/menu/autoboot/env
  -> original ARCS device/path naming
  -> original SystemPartition + OSLoader policy
  -> reconstructed UEFI Execute() boundary
       -> FileDevicePath()
       -> LoadImage()
       -> StartImage()
```

## Autoboot integration files

`arcs/sgisrc/libsc/lib/bootname.c`
- Ported original SGI `bootname.c`.
- Preserves `make_bootfile()` and `env_get_var()`.
- UEFI-only compatibility includes/local ASCII whitespace helper.

`arcs/uefi/bootstrap_compat.c`
- Current ARCS environment is RAM-backed.
- On the MacBook test branch, relevant values include:

```text
SystemPartition = scsi(0)disk(0)rdisk(0)partition(2)
OSLoader        = System/Library/CoreServices/boot.efi
AutoLoad        = Y
sgilogo         = y
OSLoadOptions   = auto
```

`NoAutoLoad` is supported but unset by default; `kernname` is also present.

`arcs/uefi/prom_autoboot.c`
- Compatibility shim preserving `make_bootfile -> setenv("kernname") -> Execute`.
- `menu_boot_sys()` -> `autoboot(0,...)`.
- `autoboot(delay,...)` currently has a temporary UEFI timer/Escape pause because the original `pause(P_STOP|P_INTERRUPT,...)` runtime is not yet ported.
- Provides `ArcsPromAutobootInit(EFI_SYSTEM_TABLE*)`.

`arcs/uefi/prom_startup.c/.h`
- Preserves the source-backed original `startup()` gate.

`entry.c`
- Calls `ArcsPromAutobootInit(SystemTable)`.
- Calls `startup()` before maintenance input.

`uefi_boot.c/.h`
- Exposes reconstructed boundary:

```c
INT32 Execute(char *Path, INT32 Argc, char **Argv, char **Envp)
```

- Translates ARCS bootfile suffix to EFI path.
- Enumerates SimpleFileSystem handles.
- Uses matching handle with `FileDevicePath`, `LoadImage`, `StartImage`.
- Restores GOP mode/state when child returns.

## Startup ordering

The ordering was corrected so the maintenance menu does not appear underneath the autoboot dialog:

```text
ArcsDrawBackgroundBringup()
  -> startup()
  -> if startup returns, ArcsDrawMaintenanceMenuBringup()
  -> ArcsRunInputBringup()
```

Relevant commits:

- `adc26fad1167fc2f9e2b8969ce43cfce20ef5046`
- `4e546cb7d3206e13b3b0e0b25673f1f7921f0377`

Physical testing confirmed the menu remains hidden until Escape stops autoboot.

## Original SGI autoboot dialog

Original `auto.c` behavior was recovered, including:

```text
Starting up the system...
[ Stop for Maintenance ]
```

The source creates a progress dialog, moves it to the original geometry, conditionally adds `DIALOGSTOP`, labels it `Stop for Maintenance`, attaches `stopCallBack`, and draws it. Original callback control flow uses `longjmp(auto_buf,1)`.

`arcs/uefi/dialog_autoboot.c` is a source-derived subset of original `dialog.c`, preserving geometry/progress icon/button layout/drawing/object lifetime.

Relevant commits:

- `672d0a3fa8ea89bbfbcd9c8a5a87fc56801bb041`
- `194f0f0f1eae1d012d2b461c246ba50db142b9`
- `b93e44823ef12ea4ec2f591998b39a3eb94259a1`

Mouse click during the temporary UEFI pause is not yet guaranteed because that pause waits on ConIn/timer rather than pumping the pointer/GUI event loop. Escape works.

Original `isGuiMode()` was imported exactly:

```c
int
isGuiMode(void)
{
    return(gfxgui.htp && (gfxgui.htp->textport.state == GUI));
}
```

Commit: `3b915b66ac39a691a2f52019c193ee4f0165f686`.

# 4. UEFI child boot and FreeBSD testing

The generic chainloader searches all SimpleFileSystem handles for the loader path, constructs a device path using the matching handle, and calls `LoadImage` / `StartImage`.

Before child execution it saves GOP mode. If the child returns after altering graphics, ARCS restores the mode, invalidates cursor save-under state, and redraws the SGI background/object tree.

Child-return fix: `7e3bf0b80e8930ada0be790a00e2e294149db7be`. This fixed the return path wholesale and must remain a return-path-only fix.

FreeBSD loader testing progressed through the EFI loader into OS root lookup.

Historical test-disk topology:

```text
/dev/sda1  4G /boot, contains /boot/EFI/FreeBSD/loader.efi
/dev/sda2  LIVE Linux root - NEVER pass whole /dev/sda to QEMU
/dev/sdb   FreeBSD root/storage + swap + Windows leftovers
expected FreeBSD root: ada1p4
```

Safe QEMU passthrough uses `snapshot=on`.

# 5. Physical MacBook hardware validation

Target: Sandy Bridge era MacBookPro8,x using Apple EFI.

Dedicated FAT partition layout:

```text
EFI
`-- BOOT
    `-- BOOTX64.EFI
```

Apple's Option picker exposes it as `EFI Boot`; no persistent custom EFI NVRAM boot entry is required.

Physical validation results:

```text
[PASS] Apple EFI launches ARCS
[PASS] GOP graphics
[PASS] SGI graphical PROM
[PASS] Maintenance interface
[PASS] Keyboard
[PASS] Original autoboot path
[PASS] Apple HFS+ exposed to ARCS
[PASS] disk0s2 boot.efi found
[PASS] LoadImage / StartImage
[PASS] Snow Leopard boots

[TODO] Built-in trackpad
[TODO] Graphics performance
```

The key milestone was genuine Snow Leopard boot through ARCS on physical Apple hardware.

Apple EFI exposes the HFS+ system volume through SimpleFileSystem sufficiently for the current loader; no custom HFS driver was required.

The built-in trackpad currently does not work. Do not guess its protocol. Inspect how firmware exposes it before implementing anything.

Graphics/UI rendering on the physical MacBook is visibly slow but functional.

## MacBook branch

`arcs-macbook` was created from generic `arcs-uefi` at commit `3b915b66ac39a691a2f52019c193ee4f0165f686`.

MacBook-specific workflow commit:

- `4059c4266c8a4ed9d7fadc1f8de5942f84c02a69` - `Add MacBook ARCS EFI build artifact`

Artifact name: `ARCS-MacBook-EFI`.

Mac-specific boot target commit includes:

- `3fedcd3e921338330a5488b29ad71fcf1cc9f098` - target native Mac OS X `boot.efi` on MacBook branch.

As of 2026-09-08, `arcs-macbook` was 16 commits ahead of `arcs-uefi` and 0 behind, with merge base `3b915b66ac39a691a2f52019c193ee4f0165f686`. Those 16 commits contain a mixture of generic Command Monitor/compatibility work and Mac-specific work.

Branch policy established on 2026-09-08:

```text
arcs-uefi
  -> generic x86-64 UEFI ARCS port
  -> original SGI code + generic compatibility layers
  -> eventual Linux boot/console work

arcs-macbook
  -> based on arcs-uefi
  -> Apple EFI / MacBook-specific bringup
  -> Snow Leopard target
  -> future Apple-specific input/trackpad quirks
```

Do NOT simply fast-forward `arcs-uefi` to `arcs-macbook`. Promote/cherry-pick the generic changes while leaving the MacBook workflow, Snow Leopard disk target, and future Apple-specific behavior on `arcs-macbook`.

# 6. NVRAM safety and environment persistence

Current hard rule: ARCS itself must not unexpectedly write Apple/UEFI NVRAM.

- macOS/native Apple code writing NVRAM is fine.
- Do not globally interpose or disable `SetVariable()` for child EFI images.
- Current ARCS tree audit found zero `SetVariable` and zero `RuntimeServices` references.
- `bootstrap_compat.c` environment is RAM-only.
- Do not add a speculative Mac-specific NVRAM blocker.
- Never map SGI environment variables directly to EFI variables.

IMPORTANT SUPERSEDED PLAN: an earlier design document discussed NVRAM-backed ARCS variables / `nvram.dat` and restricted reads of EFI boot variables. That is not the current architecture. The current persistence decision is FILE-BACKED ARCS environment state, not EFI NVRAM. Exact persistent file path/format remains to be finalized.

Current conceptual model:

```text
original SGI getenv()/setenv()
        ->
ARCS environment layer
        ->
RAM working copy + file-backed persistence
```

# 7. Genuine SGI Command Monitor - current active work

The immediate goal is to make the first `>>` on the MacBook/generic EFI build come from the actual SGI parser/textport stack. There must be no fake EFI shell and no reconstructed replacement parser when the original source exists.

## Original entry flow

Original `stand/arcs/lib/libsk/cmd/pmenu_cmd.c`:

```c
int
menu_manualmode(void)
{
    extern void EnterInteractiveMode();

    setTpButtonAction(EnterInteractiveMode,TPBDONE,WBONCHANGE);
    p_textmode();
    p_clear();

    printf("Command Monitor.  Type \"exit\" %sto return to the menu.\n",
           isgraphic(StandardOut) ? "or click on \"done\" " : "");

    return 1;
}
```

This is important: `menu_manualmode()` does NOT directly invoke the parser. It switches the presentation to text mode, clears it, prints the original message, and returns 1 so `menu_parser()` returns to PROM `main()`.

Original IP22 `main.c` uses:

```c
#define PROMPT ">> "
int _prom=1;
```

and loops through maintenance menu then command parser:

```text
menu_parser(&prom_menu)
  -> return to main after Enter Command Monitor
  -> close_noncons()
  -> _scandevs()
  -> command_parser(cmd_table, ">> ", 1, 0)
```

The genuine command table includes at least:

```text
auto
boot
.checksum
date
.dump
exit
.fill
.g
.go
help
.?
init
hinv
ls
.gio
passwd
printenv
.p
resetenv
resetpw
setenv
single
unsetenv
version
```

plus conditionally compiled commands.

## Imported parser/editor source

Current imports/work include:

- `parser.h`: exact upstream after correction. Initial import then restored verbatim at `254650698163acf6b34013561eee67a0ee6d3fa3`.
- `stringlist.h`: exact upstream, `b7fa331d0c378e2f532f3aa30c85c556190eecaf`.
- `expand.c`: appears exact upstream, `cf863cf36c65d1688c550a012c54ee792289d5c8`.
- `stringlist.c`: imported at `e8f8571b6ac303e99afacb9a303ce05311374572`, but currently NOT verbatim and must be corrected against upstream.
- `parser.c`: imported at `6e0951559488282093220e206579771fdd5e2f4e`, but currently NOT verbatim and must be corrected to exact upstream text/comments/formatting.
- `edit_gets.c`: source-derived provisional split at `543c929a60504e462252d2ca455582f1a02b94c2`. It omitted the SPB debugger hook, stripped comments, and hardcoded `"? "` in the Ctrl-R path. It needs to be brought much closer to the original `stdio.c` body, preserving comments and `PROMPT`, with only the minimum architecture/platform conditional around the SPB debugger hook.
- INF parser source inclusion: `0fef9873cc08464d1f57b26d160919067af5b4bc`.

Original `edit_gets()` supports Ctrl-N/P history, Ctrl-V quote, Ctrl-D/X/EOF handling, CR/LF, backspace/delete, Ctrl-E/A/F/B/K/R/U, tab->space, insertion, `!!`, history, and raw-mode ioctl behavior.

Original Ctrl-A includes the SPB debugger hook:

```c
case CTRL('A'):
    if (SPB->DebugBlock && !symmon) {
        debug("ring");
        continue;
    }
    while (bufc > buf) {
        putchar(CTRL('H'));
        bufc--;
    }
    break;
```

## Parser dependencies

Original `parser.c` includes:

```text
<sys/debug.h>
<saioctl.h>
<parser.h>
<setjmp.h>
<stringlist.h>
<ctype.h>
<arcs/io.h>
<arcs/errno.h>
<libsc.h>
<libsc_internal.h>
```

It consumes real `setjmp`/`longjmp` behavior because `expand.c` parse errors jump through `perr_buf`.

Other dependencies include `ioctl(StandardIn,TIOCSETXOFF,1)`, `printf`, `edit_gets`, `_argvize`, `expand`, `getenv`, `setenv`, `environ`, `Execute`, `ENOENT`, `arcs_strerror`, `strcmp`, `isspace`, `ASSERT`, `atob_ptr`, `atobu`/`atobu_L`, SGI `LONG`, and pointer-sized SGI integer types.

Core parser shape remains original:

```c
void
command_parser(struct cmd_table *cmd_table, char *prompt, int search_path,
    struct cmd_table *parent_table)
{
    register struct cmd_table *ct;
    static struct edit_linebuf elinebuf;
    char *linebuf;
    struct string_list argv;
    LONG err;
    int argc;

    for (;;) {
        setjmp(perr_buf);
        ioctl(StandardIn,TIOCSETXOFF,1);
        printf("%s", prompt);
        if ((linebuf = edit_gets(&elinebuf)) == 0)
            return;
        argc = _argvize(expand(linebuf, 0), &argv);
        if (argc == 0)
            continue;
        ...
    }
}
```

## First Actions failure and compatibility work

A first useful CI failure from run #10 / job `102173106071` was:

```text
parser.h:12:10: fatal error: sgidefs.h: No such file or directory
```

Compatibility work then added:

`arcs/sgisrc/include/sgidefs.h`
- Commit `f2fd8bd251720d7aa0dfd30aca901bdb0a4b0892`.
- Compatibility header for SGI ABI names on native x86-64 UEFI.
- Intentionally not a verbatim MIPS header. Pointer-sized types map to X64 semantics.
- Watch for typedef collisions with EDK headers.

`arcs/uefi/include/saioctl.h`
- Commit `bc00e486b2665c8560ca2a4334c5576e4b6c7efb`.
- Replaced empty placeholder with exact original SGI standalone header.
- Original declarations/`CTRL` are under `#ifdef LANGUAGE_C`; build currently passes `_LANGUAGE_C`, so investigate before changing global build flags.

`arcs/uefi/include/arcs/io.h`
- Commit `131da6211b201fa00f775ef98fb7b1a8f9aefea3`.
- Current hand-written compatibility subset defines `StandardIn`, `StandardOut`, pathname limits, SGI scalar names, and `Execute` declaration.
- This needs further type-source auditing; avoid ad hoc collisions. In particular the current `typedef VOID VOID;` is suspect/redundant and should be validated rather than preserved blindly.

`arcs/uefi/include/sys/debug.h`
- Commit `cd00aba813910d09beca75d79aa4043c79779901`.
- Minimal standalone compatibility preserving the original non-DEBUG `ASSERT(x)` no-op semantics and avoiding MIPS/kernel-only baggage.

`arcs/uefi/include/setjmp.h`
- Commit `08480c3492030878249a7477e1a426642a3b5ce8`.
- Maps original parser `setjmp`/`longjmp` contract onto EDK II BaseLib `SetJump`/`LongJump`.
- Verify exact EDK `LongJump` prototype/semantics and alignment requirements.

`arcs/uefi/ArcsGfxBringup.inf`
- Commit `ec3c0292fc6a61bc3f35004494c113c46b175ffb`.
- Added `BaseLib` for parser control flow.

CI policy: do not intentionally push an Actions run for every tiny include/one-line change. Batch substantial work before treating a new Actions run as the useful compile/link checkpoint. GitHub contents API writes themselves may create commits and trigger workflows, so minimize tiny sequential writes.

# 8. Key Command Monitor discovery: original SGI textport is the output path

This is the most important current architectural discovery.

Original `panel.c`:

```c
void
p_clear(void)
{
    if ( isgraphic(1) )
        (void)puts("\033[2J\033[H");
    p_offset(0);
}

void
p_textmode(void)
{
    if ( isgraphic(1) )
        (void)puts("\033[32h");

    p_offset(0);
}
```

Therefore the Command Monitor is not supposed to dump text through UEFI ConOut. The original graphical host textport in `stand/arcs/lib/libsk/graphics/htport.c` is the correct renderer.

Important original `htp_write()` behavior: when the textport is in GUI state and receives ordinary text, it calls `cleanGfxGui()`, resets the textport, processes text/control sequences, then paints through the SGI textport renderer.

It handles backspace, bell, LF/CR, tab, CSI erase/control sequences such as the `\033[2J` used by `p_clear()`, and draws the original shaded textport/border/optional Done button.

Correct architecture:

```text
menu_manualmode()
    -> p_textmode()
    -> p_clear()
    -> original printf()
    -> ARCS Write(StandardOut,...)
    -> original htp_write()
    -> original SGI graphical textport
    -> command_parser(..., ">> ", ...)
```

Original `stdio.c` confirms that standalone `putchar()`/buffered `printf()` ultimately write through ARCS `Write(1, ...)`.

Therefore the faithful UEFI boundary is to implement ARCS `Write(StandardOut,...)` so it feeds the SGI `htp_write()` renderer, and implement ARCS `Read(StandardIn,...)`/`getc` from UEFI keyboard input. Do NOT bypass this with UEFI ConOut.

Current repo already contains `arcs/sgisrc/libsc/gui/tport.c` with source-derived SGI pieces including `drawShadedBackground()` and `txOutcharmap()`, plus `stand_htport.h` and original fonts. However, full `htport.c` owns a static `hosttextport`/global `htp`, while the current UEFI GUI initializes `Bringup.Htp`:

```text
ArcsGfxGuiBringupInit(&Bringup, Gop, Fonts)
initGfxGui(&Bringup.Htp)
drawBackground()
```

Do not blindly import full `htport.c` and accidentally create a second independent textport state. The next task is to preserve original `htp_write`/tx control flow while binding it to the already-active UEFI `Bringup.Htp` through the narrowest possible platform boundary.

# 9. Immediate Command Monitor next steps

Before claiming a real monitor:

1. Correct `parser.c` to exact upstream source text/comments/formatting.
2. Correct `stringlist.c` to exact upstream.
3. Verify `expand.c` and `stringlist.h` remain exact.
4. Rework `edit_gets.c` from the exact original `stdio.c` body, preserving comments and `PROMPT`; conditionalize only the unavailable SPB debugger hook.
5. Audit original definitions of `LONG`, `ULONG`, `CHAR`, `UCHAR`, `VOID` and current include landscape before extending the hand-written `arcs/io.h` subset.
6. Import/source-port `arcs/errno.h` sufficiently faithfully for `ENOENT` and parser behavior.
7. Locate/implement required libsc primitives: `environ`, `arcs_strerror`, `atob_ptr`, `atobu`, `printf`, `putchar`, `getc`, `bcopy`, `strcmp`, `ioctl`, `EnterInteractiveMode`, `Write`, `Read`, `isgraphic` without duplicating symbols already provided by imported SGI code.
8. Port the original textport engine around the existing active Htp state, not a second renderer.
9. Implement ARCS `Write(StandardOut,...) -> htp_write(...)`.
10. Implement ARCS `Read(StandardIn,...)` through UEFI keyboard while leaving original parser/editor code intact.
11. Implement only the source-backed/minimum `ioctl` semantics required by the standalone editor/parser (`TIOCRAW`, `TIOCSETXOFF`, etc.).
12. Import/source-port `panel.c` pieces, exact `menu_manualmode()`, original command table, and original command implementations.
13. First visible `>>`, `help`, `printenv`, `setenv`, `exit`, etc. must come from genuine SGI code.

Expected visible milestone:

```text
Welcome to Reality

Command Monitor. Type "exit" or click on "done" to return to the menu.

>>
```

# 10. Future Linux boot/console and graphical bringup

After the Command Monitor/textport is faithful, Linux support becomes the next major phase.

The intended Linux verbose-console architecture is conceptually:

```text
Linux console output
        |
        v
Linux-side console/presentation boundary
        |
        v
SGI textport-compatible renderer
        |
        v
GOP/framebuffer state inherited from ARCS
```

Important boundary: after `ExitBootServices()`, Linux cannot keep calling UEFI ARCS code. Anything surviving into the kernel must be implemented on the Linux side or handed across as immutable state/data. Do not hack around this firmware lifecycle.

The goal is not merely to put an SGI-themed Plymouth image over Linux. The graphical Linux bringup should be derived from the actual IRIX graphical startup behavior/assets/source where available.

Desired eventual behavior:

```text
normal boot:
ARCS GUI -> IRIX-style graphical Linux bringup -> MaXX/IID

verbose boot:
ARCS GUI -> SGI-style textport -> Linux console -> MaXX/IID

boot failure:
ARCS GUI -> graphical startup -> SGI-style diagnostic/text display
```

The ideal experience is that the machine visually behaves like it is booting IRIX from power-on until MaXX/IID appears, despite UEFI/Linux/systemd underneath.

The user's main PC is available as a generic physical UEFI/Linux test target. Hardware matrix:

```text
QEMU
  -> fast development/debugging

Main PC
  -> generic real UEFI hardware
  -> eventual Linux boot/console/graphical bringup

MacBookPro8,x
  -> Apple EFI compatibility
  -> HFS+/Snow Leopard
  -> physical firmware compatibility torture test
```

# 11. Visible firmware personality / future firmware experiments

A longer-term idea is to make ARCS the first visible firmware UI. On ordinary proprietary UEFI hardware, the safe approach is to suppress vendor splash where firmware settings permit and install ARCS as the default/fallback EFI application (`EFI/BOOT/BOOTX64.EFI`), rather than modifying/flashing proprietary firmware.

On open firmware stacks such as coreboot/TianoCore, deeper integration could eventually make the SGI ARCS interface the actual human-facing firmware presentation while retaining modern firmware services underneath.

Do not make risky proprietary firmware flashing part of the normal project/install path.

# 12. PowerPC/Open Firmware idea and available hardware

The user owns two New World PowerPC Macs:

- iBook G3 Snow
- Power Mac G4 MDD, 2003 model

This creates a plausible future sibling backend, tentatively `arcs-openfirmware`, rather than another fork of the SGI upper layers.

Conceptual structure:

```text
                    original SGI ARCS/PROM
                           |
                  shared SGI code/UI
                    /             \
                   /               \
          x86-64 UEFI          PowerPC OF
              |                    |
          arcs-uefi        arcs-openfirmware
```

The Open Firmware backend could map ARCS component/device operations onto IEEE 1275/Open Firmware packages, device-tree nodes, and client-interface methods. Exact mappings must be based on the SGI source plus the actual Apple OF implementation, not assumed from conceptual similarity.

A safe first bringup would be an OF client/boot program loaded manually from the Open Firmware prompt, avoiding permanent BootROM modification. Initial target could simply initialize display/input and show the SGI graphical PROM. Later stages could add OF-backed device enumeration, disks/filesystems, environment, and boot/Execute behavior.

The iBook G3 Snow is the best first graphical target because its built-in panel removes GPU/adapter/monitor variables. The MDD can be the second-generation compatibility target.

An eventual PowerPC Linux path could mirror the x86-64 architecture:

```text
PowerPC Mac
  -> Open Firmware
  -> ARCS
  -> Linux/PPC
  -> IRIX-style graphical bringup
  -> MaXX/IID
```

# 13. Power Mac G4 MDD display issue

The MDD itself successfully starts and apparently boots an installed Mac OS (version not yet identified). Its video card is functional because it has produced a correct picture on the user's TV.

The user's normal monitor remains blank from boot with this video card, strongly suggesting a timing/mode/EDID/DDC compatibility problem rather than a dead PowerMac or dead GPU.

The TV is modern and has no DVI/VGA input. The user's DVI-to-HDMI adapter, previously used to make the TV path work, is currently lost. This prevents easy visual Open Firmware diagnosis for the moment.

Once a working display path is restored, useful Open Firmware diagnostics include inspecting the actual display package (`dev screen`, `.properties`, `words`, `devalias screen`, `printenv output-device`) before attempting card-specific mode-setting commands. There is no assumption that every Apple OF graphics FCode driver implements the same resolution-setting word.

A PRAM/NVRAM reset is a reasonable non-destructive blind test for stale display mode state. Avoid blindly changing `output-device` or invoking undocumented GPU FCode methods without a known-good display path.

# 14. Current branch/promotion policy

Generic non-Mac-specific changes developed while on `arcs-macbook` should be promoted back to `arcs-uefi`.

Keep Mac-specific on `arcs-macbook`:

```text
MacBook Actions workflow/artifact
Snow Leopard disk0s2 / boot.efi target
future Apple EFI quirks
future MacBook trackpad/input quirks
```

Promote to `arcs-uefi`:

```text
source-faithful SGI parser/editor imports
ABI compatibility needed by generic UEFI ARCS
SGI textport integration
ARCS Read/Write/ioctl compatibility
original Command Monitor
source-backed generic autoboot/dialog fixes
generic Execute/child-return fixes
eventual Linux boot/console work
```

Do not collapse the branches by blindly moving `arcs-uefi` to the MacBook tip.

# 15. Known-good state that must not regress

- SGI graphical background and five-item maintenance UI render correctly.
- Original cursor assets/hotspot and GOP software cursor work.
- Keyboard works.
- Original SGI startup/autoboot policy is in control.
- Escape can stop autoboot and expose maintenance menu.
- Generic UEFI `Execute` chainloading works.
- Child-return graphics restoration works.
- FreeBSD EFI loader has been reached through ARCS.
- Physical Apple EFI launches ARCS.
- Apple HFS+ system volume is visible through EFI SimpleFileSystem.
- ARCS finds Snow Leopard `disk0s2` `System/Library/CoreServices/boot.efi`.
- Snow Leopard successfully boots through ARCS on the Sandy Bridge MacBook.
- ARCS does not write EFI NVRAM.

# 16. Chat continuity procedure

This file is now the failsafe for chat-lock continuity.

At meaningful milestones, update this log on `main` with:

- what changed;
- relevant branch/commit IDs;
- source/binary evidence behind important decisions;
- tests performed and their results;
- regressions/TODOs;
- any old design decision that has been superseded;
- the exact next work item.

The purpose is not to replace Git history. It records the reasoning, recovered source behavior, hardware observations, and project constraints that are otherwise easy to lose when a long ChatGPT conversation locks.

## Current exact next work item

Continue the genuine SGI Command Monitor port. First clean up source-fidelity debt in `parser.c`, `stringlist.c`, and `edit_gets.c`; audit SGI scalar/type definitions and parser dependencies; then integrate the original SGI host textport with the existing active UEFI Htp state and route ARCS `Write(StandardOut,...)` through `htp_write()`. Add UEFI-backed ARCS keyboard `Read`/`getc`/minimal standalone `ioctl` below the unchanged SGI parser/editor. Do not start Linux console work until the real SGI `>>` path is working.