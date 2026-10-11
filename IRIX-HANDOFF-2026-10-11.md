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
