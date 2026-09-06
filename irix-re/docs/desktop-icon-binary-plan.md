# Desktop icon path: binary-proven implementation plan

This note is intentionally narrow. It records only evidence already recovered from preserved IRIX desktop binaries and the current reconstruction state.

## Proven reusable IMD stack

The preserved `libimdIconPanel.so` carries DWARF and proves the later IID icon stack contains these original source units:

- `IconPanel.c++`
- `IconRenderer.c++`
- `IconView.c++`
- `IconLayout.c++`
- `ModelBasedIconPanel.c++`
- `ModelBasedIconView.c++`
- `PositionOrder.c++`
- `SingleViewLayout.c++`
- `IconOnlyRenderer.c++`
- `InvertedTeeRenderer.c++`
- `DragManager.c++`
- `LaunchManager.c++`

The same binary exposes the corresponding `ImdIconPanel`, `ImdModelBasedIconPanel`, `ImdIconLayout`, `ImdIconRenderer`, `ImdIconOnlyRenderer`, and related methods. These are the classes currently being reconstructed for `fm`.

## Proven desktop service / window layer

The old desktop libraries and later IMD libraries overlap strongly enough to recover a direct lineage:

- `libdesktopIcon.so` -> `libimdXutil.so`
- `libdesktopUtil.so` -> `libimdXutil.so`
- `libdesktopFileicon.so` -> `libimdFtrDB.so`

Exact-name overlaps include desktop-service APIs such as:

- `AddIcon`
- `CleanUpDesk`
- `CloseDesk`
- `getDesktopWindow`
- `getBackgroundWindow`
- `getBackgroundDaemonWindow`
- `tellDesktop`

The later `libimdXutil.so` source-unit recovery also proves original files named:

- `getDtWindow.c++`
- `getBgWindow.c++`
- `getBgDWindow.c++`
- `tellDesktop.c++`
- `desktopLaunch.c`
- `imdProtocol.c++`
- `Launcher.c++`

This proves the desktop is not just an arbitrary root-window painter. It participates in a real desktop service/window protocol that survived into the IMD-era libraries.

## What is NOT yet proven

We do not yet have binary proof that the desktop executable itself directly instantiates `ImdModelBasedIconPanel`, `ImdIconLayout`, or any particular IMD renderer class.

Therefore the current `desktop-prototype/iid-desktop.c` must remain a prototype only. Its hand-written object layout, icon painter, shaped-window policy, and fixed Home/Dumpster placement are not to be promoted into the IID implementation until the relevant desktop binary path proves them.

## Implementation gate

Before replacing the prototype with the real desktop icon path, recover from the preserved desktop executable and/or its direct libraries:

1. desktop startup / main construction path;
2. exact widget or view class construction;
3. renderer class selection;
4. layout class selection and mapping persistence;
5. desktop object/model population path;
6. root/background window relationship;
7. selection and drag/drop wiring;
8. Home/Dumpster creation and placement semantics.

Only after those are recovered should the desktop target share `fm`'s reconstructed icon implementation. If the binary proves the same IMD classes are used, reuse them directly. If it proves a separate desktop renderer, follow that path instead.

## Current direction

The immediate reusable work remains binary-proving and completing the `fm` IMD render path. Once that renderer is alive, the desktop archaeology can wire the same implementation only where the desktop binary explicitly proves the relationship.
