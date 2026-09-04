# Pandora icon panel reconstruction

Target binary: `/usr/Cadmin/bin/clogin`

Primary target symbol:

```text
0x100115e8 PandoraComponent::readyToDisplay(int, int, char)
```

This document records the binary-proven construction of the user icon panel.
Names and values below come from the uploaded IRIX `clogin` binary and the
symbol-bearing IRIX `libimdIconPanel.so`, not from visual guessing.

## ImdIconPanel constructor ABI

`libimdIconPanel.so` exports:

```text
0x09682ec8 ImdIconPanel::ImdIconPanel(const char *, Widget, Arg *, int)
```

Mangled symbol:

```text
__ct__12ImdIconPanelGPCcP10_WidgetRecP3Argi
```

`clogin` allocates 0x64 bytes for the object immediately before the constructor
call and then invokes the constructor with:

```text
name   = "panel"
parent = scrolledWindow
args   = Arg array at stack +0x108
count  = current Arg count
```

The instance name is binary-proven from the string at virtual address
`0x10033790`.

## Visual argument setup

Before adding its own icon-panel resources, `readyToDisplay()` calls:

```text
imdFtrChooseVisualArgs(this + 0x10, 2, args, &count, stack +0x198)
```

The exact meaning of the final output object at stack +0x198 is not yet named;
do not invent a name for it.

The return value from `imdFtrChooseVisualArgs()` is reduced to a boolean and is
used for both `installColormap` and `ditherBackground`.

## Exact Arg resources added by Clogin

After `imdFtrChooseVisualArgs()`, the binary appends exactly eight `Arg` pairs.
Each `Arg` is the N32 8-byte `(name, value)` pair.

```text
installColormap    = chooseVisualResult != 0
ditherBackground   = chooseVisualResult != 0
horzScrollPolicy   = 0
vertScrollPolicy   = 1
dropEnabled        = 0
dragEnabled        = 0
boxSelectEnabled   = 0
renameEnabled      = 0
```

The resource strings are binary-proven from `.rodata`:

```text
0x1002f040 installColormap
0x1002f050 ditherBackground
0x1002f068 horzScrollPolicy
0x1002f080 vertScrollPolicy
0x1002f098 dropEnabled
0x1002f0a8 dragEnabled
0x1002f0b8 boxSelectEnabled
0x1002f0d0 renameEnabled
```

The count is incremented by eight before the `ImdIconPanel` constructor call.

## Runtime hierarchy

The binary creates the icon area in this order:

```text
panelWindow
  -> XmCreateScrolledWindow("scrolledWindow", NULL, 0)
  -> XtManageChild(scrolledWindow)
  -> ImdIconPanel("panel", scrolledWindow, args, count)
```

The `scrolledWindow` string is at `0x1002f030`.

## Renderer

`readyToDisplay()` allocates 0x90 bytes and constructs:

```text
PandoraRenderer::PandoraRenderer()
```

Symbol:

```text
0x100184b4 PandoraRenderer::PandoraRenderer()
```

The renderer is then adopted by the panel through:

```text
ImdViewPanelAdoptRenderer(panelWidget, renderer)
```

The exact panel widget is obtained through an `ImdIconPanel` virtual method
immediately before the adopt call.

`pandora::getHowDisplay()` is consulted after renderer adoption. When its return
value is not 1, the binary invokes a renderer virtual method with boolean `1`.
The inherited `ImdIconRenderer::setGallery(bool)` symbol exists and is a strong
candidate for that virtual slot, but this call remains marked as an inference
until the vtable slot is cross-checked directly.

## Layout

`readyToDisplay()` allocates 0x14 bytes and constructs:

```text
PandoraLayout::PandoraLayout(Widget)
```

Symbol:

```text
0x10017fcc PandoraLayout::PandoraLayout(Widget)
```

The constructor receives the panel widget obtained from the `ImdIconPanel`.
The result is adopted with:

```text
ImdViewPanelAdoptLayout(panelWidget, layout)
```

## Scale

The icon panel scale is binary-proven:

```text
ImdViewPanelSetScale(panelWidget, 1.5f, 0, 0)
```

The float constant `1.5f` is stored at virtual address `0x10033a60` as
big-endian IEEE-754 `0x3fc00000`.

Immediately after that, the renderer receives a virtual call with float
`2.0f`. The constant is stored at `0x10033a64` as `0x40000000`. The exact
virtual method name remains to be cross-checked before assigning it.

## Callback wiring

The two callback registrations are exact:

```text
panel.addCallback(
    ImdIconPanel::kCommandCallback,
    PandoraComponent::commandCB,
    this)

panel.addCallback(
    ImdIconPanel::kSelectionChangedCallback,
    PandoraComponent::selectCB,
    this)
```

Binary symbols:

```text
ImdIconPanel::kCommandCallback
ImdIconPanel::kSelectionChangedCallback
PandoraComponent::commandCB(VkCallbackObject *, void *, void *)
PandoraComponent::selectCB(VkCallbackObject *, void *, void *)
```

The panel is then shown through a virtual method at the end of the setup block.
The exact method name must be recovered from the `ImdIconPanel` vtable before
naming it.

## Cross-check against libimdIconPanel.so

The symbol-bearing IRIX library confirms these exported interfaces:

```text
ImdIconPanel::ImdIconPanel(const char *, Widget, Arg *, int)
ImdViewPanelAdoptRenderer(Widget, ImdRenderer *)
ImdViewPanelAdoptLayout(Widget, ImdLayout *)
ImdViewPanelSetScale(Widget, float, long, long)
ImdIconPanel::kSelectionChangedCallback
ImdIconPanel::kCommandCallback
```

Relevant original addresses in `libimdIconPanel.so`:

```text
0x09682ec8 ImdIconPanel constructor
0x09680380 ImdViewPanelAdoptRenderer
0x096802f4 ImdViewPanelAdoptLayout
0x09687afc ImdViewPanelSetScale
```

## Current reconstruction boundary

The ViewKit probe already reconstructs the binary-proven Motif container:

```text
panelWindow
  -> scrolledWindow
```

The next implementation step is to reconstruct the minimal `ImdIconPanel`
surface required by `clogin` using the original constructor and exported
interfaces above. Any Linux-only backing widget or storage used internally must
be marked as reconstructed implementation detail; the public names and behavior
should remain aligned with the recovered IRIX interfaces.
