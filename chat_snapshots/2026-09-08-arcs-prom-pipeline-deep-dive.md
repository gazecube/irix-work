# ARCS/x86-64 handoff: PROM production pipeline deep dive

Date: 2026-09-08
Branch: `arcs-uefi`
Project rule: port first, reconstruct only where necessary. Original SGI source/build behavior remains primary evidence.

## Why this snapshot exists

A deeper archaeology pass through `stand/arcs/tools/` confirmed that the original IRIX 6.5.7m tree contains a real SGI firmware production toolchain. This is important enough to preserve separately from the earlier general tooling discovery snapshot.

The detailed source-grounded writeup is:

`research/arcs-prom-production-pipeline.md`

Commit:

`703f8cfac0939df3075b497d24a0218f8c52b059`

The earlier broader inventory remains:

`research/arcs-host-tooling-inventory.md`

## Major confirmed findings

### `stand/arcs/commondefs`

The shared ARCS build definitions explicitly define:

```make
CONVERT=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/convert/convert
PROMCVT=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/promcvt/promcvt
PROMGEN=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/promgen/promgen
ELF2COFF=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/elf2coff/elf2coff
```

So these are first-class intended standalone build tools.

### `promcvt`

- 1992 EVEREST/IO4 PROM packager.
- Reads SGI ELF executables.
- Builds EVEREST IO4 flash PROM images.
- Supports typed master/R4000/TFP/R10000/graphics segments.
- Emits binary or S3.
- Uses target-side IO4 segment-loader compression source via symlink.
- Confirmed production invocation in `IO4prom/segldr/Makefile` packages:

```text
master segment loader
+ R4000 EVEREST PROM
+ TFP EVEREST PROM
+ R10000 EVEREST PROM
```

into `io4prom.bin`.

### `promgen`

- 1995 later-generation PROM image generator.
- Source says it was rewritten from `promcvt` and `nextract`.
- Assembles a PROM header plus one or more named loadable segments.
- Supports MIPS ELF32 and ELF64 big-endian input.
- Extracts first `PT_LOAD`, entry, load address, file length, memory length.
- Supports binary and S3 output.
- Compression defaults on; gzip is the implemented compression method.
- Host compressor uses external `gzip` through `dozip.c`.
- Host verifier uses the original IP27 PROM `unzip.c` to prove generated compressed data can be decoded by the target implementation.

SN PROM format:

```text
PROM_MAGIC       = 0x4a464b535743534d
PROM_MAXSEGS     = 16
PROM_DATA_OFFSET = 0x1000
```

Each segment stores name, flags, offset, entry, load address, true/compressed lengths and sums, plus memory length.

Binary image layout:

```text
promhdr_t at offset 0
zero pad to 0x1000
payload 0
8-byte alignment
payload 1
8-byte alignment
...
```

`promgen -h` validates magic, checks payload sums, decompresses compressed segments, validates true length, and validates the uncompressed sum.

### Confirmed production use of `promgen`

`IO6prom/Makefile` explicitly says the `.img` image is used by `flashio6` and contains headers/code in compressed format. It invokes `promgen`, then immediately verifies with `promgen -h`.

`IP27prom/Makefile` uses `promgen` for CPU PROM `.img` images but passes `-U` to disable compression.

The IP27 technical reference manual has a developer-only section saying:

- IP27 builds and IRIX flash commands use `promgen` format
- extension is `.img`
- installed images live in `/usr/cpu/firmware`
- `promgen -h file.img` is the official image verification/version-inspection command

This confirms `promgen` is a deployed firmware image format, not an incidental build helper.

### `convert`

A generalized object/image converter derived from MIPS SPP and SGI-modified.

Input types include:

```text
MIPS object formats
Berkeley a.out
COFF
ELF
pure/raw binary
```

Output formats include:

```text
intelhex
step
srec
s1rec
s2rec
s3rec
stub
binary
pure
rprom
fprom
```

It supports byte-lane/halfword selection and machine-specific byte swizzling.

The source comments associate:

```text
-z -> EVEREST/IP19 ordering
-Z -> IP22/IP24-style ordering
```

### IP30 integration through `convert`

`convert/racer.c` defines:

```text
rprom = recovery PROM; validates/checks FPROM and jumps to it
fprom = full PROM; actual PROM functionality
```

It writes flash headers, checksums, version data, timestamps, and padding.

`IP30prom/Makefile` confirms the real build:

```make
${CONVERT} -f rprom -v ${RPROM_VERSION} ../RACER_RPROM.O/rprom > IP30prom.bin
cat fprom.bin >> IP30prom.bin
```

and external Intel HEX generation:

```make
${CONVERT} -Z -p -f intelhex IP30prom.bin > IP30prom.hxe
```

The Makefile notes that this byte orientation was for Solectron, while SGI local Data I/O programmers used custom burn scripts.

### `myacc`

The README confirms it is a modified Yacc created to generate modular parsers with parser state in a structure rather than globals.

Its explicit purpose was to build the IP27 PROM parser without global data, using stack-only state, and to allow multiple parsers in one application.

`IP27prom/Makefile` directly invokes:

```text
tools/myacc/myacc
```

on `parse.y` to generate `parse_y.tab.c/.h`.

This is highly relevant to the current pristine Command Monitor parser migration.

## Architectural consequence

The project should now be thought of as porting an SGI firmware development environment, not only an ARCS runtime binary.

Original flow recovered so far:

```text
source + generated config/parser/assets
        |
        v
linked standalone MIPS ELF
        |
        +-> promcvt -> EVEREST/IO4 flash image
        |
        +-> promgen -> SN0/IP27/IO6 .img
        |
        +-> convert -> Intel HEX/S-record/binary/IP30 RPROM-FPROM formats
```

For x86-64 UEFI, preserve the tool architecture and patch narrow architecture/host boundaries rather than replacing these tools wholesale.

## New host-tool recovery priority

1. `graytospans` + SGI image dependency, immediate GraVT/REALITY use.
2. `myacc`, regenerate/validate original IP27 parser.
3. `promgen`, reproduce known original `.img` output and verification.
4. `convert`, validate Intel HEX and IP30 RPROM packaging.
5. `promcvt`, reproduce EVEREST/IO4 packaging.
6. `gconf`, `bdftohtp`, `topcbm`, reverse asset tools.
7. `elf2coff`, `hwreg`, `dataio`, `promice`, `eawrite`, `squash` dedicated archaeology.

## Current unrelated-but-important runtime state

Do not lose the Command Monitor textport bug while pursuing tooling:

- prompt/input works
- GUI remnants remain when entering monitor
- literal escape content like `[32h [2J [H` leaks visibly
- expected path remains pristine `htp_write`; do not route back to UEFI `ConOut`

Current GraVT target remains 781x256 so `clogow > 600` selects the original large-logo layout path.

## Recovery order after a locked chat

Read, in order:

1. `chat_snapshots/2026-09-08-arcs-prom-pipeline-deep-dive.md`
2. `research/arcs-prom-production-pipeline.md`
3. `chat_snapshots/2026-09-08-arcs-tools-discovery.md`
4. `research/arcs-host-tooling-inventory.md`
5. current `arcs-uefi` history after commit `703f8cfac0939df3075b497d24a0218f8c52b059`

Then continue host-tool recovery/validation from original SGI sources.
