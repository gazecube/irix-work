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
