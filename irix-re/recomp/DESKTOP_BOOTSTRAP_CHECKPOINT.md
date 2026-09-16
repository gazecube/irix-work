# Desktop bootstrap checkpoint

This file records the live translated IRIX 6.5 desktop bootstrap state so the
runtime work can be resumed without reconstructing the execution trace.

## Proven translated closure

The relocated N32 core closure currently translates 8,366/8,366 sized
functions with zero decoder failures:

- libXt.a: 1077/1077
- libXm.a: 3311/3311
- libSgm.a: 1232/1232
- libvk.a: 2298/2298
- libMrm.a: 198/198
- libUil.a: 250/250

The synthetic linked image contains 410 objects, 6,879 globals, 522 native
boundary imports, 4,387 GOT entries and 159,665 applied runtime relocations.
The six-library translated corpus compiles cleanly with -Wall -Wextra -Werror.

The 6.5 desktop application side also translates cleanly:

- desktop executable: 68/68 sized functions
- libimdApp.so: 182/182
- libimdUtil.so: 252/252
- libimdXutil.so: 459/459
- libimdModel.so: 347/347
- libimdIconPanel.so: 541/541
- libimdFtrDB.so: 183/183
- libimdFtrGallery.so: 102/102
- libimdFtrIcon.so: 181/181

The desktop + IMD bundle therefore contributes another 2,315 translated SGI
functions. A generated host bundle of 2,324 translation/registry units has
compiled cleanly with -Wall -Wextra -Werror.

## Runtime progress

With an X server available through DISPLAY=:99, translated `desktop` has run
through all of the following under strict guest-memory faults:

- desktop main and FmApp allocation
- ViewKit construction
- translated XtToolkitInitialize
- Xrm parsing/database/resource setup
- real X display open
- Xt shell setup and realization paths
- colors/cursor setup
- XAllocSizeHints bridge
- IMD aligned allocation
- 11-element and 251-element SGI C++ vector construction
- selection/atom setup

This is real translated SGI execution, not a source-level reimplementation.

## SGI FAST_QUARKS ABI

IRIX Xlib/Xt uses negative pseudo-pointers for common resource strings.
`XrmPermStringToQuark` must decode a negative 32-bit argument as:

```
index = ~encoded_pointer
name  = _SGIXQuarkNameTable[index]
quark = _SGIXQuarkValueTable[index]
```

Observed examples include:

```
-38  -> Boolean
-26  -> String
-55  -> CallProc
-14  -> Immediate
-513 -> initialResourcesPersistent
-497 -> InitialResourcesPersistent
-163 -> translations
-262 -> Translations
-134 -> TranslationTable
-177 -> screen
-264 -> Screen
```

The checked-in X11 bridge and `extract_fast_quarks.py` are the intended path
for this ABI.

## Composite GP relocation rule

SGI emits two forms of `GPREL16 + SUB + HI16/LO16` composite relocation.
The synthetic linker must distinguish them:

- local STT_SECTION symbol: account for the object's original `.reginfo` gp0;
- normal/global symbol: do not subtract gp0 a second time.

This distinction fixed Intrinsic.o without breaking VkApp.o.

## Current translator correctness bug

The most recent desktop trace identified a global REGIMM link bug.

IRIX uses all four branch-and-link forms:

- BLTZAL  rt=0x10
- BGEZAL  rt=0x11
- BLTZALL rt=0x12
- BGEZALL rt=0x13

For all four, the translator must write `$ra = PC + 8`. The `*ALL` forms are
branch-likely: when the branch is not taken the delay slot is nullified, but the
link semantics must still be modeled correctly according to the instruction.

The existing translator only handled BGEZAL (0x11), and its likely-branch
rewrite discarded the link assignment. This was proven to corrupt guest ABI
state in:

```
testResourceFiles__18ImdResourceManagerGb
```

The resulting stale return address caused re-entry into the wrong code path and
ultimately destroyed `$sp`/`$gp` in the outer `ImdDefaultsResourceManager`
constructor. This must be fixed before trusting further desktop execution.

## Hidden compiler-generated entrypoints

The current executable/DSO generator only emits STT_FUNC symbols. SGI C++ code
also calls unsymbolized compiler-generated helper entrypoints.

The first confirmed pair in libimdUtil is:

```
0x0983c234  hidden vector element constructor
0x0983c248  matching hidden destructor
```

The constructor is the tiny sequence:

```
move v0,a0
sw   zero,8(a0)
sw   zero,4(a0)
jr   ra
sw   zero,0(a0)
```

`__vec_new_eh2` reaches these helpers during 11-element and 251-element vector
construction. The generator needs a reusable extra-entrypoint/reachable-gap
mechanism rather than a one-off hardcoded runtime stub.

## Proven native-boundary adapters from the live harness

The live desktop run has required working guest-aware adapters for at least:

- XrmInitialize
- XrmPermStringToQuark / XrmStringToQuark with SGI FAST_QUARKS
- Xrm database/search-list/resource operations
- XAllocSizeHints (IRIX guest layout is 72 bytes in this path)
- guest heap malloc/realloc/free/operator new
- memalign
- qsort with translated guest comparator callbacks
- `__vec_new_eh2` with translated guest element constructors
- setlocale/getenv and ordinary libc string/memory leaves

The checked-in X11 bridge already contains the Xrm and XAllocSizeHints work.
The remaining generic libc/C++ allocation/vector helpers should be moved out of
the temporary desktop harness into a proper runtime bridge next.

## Immediate next steps

1. Fix REGIMM link variants globally in `translate_n32.py` and regenerate the
   affected IMD translation units.
2. Add explicit/reachable unsymbolized entrypoint generation to
   `recomp_elf_bundle.py`.
3. Move memalign and `__vec_new_eh2` out of the temporary harness into a
   checked-in guest libc/C++ bridge.
4. Re-run `desktop` with DISPLAY=:99 and strict memory faults.
5. Continue until visible desktop state is stable, then pull in the actual File
   Manager/browser path and 4Dwm/Toolchest around it.
