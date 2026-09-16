# IRIX static recompilation bootstrap

This directory is the execution-first path for IID. The original IRIX N32
machine code remains authoritative. MIPS code is translated ahead of time and
runs against a small compatibility runtime; clean recovered implementations
can later override translated functions one address at a time.

Invariants:

- guest virtual addresses and pointers remain 32-bit;
- MIPS-III integer registers remain 64-bit;
- guest memory is big-endian regardless of host endianness;
- original guest object layouts are never cast to host C++ class layouts;
- function dispatch is address-based so translated functions can be replaced
  by verified native overrides without changing callers;
- X11/OS bridges are explicit ABI boundaries, not silent structure casts.

Toolchest proved the execution model: original PT_LOAD state, N32 t9/PIC,
delay slots, guest dispatch, integer/memory/COP1 translation and explicit
traps. All 282 sized Toolchest entries generate host C; 105 are application
functions and 177 are quickstart/import stubs. F_Separator and ToLower have
executed from translated code against original guest state.

The pipeline is now deliberately whole-stack rather than function-at-a-time:

- `extract_ar.py` extracts members from IRIX static archives without relying on
  host `ar` compatibility.
- `link_n32_rel.py` lays ET_REL allocatable sections into synthetic guest
  memory, resolves symbols, builds a guest GOT, applies supported MIPS
  relocations and can emit a linked guest image.
- `recomp_archive.py` translates every sized function in every ELF object in an
  archive and records unsupported functions without hiding later successes.
- `recomp_stack.py` locates and processes the N32 Xt/Motif/Sgm/ViewKit/Mrm/Uil
  archive set from an IRIX development tree.
- `audit_manifest.py` groups the remaining translator failures by unsupported
  instruction so opcode implementation can be attacked breadth-first.
- `gen_registry.py` emits address-based runtime registration for successfully
  translated functions.
- `gen_stack_build.py` emits the complete translated C source inventory for a
  generated stack.

Typical broad pass:

```
python3 tools/recomp_stack.py /path/to/irix-development-tree -o build/recomp-stack
python3 tools/audit_manifest.py build/recomp-stack/*/manifest.json
python3 tools/gen_registry.py build/recomp-stack/*/manifest.json -o build/recomp-stack/registry.c
python3 tools/gen_stack_build.py build/recomp-stack -o build/recomp-stack/sources.json
```

The intended closure is application -> ViewKit/Sgm/Motif -> Xt/Xrm where guest
layout or behavior matters, with native boundaries pushed down toward Xlib,
libc and the host OS. The same machinery is intended for Toolchest, Desktop,
IconCatalog, File Manager and 4Dwm rather than separate compatibility hacks.
