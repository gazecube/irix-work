# toolchest port

Original binary: /usr/bin/X11/toolchest (IRIX 6.5, MIPS N32, stripped)

This directory is the source-recovery target for the original SGI Toolchest. MaXX Toolchest is a Linux portability/behavior reference only; the IRIX binary remains the authority.

Binary-backed surface:
- `main` exported at 0x1000ebac.
- Core parser/build functions exported: `BuildMenus`, `BuildPane`, `ParseMenuDescr`, `ParseMenuSet`, `ParseMwmMenuStr`, `ProcessConfigFile`, `ProcessMenuFiles`, `IncludeFile`, `GetNextLine`, `GetString`.
- Command handlers exported: `F_Menu`, `F_Exec`, `F_Exec_sh`, `F_Checkexec`, `F_Checkexec_le`, `F_Checkexec_sh`, `F_Checkexec_sh_le`, `F_Label`, `F_Separator`, `F_Nop`, `F_Title`, `F_TellDesktop`.
- Config paths proven by strings: `~/.chestrc`, `/usr/lib/X11/system.chestrc`.
- CLI usage string: `%s [-horizontal|-vertical|-icon]`.
- SGI desktop protocols include `_SGI_TOOLCHEST_WINDOW`, `_SGI_MENU_*`, `_SGI_DESKS_*`, `F_TellDesktop`, desktop launch effect/waitmap integration and IconCatalog integration.

The source recovery will keep the original chest command language and SGI desktop protocol semantics. Host substitutions are allowed only for unavailable IRIX service binaries.

Next binary pass: recover parser data structures from `ParseMenuDescr`/`ParseMenuSet`, then `BuildPane`/`BuildMenus`, then desktop/desk-manager protocol handling.
