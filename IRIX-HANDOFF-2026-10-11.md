# IRIX desktop handoff — 2026-10-11

Branch `irix-re`. Supersedes the "remaining work" of IRIX-HANDOFF-2026-10-10.md.
Test GUI programs on `DISPLAY=:1` (Xephyr); never touch the host `:0`.
Build root `build-irix-re`; native runners in `build-irix-re/recomp/root`.
Launcher: `DISPLAY=:1 build-irix-re/recomp/quick/irix-session`.

## Fixed this session (all committed)
- Background: `SG_UseBackgrounds` never reached 4Dwm because
  `bridge_XrmParseCommand` was a stub, so **`-xrm` and every command-line
  resource was dropped for all guest apps**. Real implementation now lives in
  `runtime/IrixXrmCompat.c` (that file owns the live Xrm handle table, base
  0x72300000; the table in IrixX11Bridge.c is not the live one).
- Move/resize outline "flicker" was authentic IRIX behaviour: stock
  app-defaults ship `*freezeOnConfig: false`, which selects FlashOutline
  (draw, sync, erase, sync loop). Launcher now passes
  `*freezeOnConfig: true` (steady outline); override with
  `IRIX_4DWM_FREEZE_ON_CONFIG=false`. Do NOT skip the XSyncs (tried, it
  inverts XOR parity and the outline disappears).
- `desktop-integration/xsession/`: real display-manager session entry +
  launcher. The installed `/usr/share/xsessions/irix-session.desktop` is the
  dtlogin template and re-execs itself forever; install the new one (needs
  root, see its README).
- DBE: `XdbeQueryExtension` now reports unavailable.

## Open / next
1. **Rebuild stale native runners.** Runners are statically linked against the
   x11 bridge, so every runner must be relinked to pick up bridge changes.
   Stale (Oct 6): `xwsh` (terminal), `desktop`, `fmprefs`, `iconcatalog` was
   rebuilt but needs one more relink for the DBE change. Command:
   `cmake --build build-irix-re --target irix_xwsh_runner irix_desktop_runner irix_fmprefs_runner irix_iconcatalog_runner -j$(nproc)`
   (takes several minutes; do not run two builds at once).
2. **Icon Catalog**: `usr/lib/desktop/iconcatalog/pages/C` was missing from
   IRIX-TREE (crash in readdir(NULL)). Created locally with an `Applications`
   page of relative symlinks to `usr/sbin/*`. IRIX-TREE is gitignored, so this
   is NOT in git; re-create if the tree is re-extracted. Next trap after the
   DBE fix is unknown: rebuild, run
   `build-irix-re/recomp/root/usr/sbin/iconcatalog`, read the
   `IRIX iconcatalog trapped:` block.
3. **fm crash when navigating from `/` into `usr`/`bin`**: reproducible with
   `fm /` then double-click `usr`. Trap is `ImdIconFinder::adoptCompletion`
   (libimdApp 0x0963fa94+0xfc) calling `_menuBar->baseWidget()` with
   `this+0x40` (the VkMenuBar) NULL. Earlier variant: XtAddCallback with a
   NULL widget. The constructor stores the menu bar at 0x0963f1c8 after
   `operator new(0x50)` at 0x0963f170; find why a finder instance reaches
   adoptCompletion without it (second window / preLoadFinder in
   `FmWindow::preLoadFinder` 0x1002bcb0?).
4. Entries without icons in fm (`bin`, `dt`, `src`: symlinks/odd dirs).
5. Generic-guest children (`launcheffect`) trap on a `syscall` insn
   (`insn=0000000c`); not investigated.

## Debug tricks that worked
- X protocol of one client: run the session under
  `strace -f -e trace=writev,write -x -s 4096` and decode requests offline
  (Xlib uses writev). BIG-REQUESTS is enabled (length field 0 + 32-bit len).
- gdb on the native runner has no debug info: use `$rdi` as `rt` at a
  function breakpoint, and cast `irix_guest_read8/32be` to call them.
  Breakpoints on translated functions only hit when entered via the registry.
- Resource problems: break on host `XrmParseCommand`/`XrmCombineDatabase`/
  `XrmSetDatabase`; `XrmPutFileDatabase(db, "/tmp/x")` dumps a database.
- Never `pkill -f <pattern>` with the pattern in your own command line.

## Update (after reboot)
- Runners `xwsh`, `desktop`, `fmprefs`, `iconcatalog` are rebuilt and current.
  Gotcha: a reboot mid-build left **zero-byte `.o` files** in
  `build-irix-re/recomp/generated-*/obj` and the incremental builder kept
  reusing them (link failed with undefined `irix_app_guest_components`).
  Fix: delete zero-size objects and the bundle `.a`, then rebuild
  (`find build-irix-re/recomp -path '*/generated-*/obj/*.o' -size 0`).
- **Icon Catalog now starts** and shows its "Applications" tab, but the page
  is empty ("0 file(s) shown"). Page entries are not picked up from symlinks
  (tried relative and absolute targets) in
  `IRIX-TREE/usr/lib/desktop/iconcatalog/pages/C/Applications/`. The real
  page format is unknown; `iconcatalogedit -add` hangs under the generic
  runner. The user-side counterpart is
  `~/.desktop-<host>/iconcatalog/pages/<page>/` (a directory).
  Needs real IRIX page data or more digging (`iconbooktocatalog`).
- **xwsh (terminal) crashes at startup**: trap in `tuGadget::open`
  (0x10030ebc, `this+0x18` NULL) called from `tuTopLevel::open`, right after
  `tuTopLevel::setInitialSize width=0 height=0` from `wshTopLevel::setHints`.
  Not yet investigated.

## Update 2: Icon Catalog pages
- Pages are generated, not shipped: products' install-time exitops run
  `iconbookedit -add "Category:File Name:<path>" -syspage <Page>`.
  `desktop-integration/iconcatalog/gen-pages.py` parses those out of idb files
  (`~/Downloads/irix-install-sets`: 88 idbs, 105 entries, 12 pages, 69 whose
  target exists in the tree) plus host XDG .desktop files, and applies them
  with the native runner `build-irix-re/recomp/root/usr/sbin/iconcatalogedit`
  (added to ecosystem.json; the generic runner busy-loops in rld for it).
  Entries on disk are absolute symlinks in
  `IRIX-TREE/usr/lib/desktop/iconcatalog/pages/C/<Page>/`.
- Icon Catalog starts and shows all 12 page tabs but every page is empty
  ("0 file(s) shown"). FAM is NOT the problem (it connects to the native fam on
  127.0.0.1:840 like fm does). strace shows it never stats entries inside the
  system page directories and only views the (empty) per-user page
  directories `~/.desktop-<host>/iconcatalog/pages/<Page>`; a symlink placed
  there did not appear either. Next: find where CatalogApp/ImdCatalog builds
  the view model (createPageList is at 0x1000cf50-0xd4 in iconcatalog;
  libimdApp/libimdModel for the model), or compare with fm opening the same
  directory in a healthy session.
- fm gets very slow/wedged when opened on huge host directories such as
  `/usr/bin` (thousands of entries, per-file type lookups through FAM); a
  full session restart clears it. Not a crash.
- XDG `.desktop` host apps would be launched by reference; untested.

## Update 3: fm instability = heap corruption (stack-scan added)
- `report_trap_context` in AppRunner.c now prints a heuristic **stack-scan**
  (return-address candidates resolved to function+offset) on every trap.
  Needs the runner relinked to take effect (done for fm).
- Standalone `fm <dir>` hands its window to the session's single desktop fm
  (`fm -b`) and exits status 0, so crashes show up in the *session* log.
- Reproduction: session up, `fm /`, double-click `usr` (or `/usr/bin`).
  Trap: `XtWindowOfObject` with a0=`0x5f2b7573` = ASCII `_+us`.
  Chain: `FmApp::runFm -> FmWindow::FmWindow (+0x2d4/0x2ec) ->
  VkWindow::addMenuPane (+0x94) -> VkWindow::setMenuBar (+0x3c) ->
  XtIsRealized -> XtWindowOfObject`.
  A widget pointer in the freshly built menu bar was overwritten by the
  TypeCache file name (`/usr/bin` -> `_+usr_+bin`), i.e. **heap corruption by
  path mangling**. The earlier NULL `_menuBar` in `ImdIconFinder::
  adoptCompletion` and NULL widget in `XtAddCallback` are the same family.
- Mangler: `convertSlashToUnderscorePlus(ImdString const&, char*)` in
  libimdUtil (0x0983f4e0): writes into a caller 1 KB buffer, bounds-checked
  at 0x400. Its caller at 0x0983f6a8 does `malloc(0x400)` + memset + strcpy/
  strcat. The code itself looks bounded, so suspect the buffer overlapping a
  live object (guest allocator in IrixLibcBridge.c: first-fit with whole-block
  reuse, realloc/free) or a use-after-free. Not yet proven.
- Next step: deterministic repro + a write watch on the corrupted word (the
  object at 0x300cbed0 in the failing run) to catch the writer's PC, or add an
  allocator debug mode (poison on free, canaries, overlap check on alloc).

## Update 4: tu toolkit (xconfirm / xwsh) and Log Out
- Log Out: `irix-session --end [-f]` (confirm via xconfirm or xmessage,
  `tellwm end_session`, SIGUSR1 to the session script via
  `$XDG_RUNTIME_DIR/irix-session-<display>.pid`). Toolchest entry in
  IRIX-TREE system.chestrc must be `f.checkexec.sh "$IRIX_SESSION_SCRIPT --end"`
  (local, untracked). Verified on a nested server.
- tu toolkit fixes (commit 1aaede138):
  1. Guest loads from address 0..0x3fff read as zero (IRIX "usenull");
     `IRIX_NULL_PAGE=0` disables. Fixed xconfirm's tuFont::getFont crash.
  2. Runner now calls the executable's DT_INIT before main (static C++ ctors,
     e.g. xwsh wshApp creating its tuXExec); `IRIX_SKIP_APP_INIT=1` disables.
  3. `_getpty` falls back to host posix_openpt when /dev/ptc is absent (the
     irix_spty module does not match the running kernel).
- Runners only pick up AppRunner.c/bridge changes when relinked. Only xwsh,
  fm, xconfirm, tellwm, 4Dwm, iconcatalog, iconcatalogedit, desktop, fmprefs
  were relinked this session; the rest (toolchest, background, ...) still
  carry older AppRunner/bridge code. Rebuild with
  `cmake --build build-irix-re --target <irix_*_runner>`; after a reboot
  delete zero-byte objects in `build-irix-re/recomp/generated-*/obj` first.
- xwsh status: window opens and draws; shell launch fails
  ("can't start command", it tries /usr/bin/ksh via the generic runner,
  which has hung in rld for dynamic guest programs). Needs: working shell
  exec, host font path with `~/.cache/irix-fonts` (SGI .pcf.Z decompressed;
  set via Xlib set_font_path, xset +fp did not work), default geometry
  (`XWsh*geometry: 80x24`; plain xwsh computes 0x0).
- Xephyr/session test notes: stale 4Dwm from earlier sessions can linger;
  never `pkill -f` with a pattern present in your own command line.
