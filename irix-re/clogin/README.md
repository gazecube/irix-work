# clogin reverse-engineering track

This directory tracks reconstruction of the IRIX Interactive Desktop visual login program `clogin` and its surrounding login-manager behavior.

## Ground truth

Primary truth for behavior and ABI remains the target IRIX 6.5.0-era binaries/resources we are reconstructing.

The pinned IRIX 6.5.7m source tree under `../evidence/irix-657m-src/` is a strong reference for names, system integration, configuration semantics, filesystem paths, and related XDM/login infrastructure. If 6.5.7m source and the target binary disagree, trust the target binary.

## Confirmed system-level behavior

From `eoe/man/man4/visuallogin.4` in the 6.5.7m source tree:

- `visuallogin=on` with `windowsystem=on` selects the visual login program.
- 4Sight systems use `pandora`.
- IRIX Interactive Desktop systems use `clogin`.
- `visuallogin=off` with `windowsystem=on` selects standard `xdm`.
- both `windowsystem=off` and `visuallogin=off` fall back to standard IRIX `login`.
- `noiconlogin=off` allows user icons to be displayed.
- `noiconlogin=on` suppresses user icons.
- `clogin` can also configure icon-login behavior through `configClogin`.

Relevant IRIX configuration files:

- `/etc/config/visuallogin`
- `/etc/config/noiconlogin`

## Existing Linux implementation

The repository root currently contains an experimental Linux/Motif login manager named `dvlogin`.

Current documented behavior includes:

- PAM authentication
- optional password visibility
- optional hostname label
- launching `/usr/bin/xmsession`
- unfinished/hard-coded user shortcuts

Current build dependencies include Motif, X11/Xt, Xpm, Xcursor, Xinerama, pthreads, and PAM.

Treat this implementation as a portability/prototype layer, not as evidence for original SGI behavior.

## Reconstruction workflow

For each subsystem:

```text
IRIX binary/resources
    +
6.5.7m source/docs
    +
strings/symbols/disassembly
    |
    v
recovered behavior and names
    |
    v
portable Linux implementation
```

Do not invent original names when a symbol, resource, manpage, source file, or binary reference can recover them.

## Initial worklist

1. Inventory `clogin`, `configClogin`, XDM, visual-login, and session-related references in the 6.5.7m tree.
2. Inventory any available `clogin`/greeter binaries and resources from the target IRIX installation.
3. Recover process boundaries: display manager, greeter/UI, authentication, session startup, logout/reset.
4. Recover configuration/resource files and exact lookup paths.
5. Compare SGI-specific behavior against inherited XDM behavior.
6. Map the current root `dvlogin` implementation onto the recovered architecture.
7. Replace hard-coded prototype behavior with reconstructed behavior incrementally.

## Current finding

A direct source-code hit for `clogin` or `dtlogin` has not yet been found in the 6.5.7m mirror under those literal names. The tree does contain authoritative system documentation and integration references. Search should continue using component/resource names, installed-file manifests, XDM internals, and related utilities rather than assuming the source directory is named `clogin`.
