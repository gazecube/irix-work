# Experimental desktop recompiler handoff

This file captures the exact state of the 2026-09-15/16 desktop bootstrap pass so the work can resume without reconstructing the temporary harness state.

## Pushed production/runtime pieces

The `irix-re` branch now contains the reproducible core and desktop bundle builders, the corrected SGI N32 composite GP relocation handling, strict guest-memory fault tracking, the extracted X11/Xrm bridge, and the guest libc allocator bridge.

Important recent commits in this pass include:

- `db48edb36763ddcec65ed2cd24b85083792763a3` - SGI composite GP relocation fix.
- `ea957a7ed6b7a1a9c97998ea2986185958c34f41` - reproducible core/desktop build documentation.
- `89708982f72e915df334594debf7e3cf2e31b9e3` - X11/Xrm bridge wired as a separate runtime library.
- `3170a663b9771a0196e2822f10e4a1395cf16475` - guest libc allocator bridge.
- `3983d8174af5a2ce2a3f95e13913fa111ce7ae5f` - build integration for the libc bridge.

## Verified translated closure

Core static archive closure:

- libXt.a: 1077/1077 translated
- libXm.a: 3311/3311 translated
- libSgm.a: 1232/1232 translated
- libvk.a: 2298/2298 translated
- libMrm.a: 198/198 translated
- libUil.a: 250/250 translated
- Total: 8366/8366 functions, zero decode failures

The relocated core image applies 159665 runtime relocations and uses the synthetic address spaces already documented by the recompiler tools.

Desktop-side translated bundle:

- 6.5 `desktop`: 68/68 translated
- 6.5 `iconcatalog`: 166/166 translated
- IMD library set: 2247/2247 translated
- Desktop + IMD bundle used during the harness pass: 2315 translated SGI functions before adding the core closure

## SGI fast-quark discovery

Negative pseudo-pointers passed to `XrmPermStringToQuark` are intentional SGI FAST_QUARKS values, not bad relocations. Decode convention:

```
index = ~encoded_pointer
name  = _SGIXQuarkNameTable[index]
quark = _SGIXQuarkValueTable[index]
```

Examples verified from the IRIX libX11 tables:

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

This is now handled by the checked-in Xrm bridge rather than treated as an invalid guest pointer.

## GP relocation rule found during desktop startup

SGI emits two forms of the composite `GPREL16 + SUB + HI16/LO16` relocation.

For local `STT_SECTION` symbols, the addend carries the original object GP bias and the relocated expression must account for the object's `.reginfo` GP value.

For ordinary/global symbols, that bias must not be applied.

The conditional handling is checked into `tools/link_stack_n32.py`. This fixed the exact `0x7800` error observed in `Intrinsic.o` without breaking the ViewKit `VkApp.o` case.

## Real desktop execution progress

The 6.5 `desktop` executable is not merely translated; its real `main` has executed through the combined translated closure.

Observed path includes:

- `desktop` main
- FmApp allocation/construction
- translated ViewKit
- translated Xt
- Xrm initialization and command parsing
- native Xlib display open through the bridge

With no X server, the guest correctly reached its own `"%s: Can't Open Display"` error path. Supplying Xvfb/`DISPLAY=:99` moved startup substantially farther into Xt per-display/resource initialization.

## Current live blocker

With Xvfb, the current fatal path is Xt reporting:

```
Cannot perform realloc
```

Tracing showed that the initial `realloc` sizes are sane. They then repeatedly double, implying the allocator is not the primary cause: some Xt table/count/capacity state is repeatedly growing or never observing termination.

The next action should therefore be to trace the translated caller of `XtRealloc`/`realloc`, identify the count/capacity field driving the repeated growth, and inspect the upstream Xrm/Xlib shadow state feeding that table.

Do not simply make the heap larger. The repeated doubling is diagnostic evidence of an upstream state/layout issue.

## Temporary harness state that existed outside the repository

The pass used disposable sources under `/mnt/data/desktop-run/` and `/mnt/data/desktop-bundle/`, including variants named approximately:

```
runner.fixed3.c
runner.realloctrace.c
runner.sizehints.c
runner.memalign.c
runner.gaps.c
runner.vecnew.c
native_bridge.c
```

These were diagnostic snapshots, not authoritative production code. Their proven allocator/Xrm/Xlib pieces have been moved into the checked-in runtime bridge modules. The useful remaining information from those variants is the realloc-growth finding above.

Generated `fast_quarks.inc` files were deliberately not treated as source-of-truth; use `tools/extract_fast_quarks.py` against the user's IRIX libX11 corpus instead.

## Resume order

1. Build the checked-in corrected core and desktop bundle from CMake using the extracted IRIX trees.
2. Start Xvfb and run the strict desktop bootstrap against a real `DISPLAY`.
3. Instrument the exact translated caller responsible for the repeated realloc growth.
4. Fix the upstream guest-state/layout error, not the heap limit.
5. Continue native-boundary adapters only as they are reached by the real startup path.
6. Once `desktop` reaches stable visible state, bring in 4Dwm and Toolchest, then use the 5.2 `fm` binary only where the 6.5 desktop executable does not already cover the File Manager behavior.

The project priority remains: get as close as possible to a working IRIX desktop first, then port the individual applications.
