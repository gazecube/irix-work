# Clogin reconstruction

Cleanroom reimplementation of the SGI IRIX graphical login program `/usr/Cadmin/bin/clogin`, targeting X11/Motif systems.

This branch combines two deliberately separate layers:

- a runnable Motif login-manager port (`dvlogin`), used for host-side integration and testing;
- an evidence-oriented reconstruction under `reconstruction/`, derived from the shipped IRIX binary, resources, symbols, disassembly, and observed behavior.

The original SGI binary and runtime resources are valid reverse-engineering evidence. Leaked IRIX source code is not implementation input and must not be copied into this repository or its documentation.

## Current reconstruction scope

The project currently has binary-backed reconstruction work for:

- `PwdNameList` policy parsing and flags;
- user enumeration through the original `pandora::createIconList()` path;
- `checkDirectory()` and its caller-specific state handling;
- user-picture lookup order and local/anywhere policy;
- Clogin presentation and icon-panel behavior;
- autologin marker consumption;
- parts of the Imd/FTR icon rendering path used by Clogin.

The runnable C bridge is in `clogin_reconstructed.h`. The C++ reconstruction and archaeology notes live under `reconstruction/` and should be treated as the primary recovered-behavior reference.

## Cleanroom boundary

Allowed evidence includes the original shipped executable, shared libraries, app-defaults, icons, configuration files, manpages, and other runtime artifacts. Derived material such as symbol maps, offsets, disassembly notes, behavioral traces, and independently written reconstruction code is also allowed.

Leaked IRIX source is kept outside the reconstruction path. It may be used only for contamination auditing, not to supply implementation text, comments, function bodies, or documentation prose.

See `EVIDENCE_POLICY.md` for the project rule in explicit form.

## Building the runnable port

Requirements:

- Motif 2.3.x
- X11/Xlib
- libXinerama
- libXpm
- libXcursor
- PAM

Build:

```sh
make clean && make
```

For debug use, the resulting binary can be launched directly with the existing prototype options, for example:

```sh
./dvlogin -vt 6
```

The runnable port is still a reconstruction target, not a claim of full IRIX parity. Host-specific integration code should remain visibly separated from behavior recovered from the IRIX binary.

## Reverse-engineering tooling

The shipped Clogin executable is an IRIX 32-bit big-endian MIPS N32 ELF. A MIPS-capable binutils build is therefore required for direct disassembly on x86 hosts.

`tools/disassemble-clogin.sh` provides a reproducible wrapper for symbol lookup and address-based disassembly. It accepts paths to the original binary and a MIPS binutils prefix rather than embedding proprietary evidence in the source tree.

## Known high-confidence user-enumeration behavior

The original `pandora::createIconList()` does not use a generic UID threshold. It enumerates passwd records and applies SGI policy, duplicate checks, ignore-list checks, and directory-state checks.

Notably:

- `checkDirectory()==3` suppresses the account;
- `checkDirectory()==2` falls back to `stat()` and requires a directory;
- states `0` and `1` continue without that fallback;
- local-home policy changes the IRIX passwd/YP enumeration source through `_getpwent_no_yp`, rather than requiring `checkDirectory()==0` after enumeration;
- local-picture lookup is different: `findUserPicture()` requires `checkDirectory(home)==0` when pictures are restricted to local filesystems;
- a leading `-` passwd record exits the enumeration routine;
- a leading `+` record enters the IRIX passwd/NIS compatibility path and may be resolved through `getpwnam()` while preserving enumeration position.

See `reconstruction/USER_ENUMERATION_RE.md` and `reconstruction/CHECKDIRECTORY_RE.md` for the detailed evidence.

## License

The independently written reconstruction code retains the repository's existing BSD Zero Clause licensing intent. Original SGI binaries and resources, when retained as evidence, are reference artifacts and are not covered by that source-code license.
