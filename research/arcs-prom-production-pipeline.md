# SGI ARCS PROM Production Pipeline Deep Dive

Status: source-grounded archaeology pass, 2026-09-08
Source baseline: original IRIX 6.5.7m source tree.
Target branch: `arcs-uefi`.

## Executive summary

The original ARCS source tree contains a real SGI PROM production toolchain, not just firmware sources and a few incidental helpers.

The strongest confirmed pipeline components are:

- `stand/arcs/tools/convert/convert`
- `stand/arcs/tools/promcvt/promcvt`
- `stand/arcs/tools/promgen/promgen`
- `stand/arcs/tools/myacc/myacc`
- `stand/arcs/scripts/gconf`
- the asset-generation tools in `stand/arcs/scripts/`

The original `stand/arcs/commondefs` explicitly defines these as shared standalone build tools:

```make
CONVERT=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/convert/convert
PROMCVT=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/promcvt/promcvt
PROMGEN=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/promgen/promgen
ELF2COFF=$(TOOLROOTSAFE) $(WORKAREA)/stand/arcs/tools/elf2coff/elf2coff
```

This establishes that these programs are part of the intended ARCS build architecture.

For the x86-64 UEFI port, the immediate consequence is important: preserve and port the original host-side build graph wherever practical instead of replacing it with ad-hoc scripts.

## Tool lineage

### `promcvt` - older EVEREST/IO4 PROM packager

`promcvt.c` is dated September 26, 1992 and identifies itself as a tool that:

- reads a standard SGI ELF executable
- appends an EVEREST IO4 PROM header
- emits straight binary or Motorola S3 records
- produces input for IO4 PROM load routines

Its header format is EVEREST-specific and uses:

```c
#define PROM_MAGIC 0x4a464b34

typedef struct {
    uint prom_magic;
    uint prom_cksum;
    uint prom_startaddr;
    uint prom_length;
    uint prom_entry;
    uint prom_version;
} evpromhdr_t;
```

The command line supports multiple typed segments:

```text
-M master_file      master segment
-R file             R4000 big-endian segment
-r file             R4000 little-endian segment placeholder
-T file             TFP segment
-A file             R10000/Andes segment
-G file             graphics data segment
```

It also supports:

```text
-b                  binary output
-s                  S3 output
-v version          PROM version
-o file             output name
-O                  obsolete/old-style flash PROM image
-h image            inspect image header
-V                  verbose
```

Compression is not self-contained in the `promcvt` directory. Its Makefile symlinks in the original segment-loader compressor from:

```text
stand/arcs/IO4prom/segldr/compress.c
stand/arcs/IO4prom/segldr/lzw.h
```

and builds with `-D_STANDALONE -DDOING_COMPRESS`.

This proves that the host packager and target segment-loader compression format were intentionally paired.

### Confirmed original EVEREST build usage

`stand/arcs/IO4prom/segldr/Makefile` builds the IO4 flash image with:

```make
../../tools/promcvt/promcvt -v $(VERSION) -V -b -o ../io4prom.bin \
    -M ${PRODUCT}segldr -R ../EVEREST32prom -T ../BB_EVERESTprom \
    -A ../T5_EVERESTprom
```

The comments state that this image resides in the IO4 EPC flash EPROM and that the loader is called by IP19, IP21, or IP25 PROMs.

So `promcvt` is not speculative or unused. It is the original multi-architecture EVEREST/IO4 flash-image construction step.

## `promgen` - later segmented SN0/IO6 PROM image format

`promgen.c` is dated October 30, 1995.

Its own source says it was:

```text
Rewritten from John Kraft's promcvt
and Steve Whitney's nextract
```

and describes its purpose as:

```text
Assembles the image of a PROM consisting of a header
and one or more loadable segments.
```

Output can be:

```text
straight binary
Motorola S3 hex records
```

This is therefore a later, generalized segmented PROM image generator descended from `promcvt`.

### Input model

The command-line interface is explicitly segment-oriented:

```text
-N segname
-E elffile
```

Each `-E` consumes an ELF file and the preceding `-N` supplies the segment name.

Other relevant options are:

```text
-b                  binary output
-s                  S3 output
-C                  compression on (default)
-U                  compression off
-v vers.rev         version and revision
-o file             output file
-a s3_addr          alternate S3 load address
-h image            inspect and verify a promgen image
-V / -Q             verbose on/off
```

### ELF assumptions

`promgen` explicitly accepts MIPS ELF32 or MIPS ELF64, big-endian only.

The source validates the ELF identification against:

```text
ELFCLASS32 + ELFDATA2MSB
ELFCLASS64 + ELFDATA2MSB
```

It then finds the first `PT_LOAD` program header and records:

```text
entry     <- ELF e_entry
loadaddr  <- PT_LOAD p_vaddr
length    <- PT_LOAD p_filesz
memlength <- PT_LOAD p_memsz
```

The file bytes from the loadable segment are copied into the PROM segment payload.

This is an important port boundary for ARCS/x86-64: the original `promgen` logic is reusable, but its ELF-machine/endianness acceptance is intentionally MIPS-specific and must be treated as an architecture boundary rather than rewritten wholesale.

### SN/IO6 PROM header format

The header comes from `irix/kern/sys/SN/promhdr.h`.

Important constants:

```c
#define PROM_MAGIC       0x4a464b535743534dLL
#define PROM_MAXSEGS     16
#define PROM_DATA_OFFSET 0x1000
```

Compression flags:

```c
SFLAG_NONE = 0
SFLAG_RLE  = 1  /* not yet supported */
SFLAG_LZW  = 2  /* not yet supported */
SFLAG_GZIP = 3
```

Each segment records:

```text
name[16]
flags
offset
entry
loadaddr
length
length_c
sum
sum_c
memlength
```

with reserved fields padding each segment descriptor to 128 bytes.

The main header stores version, revision, total image length, segment count, and up to 16 segment descriptors.

### Image layout

`calc_offsets()` proves the exact binary layout:

```text
offset 0x0000: promhdr_t
padding up to 0x1000
segment 0 payload
8-byte alignment
segment 1 payload
8-byte alignment
...
```

The first payload begins at `PROM_DATA_OFFSET == 0x1000`.

All later segments are aligned to 8-byte boundaries.

`ph->length` is the final aligned end offset.

### Checksums

Each segment stores byte-sum values for both the uncompressed and compressed representations.

`promgen -h image.img` does more than print metadata. It:

- validates `PROM_MAGIC`
- reads every segment descriptor
- seeks to each payload
- checks compressed-data sums when compressed
- decompresses compressed payloads
- verifies decompressed length
- verifies the uncompressed byte sum

This is a built-in image-verification path and should be preserved in a host port.

### Compression implementation

The supported compressed representation in this source is gzip.

`gzip_compress()` uses `dozip()`.

`dozip.c` is historically simple: it writes the in-memory segment to a temporary file and executes the host `gzip` command using `system()`, then reads the resulting gzip stream back.

The decompressor is the original PROM-side unzip implementation. `promgen/Makefile` symlinks:

```text
../../IP27prom/unzip.c
../../IP27prom/unzip.h
```

into the host-tool build.

This is another strong host/target pairing: `promgen` verifies that the exact compressed stream it creates can be decoded by the same unzip logic used by the PROM.

### Binary output

`write_bin()` writes:

1. `promhdr_t`
2. zero padding to the first segment offset
3. each compressed or uncompressed payload according to segment flags
4. zero padding between aligned segments
5. zero padding to final image length

### S3 output

`write_s3()` emits the header and segment payloads through the bundled `s3.c` implementation using the segment offsets as S3 load-address offsets.

### Confirmed original production usage: IO6

`stand/arcs/IO6prom/Makefile` says explicitly:

```text
Create an image file .img, which can be used by the
flashio6 command to write it to the PROM on the IO6.
This image file contains headers and code in a
compressed format.
```

The rule is:

```make
${PROMGEN} -o $@ -v "${RELEASE_VER}.${RELEASE_REV}" -V -N io6prom \
    -E ${PRODUCT}io6prom
${PROMGEN} -h $@
```

The Makefile also documents the uncompressed variant:

```make
${PROMGEN} -o $@ -V -U -N io6prom -E ${PRODUCT}io6prom
```

So `promgen` is the actual image generator and verifier for shipped IO6 firmware.

### Confirmed original production usage: IP27

`stand/arcs/IP27prom/Makefile` uses `promgen` directly for the CPU PROM image:

```make
${PROMGEN} -o ${PRODUCT}ip27prom.img \
    -v "${RELEASE_VER}.${RELEASE_REV}" -V \
    -N ip27prom -U -E ${PRODUCT}ip27prom
```

It deliberately disables compression for IP27 CPU PROM images with `-U`.

The same Makefile uses `convert` to produce Intel HEX variants for config-specific images:

```make
${CONVERT} -f intelhex ${PRODUCT}ip27prom_${IP27_CONFIG} \
    > ${PRODUCT}ip27prom_${IP27_CONFIG}.hex
```

### Original developer documentation confirms `.img` semantics

The IP27 PROM Technical Reference Manual contains a developer-only `PROM Images` section stating that:

- the IP27 build directory and IRIX flash commands use PROM images in `promgen` format
- these files use the `.img` extension
- installed images reside in `/usr/cpu/firmware`
- `promgen -h file.img` is the supported way to verify an image and view its version

This confirms `promgen` format is a first-class deployed firmware image format, not merely a build intermediate.

## `convert` - generalized object/image conversion framework

The `convert` Makefile describes it as:

```text
ripped off from MIPS' SPP and SGI-ized
```

It links against:

```text
-lmld -lelf
```

and is modularized into object readers and output formatters.

### Input object formats

The object dispatch table recognizes:

- MIPS object formats, multiple big/little-endian magics
- Berkeley a.out (`OMAGIC`, `NMAGIC`, `ZMAGIC`)
- several COFF magics
- ELF
- pure/raw binary when `-p` is supplied

The source architecture is deliberately extensible:

```text
object reader -> conversion core -> output formatter
```

### Output formats

The format table supports:

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

This is much broader than the earlier assumption that `convert` was just an ELF-to-hex helper.

### Byte/word manipulation

Command-line options include:

```text
-b N      select one byte lane
-s N      select one 16-bit half
-z        swizzle bytes within words
-Z        alternate swizzle used by IP22/IP24-style output
-p        force pure/raw input
-f fmt    select output format
-v X.Y    set output version for formats that support it
```

The source comments identify the swizzle use cases explicitly:

- `-z`: EVEREST/IP19 byte ordering
- `-Z`: IP22/IP24-style ordering

### IP30 / Speed Racer integration

The later source added `rprom` and `fprom` output support through `racer.c`.

Its own comments define the two IP30 flash regions:

```text
rprom: recovery prom, checks fprom and jumps to it
fprom: full prom, real PROM functionality
```

The formatter adds flash headers, checksums, version data, timestamps, and required padding.

`stand/arcs/IP30prom/Makefile` confirms the production rule:

```make
${CONVERT} -f rprom -v ${RPROM_VERSION} ../RACER_RPROM.O/rprom > $@
cat fprom.bin >> $@
```

It also generates the external-programmer-oriented Intel HEX form with:

```make
${CONVERT} -Z -p -f intelhex IP30prom.bin > IP30prom.hxe
```

The Makefile comment says the swapped Intel HEX form was for Solectron, while SGI's local Data I/O programmers used custom burn scripts instead.

This proves `convert` spans multiple PROM generations and manufacturing workflows, including IP30.

## `myacc` - original parser generator for IP27 PROM

The `myacc` README removes any ambiguity about its purpose.

It is a modified Yacc that:

- produces modular output
- stores parser state in a structure instead of global variables
- prefixes generated names with a module name
- allows multiple parsers in one application
- was specifically created so the IP27 PROM parser could run without global data, using stack-contained state

`stand/arcs/IP27prom/Makefile` uses it directly:

```make
MYACC   = ${TOOLS}/myacc/myacc
YACCPAR = ${TOOLS}/myacc/yaccpar

parse_y.tab.c parse_y.tab.h: parse.y
    ${MYACC} -v -p${YACCPAR} parse.y
```

This matters to the current ARCS/x86-64 project because the Command Monitor parser should continue to be treated as generated original PROM code, not casually replaced by a modern parser implementation.

## Overall historical build graph now confirmed

A simplified source-grounded graph is:

```text
PROM C/assembly sources
        |
        +-> gconf / myacc / asset generators
        |       produce generated C/tables/fonts/bitmaps/parser source
        |
        v
SGI linker produces MIPS ELF standalone PROM executable
        |
        +------------------------------+
        |                              |
        | older EVEREST/IO4            | later SN0/IP27/IO6
        v                              v
     promcvt                        promgen
        |                              |
        v                              v
 IO4 multi-segment flash image     .img segmented PROM image
                                   header + payload(s)

General and manufacturing conversions:

linked ELF / raw image
        |
        v
      convert
        |
        +-> Intel HEX
        +-> S-record variants
        +-> binary/pure
        +-> IP30 RPROM/FPROM packaging
```

This is substantially more complete than the project previously assumed.

## Implications for ARCS/x86-64 UEFI

### Preserve the architecture, not the MIPS-only assumptions

The promising port strategy is:

```text
original firmware source
    -> original generators where practical
    -> x86-64/UEFI link stage
    -> ported SGI PROM packaging/conversion tools where useful
```

For example, `promgen` itself should not be thrown away merely because it checks for MIPS ELF today. That ELF identification logic is a narrow architecture boundary. The image format, segment table, verification, compression test, binary writer, and S3 writer are separable original SGI logic.

### Do not force obsolete packaging onto the UEFI executable

The end product remains a native UEFI application. We do not need to pretend an EFI PE/COFF image is physically an IO6 flash PROM.

However, recovering these tools is still valuable because they provide:

- exact original generated assets
- exact original parser/config generation
- validation utilities
- known firmware image structures
- historical packaging behavior
- a route to producing authentic SGI-style PROM image artifacts in addition to the EFI executable
- direct evidence for how SGI expected firmware components to be segmented, versioned, checksummed, compressed, and verified

### Host-tool porting rule

For every original host utility:

1. build the original source first if possible
2. preserve command-line syntax and output formats
3. patch only host API/compiler/architecture assumptions
4. validate against original inputs and known outputs
5. keep host-port changes isolated from pristine firmware logic

## Immediate engineering experiments

### `promgen`

Build it natively on Linux with minimal compatibility patches.

Likely host-port boundaries:

- SGI `libelf` API compatibility
- SGI integer typedefs
- old `sys_errlist` use
- `tmpnam`/host `gzip` behavior
- MIPS-only ELF identification
- include path for `sys/SN/promhdr.h`

First correctness target:

- feed an original MIPS IP27 or IO6 ELF into the ported utility
- compare generated `.img` against known/original output if available
- run `promgen -h` and verify all sums and decompression

Only after that should x86-64 ELF acceptance be added as an architecture-boundary extension.

### `promcvt`

Build original EVEREST tool and reproduce `IO4prom/segldr/Makefile` packaging from its original segment inputs.

This is valuable as an independent confirmation that the recovered host environment can reproduce an older SGI flash-image pipeline.

### `convert`

Build the generalized converter and validate at least:

```text
ELF -> intelhex
raw -> intelhex
ELF/raw -> binary/pure
IP30 rprom packaging
```

The IP30 Makefile gives a concrete regression path for `rprom` output.

### `myacc`

Build original `myacc` and regenerate the IP27 parser from `parse.y`.

Compare generated `parse_y.tab.c` and `.h` with the source-tree copies/build expectations.

This would let the project use SGI's own parser generator for parser regeneration rather than treating the checked-in generated C as a dead end.

## Current priority

The new priority order for original host-tool recovery is:

1. `graytospans` and the SGI image dependency, because it immediately affects GraVT/REALITY assets.
2. `myacc`, because it is directly tied to the pristine IP27 command parser.
3. `promgen`, because it is the confirmed deployed SN0/IP27/IO6 image format and has its own verification path.
4. `convert`, because it is the generic conversion/manufacturing framework and contains IP30 packaging.
5. `promcvt`, because it reproduces the older EVEREST/IO4 multi-segment flash pipeline.
6. `gconf`, `bdftohtp`, `topcbm`, and the reverse asset tools.
7. remaining standalone tooling: `elf2coff`, `hwreg`, `dataio`, `promice`, `eawrite`, `squash`.

## Key conclusion

The original source tree contains enough host-side infrastructure that the ARCS/x86-64 project should now be treated as a port of an SGI firmware development environment, not merely a port of one firmware binary.

That is the major architectural consequence of this discovery.
