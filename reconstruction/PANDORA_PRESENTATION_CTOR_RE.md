# Pandora presentation constructor reconstruction

Primary target: fresh extracted IRIX `/usr/Cadmin/bin/clogin` from `sysadmdesktop`.

This note records constructor chains and the user presentation payload recovered directly from the executable. These findings supersede the earlier provisional interpretation of `UserView(const ImdString &, long, long)`.

## UserView

Dynamic symbol:

```text
0x100179c4 __ct__8UserViewGRC9ImdStringlT1
```

The constructor directly calls `ImdIconView::ImdIconView()`, installs the `UserView` vtable, stores the scalar argument as UID, then copies two `ImdString` values into the object.

Recovered logical signature:

```c++
UserView(const ImdString &login, long uid, const ImdString &home)
```

The old SGI mangling `GRC9ImdStringlT1` uses `T1` to repeat the first argument type, so the final argument is another `const ImdString &`, not a second `long`.

The corresponding model object produced by `PandoraComponent::addUser()` is recovered as:

```text
+0x00 ImdString login
+0x08 ImdString full name / GECOS
+0x10 long uid
+0x14 ImdString home
```

`displayIcon()` passes the login field, UID field, and home field directly to `UserView`.

Direct base chain:

```text
ImdView
  -> ImdIconView
      -> UserView
```

## PandoraLayout

`PandoraLayout::PandoraLayout(Widget)` directly calls `ImdLayout::ImdLayout(Widget)`.

The constructor then installs the `PandoraLayout` vtable and initializes two class fields to decimal `10` and `12`. Their semantics are not assigned yet.

Direct base chain proven from constructor call:

```text
ImdLayout
  -> PandoraLayout
```

Do not insert `ImdIconLayout` into this chain without new evidence.

## PandoraRenderer

`PandoraRenderer::PandoraRenderer()` directly calls `ImdInvertedTeeRenderer::ImdInvertedTeeRenderer()` and then installs the `PandoraRenderer` vtable.

This proves the renderer chain is:

```text
ImdRenderer
  -> ImdIconRenderer
      -> ImdInvertedTeeRenderer
          -> PandoraRenderer
```

The intermediate `ImdIconRenderer` relationship is independently supported by the `libimdIconPanel.so` class inventory and the `ImdInvertedTeeRenderer` method surface.

This supersedes the earlier provisional `PandoraRenderer : ImdRenderer` scaffold.

## Consequences

- `UserView` should carry login, UID, and home directly.
- `PandoraRenderer` should be modeled on `ImdInvertedTeeRenderer`, not generic `ImdRenderer`.
- `PandoraLayout` is a direct `ImdLayout` subclass unless further constructor evidence proves otherwise.
- The temporary Motif `XmDrawnButton` and `XmRowColumn` objects remain portable rendering/layout backends only; they are not claims about the original IRIX implementation.
