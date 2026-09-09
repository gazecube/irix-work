# ARCS/UEFI state before IRIS GL pivot

Date: 2026-09-08

This snapshot records the ARCS/UEFI work immediately before the user moved IRIS GL archaeology into a fresh chat. The ARCS work is intentionally paused, not abandoned.

## Current branch and project intent

Primary ARCS branch: `arcs-uefi`

Goal: a native x86-64 UEFI port of SGI ARCS/PROM using the original IRIX 6.5.7m standalone source wherever possible. The porting policy remains:

- port first, reconstruct only where necessary
- preserve original SGI names, behavior, assets, parsers, and build tooling
- replace MIPS/SGI-hardware boundaries with x86-64/UEFI boundaries
- do not replace original SGI logic with modern approximations when source or binary evidence exists

## Known-good ARCS GUI/textport state

The pristine SGI textport path is now driving the UEFI GOP backend. The important palette-order fix is in place: `txMap(htp)` is called after GOP attach and before `htp_init()`, matching original SGI initialization order.

Relevant known commits:

- `82fe7d1a24b50af091381a8dae56bed60369b250` - restore original SGI textport palette initialization order
- `833485b1ec169ddd97f8991705e167eaf7875ce8`
- `6c19689611de62898b2bc62133efae1f650cdeb4`
- `95245c29377d0e99ba751dafec41db83ebded2c5` - known-good pull-through before splash resizing work

The GUI is visibly rendering under QEMU with the original ARCS maintenance menu:

- Start System
- Install System Software
- Run Diagnostics
- Recover System
- Enter Command Monitor

The original widescreen background/layout logic is in use.

## Product splash

The custom GraVT product splash is still desired and should remain.

The SGI layout source in `stand/arcs/include/style.h` was recovered and explains the original product-logo positioning branches:

- `clogow > 600` -> centered/wide-logo layout
- `gfxWidth() <= APPROX1024` -> ~1024 layout
- otherwise -> normal widescreen fixed-coordinate layout

The current generated GraVT splash is 781x256, preserving aspect ratio while crossing the original `clogow > 600` threshold.

Relevant commits:

- `563105a0` - use 256px-tall GraVT PROM splash
- `86c29990` - allow wrapped GraVT splash data during decode

Current generated span data:

- `clogo_w = 781`
- `clogo_h = 256`
- decoded span size = 4482 bytes

The exact original `graytospans.c` run-length algorithm was reproduced. Long runs use the original `255,0` continuation convention.

The temporary base64 path is compatibility glue and should eventually disappear once the original SGI host-side image tooling is resurrected and directly generates `clogo_data[]`.

## Original SGI host tooling recovered

`stand/arcs/scripts/` contains the original build-time asset/config tools:

- `graytospans.c`
- `graytoxbm.c`
- `xbmtospans.c`
- `xbmtohtpbm.c`
- `bdftohtp.c`
- `topcbm.c`
- `fromspans`
- `frompcbm`
- `gconf`

`stand/arcs/scripts/images/` contains original SGI grayscale source artwork including:

- `indigo.bw`
- `reality.bw`

This gives a direct validation path for the image pipeline: original `.bw` -> original `graytospans` -> generated span header -> compare with shipped/generated product-logo data.

The `stand/arcs/tools/` tree was also inventoried and is effectively an SGI PROM engineering toolbox. Important recovered tools include:

- `promgen`
- `promcvt`
- `convert`
- `myacc`
- `elf2coff`
- `dataio`
- `eawrite`
- `hwreg`
- `promice`
- `squash`

The original ARCS common definitions reference these as first-class build tools.

### promgen

`promgen` is the later SGI PROM image packager. It:

- reads ELF
- supports multiple `PT_LOAD` segments
- writes SGI PROM image headers and segment tables
- supports compressed and uncompressed segments
- writes binary and Motorola S3 output
- can inspect/verify images with `-h`

It is used by real SGI PROM Makefiles such as IO6prom/IP27prom. Its current source is MIPS-ELF aware at the input-recognition boundary, but much of the container/image logic is generic enough that an x86-64 ELF input port should be surgical.

### promcvt

Older EVEREST/IO4 PROM packager used for older IO4/segment-loader image production.

### convert

General object/image conversion framework with ELF, binary, Intel HEX, MIPS, Racer/IP30 formats and production use in IP27/IP30 builds.

### myacc

SGI-modified yacc used to produce modular parser output with parser state in a structure instead of globals. IP27prom uses it for `parse.y`. This is directly relevant to preserving original parser behavior instead of hand-rewriting it.

Research docs already created:

- `research/arcs-host-tooling-inventory.md`
- `research/arcs-prom-production-pipeline.md`
- `chat_snapshots/2026-09-08-arcs-tools-discovery.md`
- `chat_snapshots/2026-09-08-arcs-prom-pipeline-deep-dive.md`

Known commits:

- `315b32461a63f3938c54ca6b2609bf5fd0757257`
- `8cf5b90dc8554710aa5c1214145dd7256f069a13`
- `703f8cfac0939df3075b497d24a0218f8c52b059`
- `77cc7dad1639ca5258493ff51a694b70802d744d`

## Command Monitor state

The Command Monitor currently reaches a working prompt, but text output is not fully canonical yet.

Observed issue: ANSI/control sequences appear literally, for example the command-monitor banner may show text equivalent to:

`[32h [2J [HCommand Monitor...`

The GUI remains partly visible behind it. This indicates the current output chain is still not fully interpreting original control sequences through the intended SGI textport path.

Do not fix this by replacing the monitor or routing StandardOut to UEFI ConOut. The target remains the original SGI command monitor and original SGI textport behavior.

The current monitor command table includes original/source-derived commands such as:

- `exit`
- `help`
- `.?`
- `printenv`
- `setenv`
- `unsetenv`

No ARCS NVRAM writes are allowed. Environment changes remain volatile.

## Known-good boot/Execute behavior prior to textport canonicalization

The following functionality was already working and should not be disturbed by broad refactors:

- original cursor behavior
- GOP save-under
- hover/click interaction
- original widescreen background
- keyboard input
- source-derived maintenance behavior
- source-derived autoboot
- Execute via UEFI `LoadImage`/`StartImage`
- child-return GOP restoration/redraw
- FreeBSD loader chaining
- Snow Leopard chaining

MacBook trackpad support remains unimplemented and should not be guessed at.

## Build command

Typical build:

```sh
cd ~/Projects/ARCS-work
git pull
WORKSPACE=$HOME/src/edk2 TOOLCHAIN=GCC TARGET=DEBUG ./arcs/build-edk2.sh
```

Expected EFI output:

`/home/nekron/src/edk2/Build/ArcsPkg/DEBUG_GCC/X64/ArcsGfxBringup.efi`

Clean build directory when needed:

```sh
rm -rf ~/src/edk2/Build/ArcsPkg
```

## Hard constraints to retain when ARCS work resumes

- no ARCS NVRAM writes
- no RuntimeServices interception
- no fake Command Monitor
- no UEFI ConOut when original textport can be used
- no broad graphics/autoboot/Execute refactors
- no speculative MacBook trackpad implementation
- no Linux console before faithful Command Monitor
- child-return fixes are return-path-only
- generic UEFI/x86-64 path first; MacBook-specific validation second
- keep the custom GraVT splash
- preserve original SGI source/tooling whenever available

## Pause point

ARCS work is intentionally paused here while a separate fresh chat begins focused IRIS GL archaeology/reconstruction. This ARCS chat should remain the canonical continuation point for firmware work when the user returns to it.
