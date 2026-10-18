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
