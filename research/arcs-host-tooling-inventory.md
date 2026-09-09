# SGI ARCS Host Tooling Inventory

Status: discovery/inventory pass, 2026-09-08
Source baseline: original IRIX 6.5.7m source tree, primarily `stand/arcs/scripts/` and `stand/arcs/tools/`.
Target branch for this documentation: `arcs-uefi`.

## Why this matters

The ARCS source tree contains much more than firmware C/assembly sources. SGI also shipped a substantial host-side firmware engineering toolchain. That toolchain covers asset conversion, textport font generation, standalone configuration generation, executable/image conversion, PROM image generation, PROM conversion/compression, hardware-register tooling, PROM programming/debugging utilities, and related support programs.

For the x86-64 UEFI ARCS port, the rule should be:

1. Use the original SGI host tool if practical.
2. If it does not build on a modern host, port only the host/platform boundary.
3. Preserve input/output formats and generated data exactly where possible.
4. Reimplement a tool only when the original cannot reasonably be made usable.
5. Validate ported host tools against known SGI source assets and known generated output whenever possible.

This is the same project philosophy as the firmware itself: port first, reconstruct only where necessary.

## `stand/arcs/scripts/`

The directory contains:

- `Makefile`
- `bdftohtp.c`
- `frompcbm`
- `fromspans`
- `gconf`
- `graytospans.c`
- `graytoxbm.c`
- `images/`
- `topcbm.c`
- `xbmtohtpbm.c`
- `xbmtospans.c`

The original Makefile builds these host tools:

- `xbmtohtpbm`
- `bdftohtp`
- `graytospans`
- `topcbm`
- `graytoxbm`
- `xbmtospans`

Original link dependencies include `-limage` and `-lXmu`, with GL include paths.

### `graytospans.c`

Purpose: convert an SGI grayscale image (`.bw`/single channel image read through `libimage`) into the PROM calligraphy run-length span stream.

Output symbols:

```c
unsigned char clogo_data[] = { ... };
int clogo_w = ...;
int clogo_h = ...;
```

Important format details:

- threshold is `buf[x] > 128`
- run stream starts with color state 1
- run lengths are one byte
- 255-pixel runs are split with an inserted zero-length run so color parity is preserved
- rows are emitted in source image row order

This is the canonical generator for the calligraphy/logo data consumed by pristine `drawCalligraphy()`.

### `graytoxbm.c`

Purpose: convert SGI grayscale image data to X bitmap source.

Important detail: it walks image rows from `ysize - 1` down to 0. This is useful evidence for SGI image/textport coordinate conventions.

### `xbmtospans.c`

Purpose: convert X bitmap files to the same `clogo_data[]` span format.

Dependencies:

- X11
- Xmu bitmap reader

Important detail: `getrowbits()` indexes the XBM backing store using:

```c
image.height - yoff - 1
```

so the converter deliberately adapts X bitmap row orientation before generating spans.

### `xbmtohtpbm.c`

Purpose: convert XBM source into the host textport bitmap representation.

Observed behavior:

- parses XBM width/height
- reverses nibble/bit ordering while reading bytes
- packs output into `unsigned short`
- emits rows in reverse vertical order

This is likely relevant to textport and GUI bitmaps beyond the product logo.

### `bdftohtp.c`

Purpose: convert BDF fonts into SGI host textport font data.

It parses BDF metadata and glyph records including:

- `ENCODING`
- `SWIDTH`
- `DWIDTH`
- `BBX`
- `BITMAP`

This means the host textport font tables are generated assets, not necessarily hand-authored firmware constants.

### `topcbm.c`

Purpose: convert SGI `.rgb` images to PROM color bitmap (`pcbm`) source.

It emits per-color bitmaps and table structures for the PROM GUI. It recognizes standard PROM palette entries including:

- BLACK/RED/GREEN/YELLOW/BLUE/MAGENTA/CYAN/WHITE
- secondary color entries (`BLACK2`, `RED2`, etc.)
- `VeryDarkGray`, `MediumGray`, `VeryLightGray`
- `TP_IDX`
- `MACHCLR0` through machine-color entries

The source explicitly contains machine-specific color values labeled for systems such as Indigo2, Indy, and SR.

This is directly relevant to the original PROM GUI icon/artwork asset pipeline.

### `fromspans`

Purpose: reverse-convert a span header back into an SGI image.

Mechanism:

- generates a temporary C program
- includes the span header
- expands `clogo_data[]`
- writes an SGI image through `libimage`

This gives us a round-trip validation path for calligraphy assets.

### `frompcbm`

Purpose: reverse-convert PROM color bitmap headers back into SGI `.rgb` images.

It generates and compiles a temporary C program, reconstructs color planes from the `pcbm` nodes, and writes an image with `libimage`.

This gives us a round-trip validation path for PROM GUI color bitmaps.

### `gconf`

Purpose: generate ARCS standalone `libsk` configuration tables.

The source describes itself as:

`gconf - generate config table for ARCS standalone libsk programs.`

It generates built-in tables for:

- device/install topology
- filesystems
- network interfaces
- tuneable variables

Input directives include:

- `node:`
- `node!`
- `fs:`
- `fs!`
- `if:`
- `if!`
- `tune:`

It can emit driver install routine references, parent relationships, graphics probe functions, driver-info entries, filesystem installers, interface tables, and compile-time exclusions.

For the X64/UEFI port, `gconf` may be the correct way to preserve the original configuration-generation architecture instead of hand-building portions of the standalone configuration table.

### `scripts/images/`

Known original source assets:

- `indigo.bw`
- `reality.bw`

This is especially valuable because `reality.bw` can be passed through the original `graytospans` and compared against known REALITY calligraphy data. That gives a strong correctness test for any modern-host port of SGI `libimage`/`graytospans`.

## `stand/arcs/tools/`

Top-level directories present:

- `convert/`
- `dataio/`
- `eawrite/`
- `elf2coff/`
- `hwreg/`
- `myacc/`
- `promcvt/`
- `promgen/`
- `promice/`
- `squash/`

The top-level `stand/arcs/tools/Makefile` actively builds only:

- `convert`
- `promcvt`
- `promgen`
- `hwreg`
- `myacc`

This strongly suggests the remaining directories are auxiliary, legacy, machine-specific, install-time, or otherwise not part of the default generic standalone-tools build. They still need to be preserved and understood.

## `tools/convert/`

Observed files include modules for:

- binary/raw representation
- ELF input
- Intel HEX input/output
- MIPS handling
- pure/raw handling
- central `convert.c` framework

Known file names include:

- `binary.c`, `binary.h`
- `convert.c`, `convert.h`
- `elf.c`, `elf.h`
- `intel_hex.c`, `intel_hex.h`
- `mips.c`, `mips.h`
- `pure.c`, `pure.h`
- Makefile

Interpretation: this is a generalized standalone/PROM executable and image conversion framework rather than a single narrow utility.

TODO: inspect all modules and map exact supported format-to-format conversions and command-line syntax.

## `tools/promgen/`

Files:

- `Makefile`
- `promgen.c`
- `dozip.c`, `dozip.h`
- `s3.c`, `s3.h`

`promgen.c` is substantial and is clearly a major PROM image-generation utility. The presence of `dozip` indicates compression support, and `s3` indicates Motorola S-record/S3 handling.

TODO: document exact image layout, checksum/header behavior, compression stage, input executable assumptions, and output formats.

## `tools/promcvt/`

Files:

- `Makefile`
- `promcvt.c`
- `promhdr.h`
- `rle.c`, `rle.h`
- `s3.c`, `s3.h`

Interpretation: another PROM conversion/packaging path, with its own PROM header definition, RLE implementation, and S-record support.

TODO: determine relationship between `promcvt` and `promgen`, which machines/builds used each, and whether their output formats differ by PROM generation or device.

## `tools/hwreg/`

Files currently observed:

- `Makefile`
- `main.c`

`main.c` is large enough to warrant a dedicated analysis pass. The directory name strongly suggests hardware-register inspection/manipulation tooling.

TODO: inspect command set, supported hardware/register metadata, target/host execution assumptions, and whether any generated register definitions are reusable for archaeology.

## `tools/myacc/`

Files:

- `Makefile`
- `README`
- `dextern`
- `y1.c`
- `y2.c`
- `y3.c`
- `y4.c`
- `yaccpar`

This appears to be a yacc-derived or yacc-related parser-generator implementation bundled with the standalone toolchain. The naming and file layout strongly suggest an SGI-custom parser generator or local yacc copy used to avoid depending on a particular host yacc behavior.

TODO: read `README`, determine why it exists, and locate build references.

## `tools/dataio/`

Files:

- `Makefile`
- `autolabel.c`
- `devicecodes`
- `nprom.1`
- `nprom.c`
- `small_label.c`

`nprom.c` is very large (~70 KB) and has a manual page. This looks like substantial PROM programming/data I/O tooling, likely including device identification and media/programmer support.

TODO: inspect `nprom.1` first to recover intended user-facing semantics before reading implementation details.

## `tools/eawrite/`

Files:

- `Makefile`
- `eawrite.c`
- `eawrite.50`
- `libasm.s`

The presence of `libasm.s` and an `eawrite.50` binary-like/install artifact indicates this is more machine-specific than the generic host converters.

TODO: inspect source and Makefile to identify target hardware and whether the tool runs on host IRIX or standalone hardware.

## `tools/elf2coff/`

Files:

- `Makefile`
- `elf2coff.c`

Purpose is nominally clear from the name: ELF -> COFF conversion.

TODO: document accepted ELF class/endianness/architecture, COFF dialect, and where original PROM builds invoke it.

## `tools/promice/`

This is a large directory containing what appears to be a PROM ICE/programming/debug environment.

Observed files include:

- `Makefile`
- `loadice.c`
- `loadice.ini`
- `loadice.ini.lfast2`
- `loadice.uu`
- `piconfig.c`, `piconfig.h`
- `picore.c`
- `pidata.h`
- `pidev.h`
- `pidriver.c`, `pidriver.h`
- `piedit.c`
- `pierror.c`, `pierror.h`
- `pifile.c`, `pifile.h`
- `piglob.c`
- `pihelp.h`
- `pikey.h`
- `pimap.c`
- `piscript.h`
- `pishell.c`
- `pistruct.h`
- `pisyn.c`, `pisyn.h`, `pisynf.c`
- `piunix.c`
- `piutil.c`
- a parallel family of `rv*` files (`rvconfig.h`, `rvcore.c`, `rvedit.c`, `rvhelp.h`, `rvscript.h`, `rvshell.c`, `rvstruct.h`, `rvsynf.c`, `rvutil.c`, `rvxcpu.c`, etc.)

This is much larger than a simple programmer frontend and needs its own dedicated archaeology pass.

TODO: identify what "PI" and "RV" mean in this context, what external ICE/programmer it talks to, and whether `loadice.uu` is a bundled third-party/device binary.

## `tools/squash/`

Files:

- `Makefile`
- `squash.c`

Likely a small binary/image compaction utility, but do not assume semantics from the name alone.

TODO: inspect implementation and references before assigning exact purpose.

## Immediate build-port plan

Priority order for recovering original host tooling on modern Linux:

1. `scripts/graytospans` - immediate use for GraVT and REALITY validation.
2. `scripts/xbmtospans` - alternate span path with XBM and explicit orientation behavior.
3. `scripts/bdftohtp` - recover original textport font generation.
4. `scripts/topcbm` - recover original GUI color bitmap generation.
5. `scripts/fromspans` and `frompcbm` - enable round-trip correctness checks.
6. `scripts/gconf` - preserve original standalone configuration generation.
7. `tools/convert` - understand executable/image conversion architecture.
8. `tools/promgen` and `promcvt` - reconstruct the original PROM packaging pipeline.
9. `tools/elf2coff`, `squash`, `dataio`, `hwreg`, `promice`, `eawrite`, `myacc` - full archaeology and buildability assessment.

## Known dependency questions

- SGI `libimage` source/buildability is the main dependency for several image tools.
- `Xmu` is needed by XBM tooling.
- historical SGI make infrastructure (`commondefs`, `COMMONRULES`) should not be blindly replaced until the original build intent is understood.
- some tools may be target-specific rather than ordinary host utilities.
- some directories are absent from the top-level default `SUBDIRS`, which is evidence that they need classification before integration.

## Validation strategy

Use original source assets and generated data to prove ports correct:

- `reality.bw` -> original/ported `graytospans` -> compare span stream with original REALITY header/data.
- `indigo.bw` -> same comparison where matching generated data can be located.
- known BDF sources -> `bdftohtp` -> compare firmware font arrays.
- known `.rgb` GUI artwork -> `topcbm` -> compare `pcbm` headers.
- generated span/pcbm headers -> `fromspans`/`frompcbm` -> image round trip.

Do not replace original tool behavior with a modern convenience pipeline until these equivalence tests are exhausted.
