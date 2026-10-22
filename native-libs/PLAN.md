# Native IRIX libraries: plan (2026-10-18)

Goal: every IRIX library the desktop links against is a standalone native library
(no shared core) exporting its own symbols, for i386 (from N32) and x86_64 (from N64).

## Decisions
- libXt: native, from source (UnixWare X11R5 Xt, `native-libs/build-xt.sh`). i386 works;
  x86_64 needs an LP64 port.
- Motif (libXm, libSgm, libvk...): static recompilation of the IRIX binaries, lowered with a
  native memory model, linking against the native libXt.
- Open question solved on paper: the UnixWare Motif 1.2 source (`~/uw7-src/.../lib/Xm`) builds
  natively for -m32 (110/113 files) and serves as a type source and behavioural oracle.

## Native-memory lowering (what has to be true)
- Guest address == host address (-m32: 32-bit pointers; x86_64 from N64: 64-bit pointers).
- Loads/stores are plain native-endian accesses at the instruction's own width/offset;
  lwl/lwr (swl/swr) pairs become unaligned accesses; FP is native float/double.
- Exports: each exported function gets a native cdecl entry that loads the arguments into the
  N32 register file, runs the translated body and returns v0. Function pointers stored by
  translated code (class records, callbacks) are these entries, so native code can call them.
- Imports (Xt, Xlib, libc): prototypes taken from headers; translated JAL becomes a native cdecl
  call. Indirect calls dispatch on address range (translated entry vs native pointer); passing
  extra stack arguments is harmless under cdecl.
- Initialized data (.data/.rodata) is big-endian in the binaries and must be swapped by field
  width. The IRIX DWARF has no types (names and lines only), so:
  1. pointer slots come from relocations;
  2. field widths come from compiling the UnixWare Motif source with clang for N32 -g (full
     DWARF) and matching data symbols by name; strings are bytes;
  3. anything unmatched defaults to 32-bit words and is checked by differential testing.
- Risks: bitfields and unions accessed at different widths, struct padding for doubles
  (N32 aligns to 8, i386 to 4), SGI-only fields/widgets with no source match.

## Milestones
1. Build the UnixWare Motif natively (fix FileSB, IconFile, List) as the oracle.
2. Compile the same Motif sources with clang for N32 (-g) as a test corpus with known types.
3. Add the native-memory mode to the translator and make that corpus pass the oracle's tests.
4. Apply to the IRIX libXm/libSgm binaries; reconcile the SGI deltas against the oracle.
5. x86_64: repeat from N64 once i386 works; port Xt to LP64.

## Status after milestone 3 (2026-10-18)
- Pipeline works end to end on i386: the UnixWare Motif 1.2 sources compiled for N32 big-endian
  (clang, DWARF), translated with native memory, linked against the from-source Xt, run the oracle
  client (form, push button, callback, native XmPrimitive subclass) correctly.
- Open correctness problem (the risk named in the plan): mixed-width accesses. Big-endian code may
  write a word and read part of it (bitfields, unions, punning), which is consistent in big-endian
  memory but not in little-endian memory. Seen in Motif's optimized XmString segments
  (`XmStringExtent` returns 774 instead of 30). `NM_SHADOW=1 nm_pack.py` builds a detector that
  reports mixed-width read sites with their guest PC (84 sites in the small oracle test; many are
  likely raw struct copies and benign, which needs value-level analysis to confirm).
- Things that did not fix it: `-ffine-grained-bitfield-accesses` (different crash, optimized flag read
  wrongly), XOR addressing (breaks every string shared with native code).
- Milestone 4 (IRIX binaries) is blocked on the same problem plus missing types for SGI-built objects.
- Candidate directions: (a) decide per hazard site using the detector and patch the translation;
  (b) treat Motif bitfield containers specially (XmString, text line table, DnD status);
  (c) use the oracle (native from source) as the primary x86 library and translate only SGI-only
  libraries (libSgm etc.), measuring their hazard count with the detector.

## Decision 2026-10-21: the output should look like a native x86_64 build
User goal: the recompiled output should be as close as possible to an actual local x86_64 (or i386)
compile of the same app. Concretely: each IRIX app is a normal ELF whose DT_NEEDED matches the IRIX
original (libXm.so.1, libXt, libgl, ...); each IRIX library is a standalone .so with the same soname
and exported symbols; no shared runtime (libirixre.so) and no address-based dispatch table; a
recompiled app plus its libraries can be copied anywhere and run. Native code can link against these
libraries, call them with the normal C ABI and subclass their widgets. O32 apps are in scope later
(iconsmith, showcase) and need O32 ABI support in the translator.

Consequence for memory: **native little-endian memory, native struct layouts** (the native-memory
lowering above), not a big-endian image with swapping. The mixed-width hazard (bitfields, unions,
struct copies, XmString segments) therefore has to be solved by recovering types, not avoided:
- field and bitfield widths come from the UnixWare source oracle compiled for N32 with -g, matched by
  name, and from relocations for pointers;
- hazard sites found by the NM_SHADOW detector are resolved from those types and patched in the
  translation (candidates a and b), with differential tests against the oracle;
- libraries with no source match (libSgm, libvk, SGI-only widgets) get a measured hazard count first.
Accuracy here means: the lowered code must behave like the type-correct native compile would.

Order of work: (1) per-DSO output in the native-memory mode; (2) libc shim named libc.so.1 (struct
layouts, errno, constants); (3) one small chain end to end (libX11 shim -> libXt -> a tiny app such
as xclock); (4) scale to libXm/libSgm/libvk and the desktop apps; (5) O32 ABI; (6) x86_64 from N64.
