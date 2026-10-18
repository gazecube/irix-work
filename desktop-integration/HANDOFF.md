# Handoff (2026-10-16)

## Done this session
- fm speed: guest-call hot path caches, guest malloc/free index + free lists (home folder open ~15 s -> ~3 s CPU).
- Runtime: guest-namespace realpath/open (absolute symlinks resolved inside the IRIX tree), host sh for guest scripts (IRIX_GUEST_SH=guest to undo), /var/tmp shared with host.
- Host data: install-guest-data.py installed IRIX data trees (manifest /usr/lib/irix-guest-data.manifest); /var/X11, /var/netscape, /usr/lib/desktop/iconcatalog on the host.
- Icon Catalog shows entries; program icons now typed (tags read from the IRIX binaries). dirview wrapper added (/usr/bin/dirview).
- Netscape: launchWebJumper installed; bundle launcher passes -irix-session-management by default (NETSCAPE_NO_IRIX_SM=1 to disable).
- Icon tools: tools/fti2svg.py, tools/svg2fti.py; icons/ holds SVG copies and steam-iid-style.{svg,fti}.

## Open
- fm crashed again (navigating inside a window); no log yet. Check ~/.local/state/irix-session/fm.log.
- Icon Catalog does not show the Claude/Codex/Steam symlinks in ~/.desktop-cachyos-x8664/iconcatalog/pages/WebTools.
- Icon Catalog page also lists the page-name folders (pages root watched by fam).
- fftr (/usr/sbin/fftr) traps in its parser (needs /usr/bin/nawk -> awk, done); blocks compiling .ftr/.fti to .otr.
- .fti numeric colours (color(-n)) are guessed greys; named colours iconcolor/shadowcolor/outlinecolor use stand-in hex values.
- iconsmith, sgihelp, insight are O32: need an O32 loader (interpreter-first plan).
- Netscape session management: flag enabled in the launcher only; not verified end to end.
- Runners other than fm, toolchest, iconcatalog, guest-exec still have the old allocator/realpath code; full rebuild + install-native.sh --apply + relog.
- desktop-integration/icons/generic.exec.closed.svg was edited in Inkscape (uncommitted).


## fm graphics-resource fix (2026-10-16, Codex)
- Reproduced folder-opening failures on a private Xvfb display with a temporary HOME.
  The first failure was the native static-path XtGetGC replacement filling the
  X11 bridge's 1024-slot GC table. It allocated one GC per request, but original
  XtReleaseGC did not own those allocations. A failed GC request then unwound
  widget creation; outer import wrappers sometimes continued with NULL/partial
  objects, explaining the later NULL-parent errors and path text in pointer fields.
- Added native Xt GC ownership: share immutable XtGetGC requests by display/root,
  mask and selected values; keep XtAllocateGC allocations separate; reference-count
  releases through the matching static-path XtReleaseGC hook; recycle released
  handle slots. Failed primary GC registration also frees its host GC.
- New shared-runtime interfaces are weak/optional, so older statically linked X11
  bridges retain their prior path until their runners are relinked. No allocator,
  translator, JIT, or interpreted-path changes are included.
- Added IrixXtGcLifetimeTest: 4097 simultaneous identical acquisitions, selected
  versus unselected value matching, distinct mutable allocations, last-reference
  release, 4096 allocation/release cycles, ordinary Xlib ownership, and the actual
  recomp GetGC/ReleaseGC dispatcher hooks. This and five existing runtime tests pass.
- Candidate native fm completed 40 folder open/close cycles and opened a temporary
  dumpster directory. Updated fmprefs/iconcatalog/toolchest launched; an unchanged
  older fmprefs runner also launched with the updated shared runtime.
- Rebuilt build-irix-re runtime and X11 bridge, then relinked the normal root/usr/sbin/fm
  using its CMake-generated link command. Existing libirixcore.so was preserved;
  the GC fix does not require regenerating the translated core.
- Final normal build-tree fm passed another 12 open/close checks, including the
  temporary dumpster directory, with an empty error log.
- install-native.sh dry run identifies /usr/bin/fm for update. It also reports an
  existing font update and Netscape symlink conflict; no install was applied.
- Separate limits observed: keeping about two dozen distinct folder windows open
  exhausted the existing 65536 allocation-record table at ~4.35 MiB guest heap
  consumption (heap budget 64 MiB). The record table itself is 1.25 MiB. This is an
  explicit compatibility-allocator cap, not an N32 ABI/16-bit counter limit.
  A FAM responsiveness warning appeared during the sustained multi-app test;
  this change does not modify FAM.
