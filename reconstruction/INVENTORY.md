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

This file explicitly says it contains additional information about some logins shipped with `/etc/passwd`. The source records per-account presentation metadata using the token vocabulary summarized below. The original account records are intentionally not reproduced here so this reconstruction inventory does not carry source text from the comparison tree.

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

## Original `sysadmdesktop` payload recovered

The extracted `sysadmdesktop` tree contains the original runtime pieces directly:

```text
/usr/Cadmin/bin/clogin
/usr/Cadmin/images/cloginlogo.rgb
/usr/lib/X11/app-defaults/Clogin
/usr/lib/X11/app-defaults/SaConfigCloginGuide
/usr/lib/X11/app-defaults/SaConfigCloginTask
/usr/sysadm/taskdf/SaConfigCloginTask.tdf
/usr/sysadm/taskdso/SaConfigCloginTask.so
/usr/sysadm/privbin/updateclogin
/usr/share/catman/u_man/cat1/clogin.z
/usr/share/catman/a_man/cat1/sysadm/updateclogin.z
/var/sysadm/config/clogin.conf
```

### Original `clogin` executable

`/usr/Cadmin/bin/clogin` is:

- ELF32
- big-endian MIPS
- N32 ABI
- MIPS-III
- dynamically linked
- stripped, but with useful dynamic symbols

Important linked libraries include:

```text
libimdIconPanel.so
libimdModel.so
libimdXutil.so
libimdUtil.so
libimdFtrIcon.so
libimdFtrDB.so
libil.so
libvk.so.1
libSgm.so.1
libXm.so.1
libXt.so
libX11.so.1
```

This proves `clogin` is not merely an xdm greeter skin. It directly uses the same IMD icon/model stack that the Interactive Desktop uses.

Recovered dynamic symbols include:

```text
findUserPicture__16PandoraComponentGP7xpasswd
getIgnoreList__11PwdNameListGv
getShowAnywayList__11PwdNameListGv
getNoshowList__11PwdNameListGv
loginCB__16PandoraComponentGP10_WidgetRecPv
passwordTextActivateCB__16PandoraComponentGP10_WidgetRecPv
userTextActivateCB__16PandoraComponentGP10_WidgetRecPv
createContents__8UserViewGb
update__13PandoraLayoutGb
draw__15PandoraRendererGR7ImdViewlT2
```

Class names present in the executable include:

```text
PandoraComponent
PandoraComponentUI
PandoraWindow
pandora
PandoraLayout
PandoraRenderer
UserView
PwdNameList
imageIcon
```

This is especially important because the IRIX documentation described `pandora` as the older 4Sight visual login program. The IID `clogin` binary still retains `pandora` class/function names internally, strongly suggesting evolutionary reuse rather than a wholly separate implementation.

### Confirmed runtime strings from `clogin`

The binary directly contains:

```text
/var/sysadm/config/clogin.conf
/etc/autologin.on
/etc/autologin
/etc/config/
/usr/Cadmin/images/cloginlogo.rgb
/.icons/login.icon
/usr/local/lib/faces/
/usr/lib/faces/
/usr/local/lib/faces/machineUserIcon
/usr/local/lib/faces/userIcon
display faces
home directory must be local
home directory can be anywhere
use small window
machineicon
noshow
noicon
```

This confirms the previously hypothesized face-search order and gives the exact runtime configuration pathname.

The exact `clogin.conf` keyword pair for account source locality is now confirmed:

```text
home directory must be local
home directory can be anywhere
```

The face lookup roots are confirmed:

```text
$HOME/.icons/login.icon
/usr/local/lib/faces/
/usr/lib/faces/
```

and the binary also references generic face images:

```text
/usr/local/lib/faces/machineUserIcon
/usr/local/lib/faces/userIcon
```

### `Clogin` X resources

The original app-defaults file confirms:

- `Clogin` uses SGI schemes/palettes.
- drag-and-drop is disabled in the login UI.
- the login icon panel disables normal traversal and DnD.
- a `useNoHang` resource defaults to `True` specifically to avoid hanging on down NFS mounts while resolving faces/accounts.
- `noiconlogin` mode displays `/usr/Cadmin/images/cloginlogo.rgb`.
- double-clicking a user icon is documented as an alternative to typing the login name and clicking Log In.
- the password field appears only when needed.
- invalid username and wrong-password messages deliberately use identical wording for security.

Important resource/widget names include:

```text
panelWindow
warningLabel
userFullNameLabel
userLabel
userText
passwordLabel
passwordText
hostLabel
helpButton
loginButton
```

### `configClogin` sysadm task

The configuration UI exposes the original intended semantics very clearly:

- show only local password-file accounts, or include network/NIS accounts,
- explicit SHOW and HIDE user lists,
- show face images or generic user icons,
- use large or small login window.

`SaConfigCloginTask.tdf` identifies the privileged helper as:

```text
PrivilegeList: updateclogin
```

The task DSO contains original class/function names including:

```text
SaCloginConf
SaConfigCloginData
SaConfigCloginTask
SaConfigCloginGuide
SaConfigCloginShowLocalFilter
SaConfigCloginShowNisFilter
SaConfigCloginHideLocalFilter
SaConfigCloginHideNisFilter
getCloginConf__18SaConfigCloginDataGv
displayLocalOnly__18SaConfigCloginDataGv
getLocalOnlyFlag__12SaCloginConfGv
getShowFaceFlag__12SaCloginConfGv
getSmWinSizeFlag__12SaCloginConfGv
```

This recovers the actual `SaCloginConf` class that the archived config file comments pointed to, even though `SaCloginConf.h` itself is not currently present.

### `updateclogin` helper

The privileged helper contains:

```text
/var/sysadm/config/clogin.conf
/var/sysadm/config/clogin.conf.tmp
display faces
home directory must be local
home directory can be anywhere
use small window
%s:noshow
noshow
```

This strongly indicates an atomic rewrite pattern using `clogin.conf.tmp` followed by rename, and confirms that `updateclogin` serializes the same keyword vocabulary consumed by `clogin`.

## Source-mirror status

Direct searches for source code named `clogin`, `dtlogin`, `configClogin`, or `SaCloginConf` have not yet exposed the implementation/header in the 6.5.7m mirror. Current literal `clogin` hits are documentation/integration references rather than the main implementation.

This means the next searches should use:

- strings and dynamic symbols from the recovered `clogin` executable,
- `SaConfigCloginTask.so` symbols and embedded source-unit names,
- package/IDB entries for neighboring sysadm files,
- installed resource paths,
- X resource names,
- library dependencies and symbol names,
- related session utilities,
- historical CDE/xdm and Pandora source names where SGI may have retained upstream filenames.

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

After recovering the original binary and resources, several of these are now confirmed in broad semantics, but the prototype's exact filtering algorithm still diverges. In particular, a generic Linux minimum-UID cutoff is not supported by the recovered IRIX evidence and should not be treated as reconstructed SGI behavior.

The following face lookup order is now confirmed by the original `clogin` binary:

```text
$HOME/.icons/login.icon
/usr/local/lib/faces/$USER
/usr/lib/faces/$USER
```

The exact precedence and fallback rules still need function-level recovery from `findUserPicture()`.

## High-value targets to recover next

1. Reverse `findUserPicture__16PandoraComponentGP7xpasswd` to recover exact face/icon precedence and `machineicon`/`noicon` semantics.
2. Reverse `PwdNameList` methods to recover account enumeration, `noshow`, `ignore`, and `show anyway` precedence.
3. Reverse `checkAutoLogin__7pandoraGv` to recover one-shot autologin behavior exactly.
4. Recover `SaCloginConf` layout and methods from `SaConfigCloginTask.so`.
5. Recover `clogin` process/session startup and exact relationship to xdm/session scripts.
6. Decode or recover the `clogin.z` and `updateclogin.z` catman pages if useful.
7. Map the current Linux `dvlogin` compatibility layer onto the recovered behavior and remove unsupported UID-threshold assumptions.

## Confidence rule

- target 6.5.0 binary/resource evidence: primary truth
- matching 6.5.7m source/documentation: very strong support
- archived IRIX config/resource files: direct behavioral evidence
- current Linux `dvlogin` code: implementation reference only
- unsupported comments/assumptions: hypothesis until proven
