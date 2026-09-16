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

Current Toolchest checkpoint:

- the original ELF32 big-endian N32 PT_LOAD segments load into guest memory;
- N32 PIC entry sets t9 to the guest callee address;
- generated code preserves normal and branch-likely delay-slot behavior;
- direct and indirect guest calls dispatch by original guest address;
- integer, memory and the COP1 subset used by Toolchest are translated;
- MIPS break instructions become explicit recorded guest traps;
- all 282 sized function entries in the executable generate host C and compile
  cleanly with `-Wall -Wextra -Werror`;
- 105 of those entries are functions defined by Toolchest and 177 are IRIX
  quickstart/import stubs; keep that distinction when building the registry;
- translated `F_Separator` executes and returns the original value 1;
- translated `ToLower` executes against the original Toolchest ELF/GOT state
  with native bindings for its libc dependencies and converts
  `HeLLo IRIX 6.5!` to `hello irix 6.5!`.

`tools/inspect_n32.py` is dependency-free and verifies the ELF properties
needed before a binary enters the translation pipeline. `tools/translate_n32.py`
translates one sized symbol at a time and intentionally fails instead of
inventing semantics when it encounters an unsupported instruction.

Next executable milestone: generate a whole-program registry for the 105
Toolchest-defined functions, classify/bind the 177 import stubs, construct an
N32 argc/argv/environment stack, and enter the translated application startup
path.
