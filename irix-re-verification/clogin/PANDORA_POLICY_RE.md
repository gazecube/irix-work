# pandora display-policy fields

Recovered from `/usr/Cadmin/bin/clogin` in the extracted `sysadmdesktop` tree.

## Exact getters

The original `pandora` class exposes four trivial getters:

```text
getHowDisplay()  -> global config object +0x0c
getWhereIcons()  -> global config object +0x10
getWhereHome()   -> global config object +0x14
getWindowSize()  -> global config object +0x18
```

These offsets match the four boolean/integer policy fields reconstructed from `PwdNameList`:

```text
+0x0c display-icons mode (0 by default; `display faces` keeps 0, `display icons` sets 1)
+0x10 pictures-anywhere mode (1 by default; local=0, anywhere=1)
+0x14 home-anywhere mode (1 by default; local-only=0, anywhere=1)
+0x18 large-window mode (0 by default; small=0, large=1)
```

## Propagation into PandoraComponent

`pandora::initialize(PandoraComponent *, Display *)` copies policy into the component:

```text
PandoraComponent +0x98 = pandora.getWhereIcons()
PandoraComponent +0x9c = pandora.getWhereHome()
PandoraComponent +0x58 = pandora *        (set through PandoraComponent::setPandora)
```

The `+0x58` field is proven by the exact implementation of:

```text
setPandora__16PandoraComponentGP7pandora
```

which is only:

```text
sw a1, 0x58(a0)
return
```

## readyToDisplay interaction

`PandoraComponent::readyToDisplay(int,int,char)` calls `pandora::getHowDisplay()` through its `pandora *` and uses a return value of `1` to alter renderer/layout behavior. The exact UI meaning of that branch is still being mapped, but the policy field identity is no longer ambiguous.

## Confidence

High. Getter offsets, component propagation, and the configuration parser defaults/keywords agree across independent binary paths.
