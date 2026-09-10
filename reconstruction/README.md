# clogin reverse-engineering track

This directory tracks the cleanroom reconstruction of the IRIX Interactive Desktop visual login program `clogin` and its surrounding login-manager behavior.

## Ground truth

Primary truth for behavior and ABI is the shipped IRIX runtime evidence we are reconstructing:

- `/usr/Cadmin/bin/clogin` and companion shipped binaries;
- symbol-bearing shipped shared libraries used by Clogin;
- app-defaults, configuration files, icons, manpages, and other runtime resources;
- disassembly, symbol maps, strings, offsets, traces, and behavior observed from those artifacts.

Leaked IRIX source code is not reconstruction input. It must not supply implementation text, comments, function bodies, names, behavior, or documentation prose. It may be used only outside the reconstruction path for contamination auditing.

When a name or behavior is not recoverable from permitted runtime evidence, use a descriptive reconstructed name and mark it as reconstructed/best-guess rather than importing a name from leaked source.

See `../EVIDENCE_POLICY.md` for the repository-wide cleanroom rule.

## Existing Linux implementation

The repository root contains a runnable Linux/Motif login-manager port named `dvlogin`.

It provides the host-side integration needed to exercise reconstructed behavior, including PAM authentication, X11/Motif UI plumbing, session startup, and development-time portability code.

Treat that implementation as a portability/prototype layer, not as evidence for original SGI behavior.

## Reconstruction workflow

For each subsystem:

```text
original shipped IRIX binary/resources
    +
symbols/disassembly/observed behavior
    |
    v
recovered behavior and recoverable names
    |
    v
independently written reconstruction
    |
    v
portable Linux integration
```

Do not invent original names when permitted binary/resource evidence can recover them. When no original name can be recovered, clearly annotate a descriptive reconstruction name as such.

## Current reconstruction areas

The current track includes binary-backed work for:

- `PwdNameList` policy parsing and entry flags;
- `pandora::createIconList()` passwd/NIS enumeration behavior;
- `PandoraComponent::addUser()` and the pre-display user model collection;
- `checkDirectory()` and its caller-specific state handling;
- `PandoraComponent::findUserPicture()` lookup order and locality policy;
- `PandoraComponent::readyToDisplay()` and the Imd icon-panel hierarchy;
- `PandoraComponent::displayIcon()` model-to-`UserView` presentation path;
- `PandoraRenderer`, `PandoraLayout`, and selected Imd/FTR rendering behavior;
- password-entry handling and autologin marker consumption.

## Current implementation direction

The long-term implementation should follow the recovered Clogin architecture instead of treating the old per-user Motif shortcut widgets as the primary design:

```text
createIconList()
    -> addUser()
    -> component user-model collection

readyToDisplay()
    -> panelWindow
    -> scrolledWindow
    -> ImdIconPanel("panel")
    -> PandoraRenderer + PandoraLayout

displayIcon()
    -> iterate user-model collection
    -> construct UserView objects
    -> adopt views into ImdIconPanel
```

Host-specific compatibility code should remain visibly separated from recovered SGI behavior so that portability decisions cannot silently become claims about the original implementation.
