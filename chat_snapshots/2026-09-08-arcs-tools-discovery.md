# ARCS/x86-64 handoff: SGI host tooling discovery

Date: 2026-09-08
Branch: `arcs-uefi`
Project rule: port first, reconstruct only where necessary. Original SGI source, binaries, generated data, and build behavior are primary references.

## Why this snapshot exists

This chat uncovered a large amount of previously-unaccounted-for SGI host-side firmware tooling in the original IRIX 6.5.7m source. This is important enough that a locked-chat recovery must preserve it.

The major realization is that the source tree is not just the ARCS firmware sources. SGI shipped a substantial firmware-development SDK/toolchain under:

- `stand/arcs/scripts/`
- `stand/arcs/tools/`

We should prefer porting and using these original tools instead of continuing to create replacement conversion/build glue.

A detailed inventory was added at:

`research/arcs-host-tooling-inventory.md`

Commit for that inventory:

`315b32461a63f3938c54ca6b2609bf5fd0757257`

## Immediate context before the discovery

The X64 PROM product logo was being changed so the GraVT artwork would naturally enter the original SGI large-logo/REALITY-style layout path in `drawBackground()`.

Important layout fact:

```c
if (htp->clogow > 600) {
    /* large logo / centered layout path */
}
```

The desired GraVT raster is now 256 px tall with aspect ratio preserved, yielding 781 x 256. This width is safely above the `> 600` threshold.

Prior mistaken attempts included making the artwork 600 px tall; that was corrected. Do not go back to the 600px-tall asset.

Current desired geometry:

```text
clogo_w = 781
clogo_h = 256
```

## Key source discovery: original calligraphy converter

Original file:

`stand/arcs/scripts/graytospans.c`

It is SGI's own converter from a grayscale SGI image to the exact run-length span format used by `drawCalligraphy()`.

It emits:

```c
unsigned char clogo_data[] = { ... };
int clogo_w = ...;
int clogo_h = ...;
```

Format behavior recovered directly from source:

- grayscale threshold: `buf[x] > 128`
- initial run color state: 1
- run lengths stored as one-byte values
- a 255-pixel run is emitted as 255, 0, then continuation, preserving alternating color parity
- rows are read with SGI `libimage` `getrow()`

This means our GraVT asset conversion should ultimately use the original SGI tool rather than a hand-written Python equivalent.

## `stand/arcs/scripts/` inventory

Files discovered:

```text
Makefile
bdftohtp.c
frompcbm
fromspans
gconf
graytospans.c
graytoxbm.c
images/
topcbm.c
xbmtohtpbm.c
xbmtospans.c
```

The original Makefile builds:

```text
xbmtohtpbm
bdftohtp
graytospans
topcbm
graytoxbm
xbmtospans
```

and links image/X11 tooling with:

```text
-limage -lXmu
```

### Important individual tools

`graytospans.c`
- SGI grayscale image -> `clogo_data[]` span stream.
- Canonical product-logo/calligraphy converter.

`graytoxbm.c`
- SGI grayscale image -> XBM.
- Explicitly walks rows from `ysize - 1` down to 0, useful evidence about coordinate orientation.

`xbmtospans.c`
- XBM -> `clogo_data[]`.
- Uses Xmu.
- Its `getrowbits()` explicitly indexes the X bitmap using `image.height - yoff - 1`, so SGI already had an orientation adapter in this asset path.

`xbmtohtpbm.c`
- XBM -> host textport bitmap format.
- Reverses bit/nibble ordering and vertical row order while packing `unsigned short` output.

`bdftohtp.c`
- BDF font -> host textport font tables.
- Proves textport fonts are generated assets with an original SGI converter.

`topcbm.c`
- SGI `.rgb` artwork -> PROM color bitmap (`pcbm`) source.
- Emits per-color bitmaps and `pcbm` tables.
- Contains explicit SGI PROM palette names and machine-specific colors for systems including Indigo2, Indy, and SR.

`fromspans`
- Reverse-converts span headers back into SGI image data.
- Generates/compiles a temporary C program and writes through `libimage`.

`frompcbm`
- Reverse-converts PROM `pcbm` headers to SGI `.rgb` images.
- Gives us a round-trip validation tool for PROM GUI assets.

`gconf`
- Generates ARCS standalone `libsk` configuration tables.
- Handles devices, parent topology, filesystems, network interfaces, graphics probe functions, and tuneables.
- Directives include `node:`, `node!`, `fs:`, `fs!`, `if:`, `if!`, `tune:`.
- This may be the correct architecture for generating the X64 standalone configuration table later instead of hand-building it.

### Original source artwork discovered

Directory:

`stand/arcs/scripts/images/`

contains:

```text
indigo.bw
reality.bw
```

This is extremely important. The actual original REALITY source artwork is in the tree.

We can use:

```text
reality.bw
 -> original/ported graytospans
 -> generated clogo_data
 -> compare against original REALITY span data
```

as a very strong regression/correctness test for a modern-host port of SGI `libimage` + `graytospans`.

## `stand/arcs/tools/` discovery

Top-level directories found:

```text
convert/
dataio/
eawrite/
elf2coff/
hwreg/
myacc/
promcvt/
promgen/
promice/
squash/
```

The top-level original Makefile defaults to these subdirectories:

```text
convert promcvt promgen hwreg myacc
```

The other directories are still present and must be documented/understood, but their omission from default `SUBDIRS` suggests they may be legacy, machine-specific, auxiliary, install-time, or otherwise non-default.

### `tools/convert/`

Observed modules include:

```text
binary.c/.h
convert.c/.h
elf.c/.h
intel_hex.c/.h
mips.c/.h
pure.c/.h
```

This appears to be a generalized executable/image conversion framework supporting ELF, raw/binary, Intel HEX, MIPS-specific handling, and pure/raw representations.

This needs a dedicated command-line/input-output analysis.

### `tools/promgen/`

Files:

```text
Makefile
promgen.c
dozip.c
dozip.h
s3.c
s3.h
```

`promgen.c` is substantial. `dozip` implies compression support and `s3` implies Motorola S3/S-record output/input handling.

Likely one major stage in building real PROM images.

### `tools/promcvt/`

Files:

```text
Makefile
promcvt.c
promhdr.h
rle.c
rle.h
s3.c
s3.h
```

Another substantial PROM conversion/packaging path with its own PROM header format, RLE implementation, and S-record handling.

Need to determine relationship to `promgen` and which machine families/builds use each.

### `tools/hwreg/`

Observed:

```text
Makefile
main.c
```

`main.c` is about 22 KB. The name strongly suggests register inspection/manipulation, but exact semantics should be recovered from source before asserting more.

### `tools/myacc/`

Observed:

```text
Makefile
README
dextern
y1.c
y2.c
y3.c
y4.c
yaccpar
```

This looks like a bundled/custom yacc implementation or yacc-derived parser generator. Need to read its README and find build references.

### `tools/dataio/`

Observed:

```text
Makefile
autolabel.c
devicecodes
nprom.1
nprom.c
small_label.c
```

`nprom.c` is very large (~70 KB) and comes with a manual page. This looks like significant PROM programming/data-I/O tooling.

Best first analysis step: read `nprom.1` before implementation.

### `tools/eawrite/`

Observed:

```text
Makefile
eawrite.c
eawrite.50
libasm.s
```

Likely machine-specific and/or low-level. Exact semantics not yet established.

### `tools/elf2coff/`

Observed:

```text
Makefile
elf2coff.c
```

Nominal purpose is ELF -> COFF conversion. Need exact supported ELF/COFF dialects and build references.

### `tools/promice/`

This is a huge PROM ICE/programming/debugging environment, not a tiny helper.

Observed file families include:

```text
loadice.*
piconfig.*
picore.c
pidata.h
pidev.h
pidriver.*
piedit.c
pierror.*
pifile.*
piglob.c
pihelp.h
pikey.h
pimap.c
piscript.h
pishell.c
pistruct.h
pisyn.*
piunix.c
piutil.c
rvconfig.h
rvcore.c
rvedit.c
rvhelp.h
rvscript.h
rvshell.c
rvstruct.h
rvsynf.c
rvutil.c
rvxcpu.c
...and more
```

There is also a large `loadice.uu` payload.

This directory needs its own archaeology document. Do not assume `PI`/`RV` meanings until source comments/README/build references prove them.

### `tools/squash/`

Observed:

```text
Makefile
squash.c
```

Likely some form of image/binary compaction, but exact purpose not yet established. Inspect before naming semantics.

## Updated project direction caused by this discovery

Before this discovery, some build/asset functionality was being recreated manually around the pristine firmware source.

New rule for host-side build support:

```text
original SGI host tool exists
 -> port/build it first
 -> verify against original input/output
 -> integrate it into X64 build pipeline
```

Only write a replacement when the original tool cannot reasonably be made usable.

## Immediate next archaeology/build steps

1. Recover and build SGI `libimage` or the smallest source subset needed by the image tools.
2. Build original `graytospans` on modern Linux with minimal host-port changes.
3. Run original `reality.bw` through it and compare generated spans with original REALITY data.
4. Use the same original tool for the 781x256 GraVT raster/source pipeline.
5. Build/test `xbmtospans`, `bdftohtp`, and `topcbm`.
6. Restore `fromspans`/`frompcbm` for round-trip asset validation.
7. Analyze `gconf` and find its original input files in machine PROM directories.
8. Fully inventory command-line behavior and build references for every directory under `stand/arcs/tools/`.
9. Map `promgen`, `promcvt`, `convert`, `elf2coff`, and `squash` into the original PROM image build graph.
10. Treat `promice`, `dataio`, `hwreg`, `eawrite`, and `myacc` as separate archaeology targets rather than ignoring them as irrelevant leftovers.

## Current unresolved runtime issue (do not lose)

The pristine textport migration is not fully correct yet when entering Command Monitor.

Observed screenshot behavior before this tooling detour:

- Command Monitor appears over/inside a black region while GUI remnants remain visible.
- escape sequence text such as `[32h [2J [H` is visibly leaking instead of being fully interpreted.
- prompt and command input work.

Expected original sequence should route through pristine `htp_write` and consume the control sequences. Do not revert to UEFI `ConOut` as a workaround.

This remains an engineering issue after the logo/tooling work.

## Current important ARCS/X64 invariants

- Generic branch: `arcs-uefi`.
- MacBook-specific validation remains separate on `arcs-macbook`.
- No ARCS environment writes to UEFI NVRAM.
- No RuntimeServices interception.
- No fake Command Monitor.
- Prefer pristine textport over UEFI console output.
- Do not make broad graphics/autoboot/Execute refactors while fixing textport/tooling.
- Child-return fixes should stay return-path-only.
- Original binaries/source/symbols remain primary evidence.
- Custom GraVT product artwork is intentional and should remain.
- GraVT wordmark artwork itself is fine; recent visual discussion concerned surrounding placement/composition, not redesigning the wordmark.
- Current target logo geometry is 781 x 256 so it enters the original `clogow > 600` layout path.

## Important recent commits/state

Previously known textport/logo work includes:

```text
82fe7d1a  Restore original SGI textport palette initialization order
833485b1  Correct X64 PROM splash row orientation
6c196896  Prepare X64 splash for SGI coordinate orientation
95245c29  Build X64 splash orientation adapter
```

A mistaken 600px-tall logo path was created later and should not be treated as the desired final geometry.

The desired logo geometry is 781x256.

Detailed tooling inventory commit:

```text
315b32461a63f3938c54ca6b2609bf5fd0757257
```

## Recovery instruction

If a future chat locks, start by reading:

1. `chat_snapshots/2026-09-08-arcs-tools-discovery.md`
2. `research/arcs-host-tooling-inventory.md`
3. current `arcs-uefi` branch history after commit `315b32461a63f3938c54ca6b2609bf5fd0757257`

Then continue with original SGI host-tool recovery, beginning with `libimage` + `graytospans`, unless a newer snapshot supersedes this one.
