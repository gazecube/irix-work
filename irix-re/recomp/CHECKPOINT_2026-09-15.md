# Desktop recompilation checkpoint - 2026-09-15

This file records the live static-recompilation state at the end of the desktop bring-up session. The goal is a usable IRIX desktop first, then individual applications.

## Core translated closure

The N32 archive closure currently covers:

- libXt.a: 1077 / 1077 functions
- libXm.a: 3311 / 3311 functions
- libSgm.a: 1232 / 1232 functions
- libvk.a: 2298 / 2298 functions
- libMrm.a: 198 / 198 functions
- libUil.a: 250 / 250 functions

Total: 8366 translated functions, zero unsupported functions in the observed closure.

The synthetic linker currently handles the runtime relocation forms observed in the six archives, including SGI N32 RELA composite `GPREL16 + SUB + HI16/LO16` sequences. The linked image previously applied 159665 runtime relocations, produced 4387 GOT entries, and exposed 522 native-boundary imports.

The composite-GP rule is conditional:

- local STT_SECTION composites include the object `.reginfo` GP bias;
- normal/global symbols do not.

## Desktop-side translated closure

The application-side bundle contains the N32 `desktop` executable and the internal IMD libraries used during bring-up:

- libimdApp.so
- libimdUtil.so
- libimdXutil.so
- libimdModel.so
- libimdIconPanel.so
- libimdFtrDB.so
- libimdFtrGallery.so
- libimdFtrIcon.so

Observed translation counts for the IMD layer totaled 2247 functions with zero decoder failures. `desktop` itself translated 68 / 68 functions. The combined desktop/IMD generated bundle previously contained 2315 translated functions.

## Reproducible build

CMake now exposes the recompilation pipeline through these cache paths:

- `IRIX_DEV_TREE`
- `IRIX_RUNTIME_TREE`
- `IRIX_INTERNAL_TREE`

With those set, the project can regenerate the translated core stack and desktop/IMD archive instead of depending on `/mnt/data` generated artifacts.

Relevant build tools include:

- `tools/build_core_stack.py`
- `tools/build_desktop_bundle.py`
- `tools/recomp_linked_stack.py`
- `tools/recomp_elf_bundle.py`
- `tools/link_stack_n32.py`
- `tools/gen_prebound_bindings.py`
- `tools/translate_n32.py`
- `tools/translate_n32_regimm.py`

`recomp_linked_stack.py` now records `rt->current_pc` on every translated instruction and propagates strict guest traps immediately.

## Native bridge state

### Xrm/X11 bridge

`runtime/IrixX11Bridge.[ch]` currently contains proven adapters for:

- XrmInitialize
- XrmPermStringToQuark
- XrmStringToQuark
- XrmGetFileDatabase
- XrmGetStringDatabase
- XrmMergeDatabases
- XrmCombineDatabase
- XrmCombineFileDatabase
- XrmQGetSearchList
- XrmQGetSearchResource
- XAllocSizeHints

Important SGI ABI discovery: IRIX uses `FAST_QUARKS`. Negative pseudo-pointers passed to Xrm encode entries in `_SGIXQuarkNameTable` / `_SGIXQuarkValueTable` using `index = ~encoded_pointer`. The bridge must decode these rather than treating them as guest addresses.

Examples observed from the original IRIX Xlib tables include:

- -38 -> Boolean
- -26 -> String
- -55 -> CallProc
- -14 -> Immediate
- -513 -> initialResourcesPersistent
- -497 -> InitialResourcesPersistent
- -163 -> translations
- -262 -> Translations
- -134 -> TranslationTable
- -177 -> screen
- -264 -> Screen

Another important Xrm behavior discovered during live startup: when Xt calls `XrmQGetSearchList` with a null/empty database, the guest-facing adapter must return success with an empty search list. Returning False makes Xt interpret the condition as an undersized search-list buffer and repeatedly double/realloc it.

### libc allocator bridge

`runtime/IrixLibcBridge.[ch]` now provides guest-address-space implementations for:

- malloc
- calloc
- realloc
- memalign
- free
- SGI C++ scalar/vector new/delete aliases currently mapped to the same allocator path

The allocator is intentionally simple and monotonic for bring-up. It preserves guest pointers and copies guest allocations during realloc.

## Live desktop execution progress

The translated IRIX `desktop` executable has run from its real N32 `main` through translated SGI library code and into translated ViewKit/Xt.

Observed milestones reached during live execution include:

- original `desktop` main entered;
- FmApp allocation and construction path entered;
- translated ViewKit constructor path entered;
- translated `XtToolkitInitialize` executed;
- command-line/resource initialization progressed through host-backed Xrm adapters;
- display open / Xt per-display setup progressed;
- shell realization progressed far enough to reach normal WM/window setup;
- `XAllocSizeHints` became necessary and is now bridged;
- execution subsequently entered the IMD layer and reached `libimdUtil::_allocString__9ImdStringGUi`.

The last observed hard failure before packaging was caused by the native `memalign(4,16)` import returning zero in the temporary harness. That exact dependency is now implemented in `IrixLibcBridge`, but the desktop run had not yet been repeated with the new production bridge at checkpoint time.

So the immediate resume action is:

1. rebuild/relink the desktop smoke runner against the current branch;
2. initialize `IrixLibcBridge` for the guest heap;
3. bind native imports through `irix_libc_bridge_lookup()` and `irix_x11_bridge_lookup()`;
4. run `desktop` main under strict guest-memory faults;
5. promote the next reached native import into the appropriate bridge module;
6. push each proven bridge immediately.

## Experimental local harness

The live desktop bring-up used several temporary runner variants under `/mnt/data/desktop-run/`. They intentionally contain diagnostic tracing and hard-coded extracted-tree paths and therefore were not promoted as production runtime code.

Latest observed variants at checkpoint time:

- `runner.memalign.c`
- `runner.vecnew.c`
- `runner.gaps.c`
- `runner.fixed3.c`

`runner.fixed3.c` is the latest broad harness snapshot and contains the experimentally proven bridge behavior that was progressively moved into `IrixX11Bridge` and `IrixLibcBridge`.

Do not treat the harness as architectural source of truth. The checked-in bridge modules are the productionized pieces; the harness is only useful for reconstructing a live smoke test if needed.

## Important correctness rules

- Guest pointers remain 32-bit and guest object layouts remain IRIX layouts.
- Do not cast translated Xt/Motif/ViewKit objects to host private structures.
- Push the native boundary down to Xlib/libc/OS rather than bridging above Xt.
- Keep strict unmapped-memory faults enabled; zero-on-unmapped previously hid real ABI/linker errors.
- Original retail IRIX binaries remain behavior authority.
- Keep generated code and its linked guest image from the same generation; stale image/code mixing previously produced false failures.
- Quickstart imports may be reached either through local stubs or prebound original IRIX GOT targets; bind both forms by symbol name.

## Desktop-first priority

Current target order:

1. finish enough Xt/Xrm/Xlib/libc bridging for `desktop` to stay alive;
2. get visible/usable IRIX desktop state;
3. bring in File Manager behavior (`FmApp`/`fm`) as the first major acceptance target;
4. add 4Dwm and Toolchest around it;
5. only then spend significant time on individual desktop applications.
