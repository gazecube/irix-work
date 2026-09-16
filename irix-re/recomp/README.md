# IRIX static recompilation bootstrap

This directory is the execution-first path for IID. The original IRIX N32
machine code remains authoritative. MIPS code is translated ahead of time and
runs against a small compatibility runtime; clean recovered implementations
can later override translated functions one address at a time.

Initial invariants:

- guest virtual addresses and pointers remain 32-bit;
- MIPS-III integer registers remain 64-bit;
- guest memory is big-endian regardless of host endianness;
- original guest object layouts are never cast to host C++ class layouts;
- function dispatch is address-based so translated functions can be replaced
  by verified native overrides without changing callers;
- X11/OS bridges are explicit ABI boundaries, not silent structure casts.

The first target is Toolchest because it provides a compact end-to-end test of
N32 loading, translated control flow, libc/Xt/Motif imports and callbacks. The
same runtime is then used for Desktop, IconCatalog, 4Dwm and FM.

`tools/inspect_n32.py` is dependency-free and verifies the ELF properties
needed before a binary enters the translation pipeline.
