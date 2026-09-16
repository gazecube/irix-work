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

The current translated core is the retail N32 Xt/Xm/Sgm/ViewKit/Mrm/Uil
closure. The observed corpus contains 8,366 sized functions and currently
translates with zero decoder failures. The static linker applies the SGI N32
REL/RELA forms observed in those archives and emits the guest main image,
small-data/GOT image, import table, symbol map and generated C registry.

`build_core_stack.py` is the reproducible entrypoint for that closure:

```
python3 tools/build_core_stack.py /path/to/irix-development-tree \
    -o build/recompiled-core
```

It expects the N32 development archives at their normal IRIX locations:
`usr/lib32/libXt.a` and `usr/Motif-1.2/lib32/{libXm,libSgm,libvk,libMrm,libUil}.a`.
It generates relocated C, compiles it with `-Wall -Wextra -Werror`, and writes
`libirix_recompiled_stack.a`.

The 6.5 desktop side is generated separately from `/usr/sbin/desktop` plus the
N32 internal IMD libraries (`libimdApp`, `libimdUtil`, `libimdXutil`,
`libimdModel`, `libimdIconPanel`, `libimdFtrDB`, `libimdFtrGallery`, and
`libimdFtrIcon`). `build_desktop_bundle.py` translates those ELF binaries,
reconstructs quickstart/prebound GOT bindings against the generated core, and
writes `libirix_desktop_bundle.a`.

CMake exposes the same pipeline. From the normal `irix-re` project:

```
cmake -S . -B build \
  -DIRIX_DEV_TREE=/path/to/irix-development-tree \
  -DIRIX_RUNTIME_TREE=/path/to/irix-runtime-tree \
  -DIRIX_INTERNAL_TREE=/path/to/tree-containing-usr/lib32/internal
cmake --build build
```

With only `IRIX_DEV_TREE`, CMake builds the translated core. With all three
paths, it also builds the translated desktop/IMD bundle. These are generated
artifacts; original IRIX binaries and libraries are not checked into the repo.

Important tools:

- `extract_ar.py`: dependency-free IRIX archive extraction.
- `link_stack_n32.py`: synthetic N32 linker for the observed SGI REL/RELA
  forms, small-data/GP handling, GOT construction and import allocation.
- `recomp_linked_stack.py`: emit translated C from the relocated stack image.
- `recomp_elf_bundle.py`: translate sized functions in an N32 executable/DSO.
- `gen_prebound_bindings.py`: reconstruct cross-DSO quickstart and prebound GOT
  bindings by symbol name.
- `extract_fast_quarks.py`: recover SGI Xrm FAST_QUARKS tables used by IRIX Xt.
- `translate_n32.py`: instruction translator; unsupported instructions remain
  fatal rather than being assigned guessed semantics.

The intended closure is application -> ViewKit/Sgm/Motif -> Xt/Xrm where guest
layout or behavior matters, with native boundaries pushed down toward Xlib,
libc and the host OS. Current runtime work is driving the real 6.5 `desktop`
startup under strict guest-memory faults, then moving directly to the File
Manager and the rest of the usable desktop session.
