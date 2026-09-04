# clogin evidence inventory

This file records only evidence currently located in the IRIX 6.5.7m source mirror, archived IRIX files, and the existing Linux prototype. Anything not directly supported is labeled as a prototype hypothesis or unresolved.

## Confirmed IRIX integration

### Visual-login selection

Source: `eoe/man/man4/visuallogin.4`

- `windowsystem=on`, `visuallogin=on` -> visual login program.
- 4Sight -> `pandora`.
- IRIX Interactive Desktop -> `clogin`.
- `windowsystem=on`, `visuallogin=off` -> `xdm`.
- `windowsystem=off`, `visuallogin=off` -> standard IRIX `login`.

### User-icon switch

Source: `eoe/man/man4/visuallogin.4`

- `noiconlogin=off` -> `clogin` may display a user icon for each user.
- `noiconlogin=on` -> user icons are suppressed.
- `configClogin(1M)` is named as another way to configure this behavior for `clogin`.

Configuration files named by the man page:

- `/etc/config/visuallogin`
- `/etc/config/noiconlogin`

### Actual `clogin.conf` vocabulary

An archived IRIX `clogin.conf` is available in the project evidence set. Its own comments state that it is modified by `configClogin`, read by `clogin`, and that the fixed keywords are defined by `sysadm/SaCloginConf.h`.

Observed active directives:

```text
home directory must be local
use small window
display faces
```

Observed hidden-user syntax:

```text
<username>:noshow
```

with the sample file hiding:

```text
lp:noshow
nuucp:noshow
```

The file explicitly says the hidden-user list must be at the end of the file.

This materially upgrades several previously vague ideas into confirmed behavior:

- `clogin` has its own configuration file in addition to `visuallogin`/`noiconlogin`.
- locality of home directories is configurable.
- a small-window mode is configurable.
- face display is configurable.
- per-user hiding is implemented by explicit `username:noshow` records.

Still unresolved from this file alone:

- the pathname from which `clogin` reads `clogin.conf`,
- the opposite keyword forms for each directive,
- exact parsing/error behavior,
- whether `display faces` and `noiconlogin` are combined, overridden, or synchronized by `configClogin`.

High-value source target: `sysadm/SaCloginConf.h`.

### Shipped account-display metadata

Source: `eoe/cmd/adm/passwd.sgi` in the 6.5.7m tree.

This file explicitly says it contains additional information about some logins shipped with `/etc/passwd`. Observed tokens include:

```text
root:machineicon
sysadm:machineicon:noicon
daemon:noshow
bin:noshow
uucp:noshow
sys:noshow
adm:noshow
lp:machineicon:noicon
nuucp:machineicon:noicon
sgiweb:noshow
man:noshow
rfindd:noshow
nobody:noshow
/hosts:ignore
/net:ignore
```

Confirmed token vocabulary from this file:

- `noshow`
- `machineicon`
- `noicon`
- `ignore`

This is strong evidence that original login-account presentation was not based on a generic UID threshold alone. SGI shipped explicit per-account display metadata, including separate notions of hiding an account, using a machine icon, suppressing an icon, and ignoring pseudo-entries.

Still unresolved:

- exact consumer of `passwd.sgi` (`clogin` directly, config tooling, account database generation, or shared library),
- exact precedence between `passwd.sgi`, `clogin.conf`, and `noiconlogin`,
- semantics of combinations such as `machineicon:noicon`,
- pathname of the installed/generated runtime database if `passwd.sgi` is only a template.

### Autologin contract

Sources:

- `eoe/man/man4/autologin.4`
- `eoe/cmd/initpkg/init.d/sysetup`

Confirmed files and semantics:

- `/etc/autologin` is an ASCII file containing the graphics-console autologin username.
- `/etc/autologin.on` is an existence-based enable flag.
- both `xdm` and `clogin` consume this mechanism.
- the boot-time `sysetup` script creates `/etc/autologin.on` when `/etc/autologin` is readable.
- documentation says `xdm` or `clogin` normally removes `/etc/autologin.on` after consuming it so autologin does not automatically repeat for subsequent sessions.

This gives us a concrete compatibility requirement for the Linux implementation: autologin is not merely a config boolean; the original interface is file-based and one-shot.

### Session-end ecosystem

6.5.7m references show `endsession -f` used by IRIX session/test tooling. This confirms `endsession` is part of the surrounding IID session lifecycle, but it does not yet prove exactly how `clogin` launches or monitors the normal IID session.

## Source-mirror status

Direct searches for source code named `clogin`, `dtlogin`, `configClogin`, or `SaCloginConf` have not yet exposed the implementation/header in the 6.5.7m mirror. Current literal `clogin` hits are documentation/integration references rather than the main implementation.

This means the next searches should use:

- package/IDB entries for `passwd.sgi`, `clogin.conf`, `configClogin`, and login resources,
- installed resource paths,
- X resource names,
- strings from the target `clogin` binary,
- library dependencies and symbol names,
- related session utilities,
- historical CDE/xdm source names where SGI may have retained upstream filenames.

## Existing Linux prototype inventory

The current repository root contains `dvlogin`, not reconstructed `clogin` source.

### Process loop currently implemented

`main.c` currently:

1. chooses/claims a Linux virtual terminal,
2. starts an X server,
3. runs the Motif login GUI,
4. tears down that X server after authentication,
5. starts the user session,
6. waits for the session to end,
7. repeats the login loop.

This is portable implementation behavior, not yet evidence of SGI's exact process architecture.

### Current compatibility layer

`clogin_features.h` currently implements or assumes:

- dynamic enumeration of login-capable users,
- home-directory filtering,
- a minimum UID policy,
- optional root shortcut,
- face-file lookup paths,
- click/double-click behavior,
- delayed password-field display.

After recovering `clogin.conf` and `passwd.sgi`, home-directory locality, face display, and explicit account visibility metadata are no longer pure guesses, but the prototype's exact filtering algorithm remains unproven. In particular, a generic Linux minimum-UID cutoff is not supported by the recovered IRIX evidence and should not be treated as reconstructed SGI behavior.

The comments currently label the following face lookup order as historical:

- `$HOME/.icons/login.icon`
- `/usr/local/lib/faces/$USER`
- `/usr/lib/faces/$USER`

Do not treat that ordering as confirmed until we locate the original `clogin(1)` documentation, binary strings, resources, or source evidence that proves it.

## High-value targets to recover next

1. Package/IDB entries for `passwd.sgi`, `clogin.conf`, `configClogin`, and the actual `clogin` executable/resources.
2. `sysadm/SaCloginConf.h` or any archive/binary containing its strings/symbols.
3. Original `clogin` binary from the target IRIX installation.
4. Original `clogin(1)` and `configClogin(1M)` man pages if present in installation media or archive material.
5. X resource databases or app-defaults used by `clogin`.
6. User-face/icon search strings from the binary.
7. Authentication/session-start symbols and imported libraries.
8. Exact relationship to `xdm`: shared daemon code, exec handoff, or separate replacement process.
9. Autologin handling in the target binary, especially unlink/removal of `/etc/autologin.on`.

## Confidence rule

- target 6.5.0 binary/resource evidence: primary truth
- matching 6.5.7m source/documentation: very strong support
- archived IRIX config/resource files: direct behavioral evidence
- current Linux `dvlogin` code: implementation reference only
- unsupported comments/assumptions: hypothesis until proven
