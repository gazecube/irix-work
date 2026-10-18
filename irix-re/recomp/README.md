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
`libirix_recompiled_stack.a` plus library-shaped ELF shared objects:
`libXt.so`, `libXm.so`, `libSgm.so`, `libvk.so`, `libMrm.so`, and `libUil.so`.
The shared objects are built from the same cached PIC objects as the archive,
so the split adds link steps without translating and compiling the source a
second time. They preserve the IRIX library boundaries, but their translated
entry points are exported under their original global IRIX function names so
native linkers can resolve them. This is a temporary link-only alias layer:
the functions still use the recomp runtime's guest-CPU calling convention,
and translated global data is not exposed as host data. They are not yet
callable drop-in host-ABI replacements for native Xt/Motif libraries. This
describes the legacy generated-core outputs; the native32 outputs omit those
unsafe aliases and need real host-C-ABI entry points in each owning DSO.
See [HOST_ABI_BRIDGE.md](HOST_ABI_BRIDGE.md) for the verified ABI boundary and
the staged work required to turn these aliases into native-callable wrappers.

For a separate i386 build, configure with a 32-bit toolchain and
`-DIRIX_BUILD_NATIVE32_CORE=ON`, then build `irix_native32_recompiled_core`.
This writes to `generated-core-native32/` and explicitly compiles and links
with `-m32`; the normal generated core and existing runners are left alone.
MIPS instructions are lowered ahead of time into x86 C and compiled to i386,
and unsupported control-flow fallbacks trap rather than invoking the MIPS
interpreter. This is a static-execution milestone, not yet a host-ABI Xt or
Motif implementation: translated functions still use guest CPU/memory helpers
and `libirixre` for IRIX address-space semantics. Host pointer, structure,
byte-order, and callback conversion remains required before native
applications can call the API safely. The native32 DSOs intentionally do not
export translated API names as link-only aliases. They now contain host-C-ABI
entry points: Xt wrappers and shared state are in `libXt.so`, while `libXm.so`
exports Motif entry points through i386 GOT-based tail thunks to the shared
context owned by `libXt.so`; there is no separate ABI-context DSO. The Motif
factory wrappers now cover the common arrow, cascade, drawn, file-selection,
form, frame, label, message, option, prompt, push, question, radio, row-column,
scale, scrolled, separator, and toggle constructors. A native 32-bit
shell/label client links directly to these DSOs and successfully creates,
realizes, and destroys its widgets. A relink of Netscape's existing i386 XFE
objects with no `libirixhostabi` dependency currently stops at 110 unresolved
Motif imports; that link experiment is the active API-coverage checklist, not a
successful browser build. This wrapper path still uses `libirixre`
to marshal calls into the translated core, and Xt translation-table operations
currently take a MIPS compatibility fallback, so this is not yet fully
runtime-free execution. The build check guarantees generated functions contain
no direct interpreter-call relocation; it does not remove runtime dispatch or
those explicitly retained compatibility paths.

The same option also builds `irix_native32_instruction_probe`: a fail-closed
MIPS-to-i386 assembly emitter for `ADDU`, `SUBU`, `ADDIU`, `LUI`, `ORI`, `LW`,
`SW`, `LBU`, `SB`, `BEQ`, `BNE`, `JR $ra`, and `NOP`. Its 32-bit executable
checks arithmetic, N32 sign extension, guest big-endian word loads/stores,
byte accesses, both outcomes of `BEQ` and `BNE`, and execution of each branch's
delay slot on either outcome. This is an instruction-level backend proof, not
yet a translator for the full library corpus. Unsupported opcodes are rejected
by the emitter rather than routed to runtime decoding.
See [N32_TO_I386_LOWERING.md](N32_TO_I386_LOWERING.md) for the architecture
mapping, the distinction between semantic C lowering and the assembly probe,
and the corpus-coverage check performed by the native32 build.

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
