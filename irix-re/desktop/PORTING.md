# desktop port

Original binary: /usr/sbin/desktop (IRIX 6.5, MIPS N32, stripped)

This directory is the source-recovery target for the SGI desktop control-panel application, not a lookalike replacement.

Binary-backed entrypoint facts:
- `main` is exported at 0x1000900c.
- `FmApp` constructor is exported at 0x10007bec.
- `main` installs locale state, allocates 0x94 bytes for the application object, constructs `FmApp`, then dispatches through the application's virtual run/main-loop entry.
- The executable links libimdXutil, libimdUtil, libimdFtrIcon, libXpm, libvk, libSgm, libXm, libXt and libX11.
- Original resources include `/usr/lib/filetype/desktop.otr`, `/usr/lib/desktop/outputd`, `/usr/lib/desktop/xhoston`, `IconScale`, `iconArea`, `iconDrawA`, and the `Desktop` application class.

Recovered classes already visible in the dynamic symbol surface:
- FmApp
- FmPanel
- PanelComponent
- basePanel
- ResetDialog
- UmaskDlg

Port rule: recover these original classes and their ViewKit/Sgm interactions from the binary. Linux-specific code belongs only at OS-service boundaries such as outputd/xhost/session integration.

Next binary pass: decompile `FmApp`, `PanelComponent`, `FmPanel`, then the panel/dialog class vtables and callbacks as complete source units.
