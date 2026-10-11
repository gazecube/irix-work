# IRIX desktop handoff — 2026-10-10

Repository: `/home/nekron/Projects/irix-work`, branch `irix-re`.
HEAD: `0f154416fe75c352813f2491533f9df7f9d6094c`.
Many pre-existing uncommitted changes are present. Do not reset or clean them.
No commit or push was made during this work.

## Constraints

- Test IRIX GUI programs explicitly on `DISPLAY=:1`. Host desktop is `:0`.
- Keep `IRIX_XT_XM_STABILITY=0`; broad missing-load/store repair is not a fix.
- Preserve original SGI private widget lifecycle, SGI artwork, and guest paths.
- Do not close the user's Alacritty or change the host window manager.
- Show build output directly. Do not interrupt builds casually.
- Desktop usability is the current focus. IRIS GL is handled in another chat.

## Build paths

Configure/build root is `build-irix-re`, source `irix-re`.
CTest directory is `build-irix-re/recomp` (root CTest finds no tests).
Original-path x86 runners: `build-irix-re/recomp/root`.
Original guest install: `IRIX-TREE`.
Launcher: `DISPLAY=:1 build-irix-re/recomp/quick/irix-session`.
The launcher uses original-path native runners despite living under `quick`.
Avoid the separately configured nested build directory's CMakeCache.

## Verified fixes from this work

### SGI named colors

Added `runtime/IrixX11Color.c`, `.h`, and `tests/IrixX11ColorTest.c`.
Integrated into `IrixRuntimeX11Bridge.c`, `IrixX11BridgeBulkUnit.c`, CMake.
When host Xlib cannot resolve a color name, consult guest `/usr/lib/X11/rgb.txt`
through the existing guest resolver. Preserve host numeric parsing and server
color allocation/approximation. SGIDarkGray and sgibrightgray warnings vanished
in the actual session. Live allocation test passed on Xephyr :1:

`IRIX_TEST_X11_DISPLAY=:1 build-irix-re/recomp/irix_x11_color_test`

### Interpreted nested call versus translated continuation

Root cause of bogus scheme filenames was reproduced in a small test.
`irix_mips_interp_call` takes the translated parent's captured return address
when handing a continuation to interpretation. It also applied that rule to
explicit nested calls into hidden helpers inside the parent's nominal ELF
range. The helper then interpreted the caller's continuation rather than
returning to its active C caller, executing scheme-loader logic twice.

Concrete evidence: SgiGetSchemeDatabase first read Base/ImdPalette/ImdSpec,
then in the same call searched Default SGI scheme/SchemeDescription and True;
the path pointer became XrmDatabase token 0x72300004. A watchpoint caught its
replacement at guest PC 0x0f639538. Hidden helper call at 0x0f639528 targets
0x0f63a090, which lies inside the recovered registry range of the parent.

Added `irix_mips_interp_invoke` in IrixMipsInterp.c/.h. It pushes a size-zero
boundary frame around an explicit interpreter invocation, so a nested helper
uses its live return address. IrixGuest.c dispatch calls now use this entry.
Generated-C continuation handoffs retain `irix_mips_interp_call` semantics.

Added `tests/IrixMipsCallBoundaryTest.c` and CMake target. Test failed before
fix (nested helper returned 99 by executing caller continuation, expected42)
and passes after. Also tests continuation handoff retains parent return.

Actual preallocColors subsequently exited with no bogus scheme warnings.
Session restart also removed these warnings in 4Dwm and Toolchest. Screenshot
verified Toolchest title and menus, FM window title, authentic sidebar pixmaps,
SGI palette colors, and FM list view staying alive. List view still has odd
size/permission formatting; do not claim everything is fixed.

The obsolete XtResolvePathname workaround accepting a database handle in its
path argument was removed from IrixRuntimeXtBridge.c AFTER live validation.
That removal is in the active build, not yet GUI-validated.

## Latest change: process replacement (tests pass, GUI validation pending)

Toolchest Background menu child exited127 before any host execve. Direct
launch of native `/usr/sbin/background` worked and displayed its panel.
Tracing revealed libc execle only looked for a staged executable. Staged
`root/bin/sh` is absent; original `/bin/sh` resolves to guest `/sbin/sh`.
Existing syscall execve already knew how to run guest MIPS/scripts via generic
runner. The bridge lacked that fallback.

Extracted shared `irix_guest_execve(rt, guest_path, argv, envp)` into IrixGuest.c,
exported in IrixGuest.h. It prefers an available staged original-path runner,
otherwise resolves original guest file and delegates MIPS/scripts to generic
runner with IRIX_GUEST_EXEC_PATH/backend environment. SYS_execve now uses it.
Absolute execvp, execl, and execle in IrixPosixBridgeUnit.c also use it.
Relative execvp retains host PATH behavior. execl now decodes all variadic
arguments, including stack slots; old implementation only retained two args.

Extended IrixPosixExecleTest.c to check execl as well, inherited environment,
and all trailing shell positional args. Focused tests passed. All eight CTests
passed earlier after shared exec extraction; latest strengthened exec test and
call boundary test also pass. Further tests for original MIPS/script delegation
were planned but NOT added yet. Review env/error/path behavior before calling
this fully validated.

## Active build at handoff

PID287931:
`cmake --build build-irix-re --target irix_toolchest_runner irix_guest_exec_runner -j4`

It was still running at handoff, compiling regenerated core archive (latest
observed190/447 units). Header changes invalidated cached core compilation.
Do not launch another competing build or interrupt it without reason.
The final rebuilt Toolchest/generic runner have not yet been tested.

Latest process inspection found NO Xephyr/session/4Dwm/FM/Toolchest processes,
although they had been running earlier. The interruption appears to have
ended those exec sessions. Recheck before starting a server. I did not manually
kill the session at handoff and did not modify the host display.

## Remaining work

1. Finish active build, restart Xephyr :1 and session if absent.
2. Validate Toolchest Background and other app launches through shared exec path.
3. Trace background pixmap: grey desktop remains despite user's 4DWm resource
   `*Desk 1.backgroundDescription: -bitmap /usr/include/X11/bitmaps/granite
   -bg #868686 -fg #69b5b5`. Authentic granite asset exists in IRIX-TREE.
4. Background panel opened directly, but selected SGI Logo has no preview;
   that choice can require GL, so choose bitmap/XPM for a meaningful test.
5. Test FM search/tree/list navigation and performance. Only list switch was
   verified alive this turn; no claim about search or /tmp navigation.
6. Remaining warnings: requested visualType unavailable; empty color name in
   4Dwm; Toolchest homogeneous RowColumn child warnings. Scheme warnings fixed.

IRIX-TREE/usr/lib/X11/system.chestrc already had prior substitutions such as
`$IRIX_RECOMP_ROOT/usr/sbin/background`, schemebr, keyboard, etc. These were not
edited here. Preserve user resources unless a runtime diagnosis warrants it.

## Temporary diagnostics / input helper

`/tmp/irix-scheme-gdb.py`, `/tmp/irix-scheme-watch.py` reproduce the loader trace.
`/tmp/irix-toolchest-exec.trace` contains process launch trace.
`/tmp/irix-prealloc-resource.trace` contains file lookup inventory.
No unconditional tracing was added by these fixes.
User service `ydotool.service` was started (not enabled) for CUA keyboard input.
Temporary xev diagnostics were killed before the session restart.

Use fresh screenshots before acting. Tool clicks worked; observation immediately
after input sometimes preceded application repaint. Main host :0 must remain
untouched. Xephyr had been1280x1024, placed on second monitor at host x1607,y43.
Avoid Xephyr -terminate: a one-shot xdpyinfo probe can then shut it down.
