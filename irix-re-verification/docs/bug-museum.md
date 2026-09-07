# Bug Museum

A running collection of project-specific bugs, crashers, implementation oddities, and other cursed behavior encountered while reconstructing the IRIX Interactive Desktop.

This file is intentionally separate from `ai-mishaps.md`, which is reserved for assistant/context failures rather than bugs in the reimplementation itself.

## Exhibit 1: Refresh recursively negotiates geometry until Motif explodes

While the bootstrap file browser was still using an `XmList` as a temporary child of the reconstructed `ImdViewPanel`, in-place updates of the current directory view could send the realized widget hierarchy into a recursive geometry feedback loop.

The funniest part was the split behavior:

```text
works:
  double-click into a directory
  Go Up
  enter another path

crashes:
  Refresh
  Hidden Files
```

So ordinary directory navigation worked, including the Go Up button, while rebuilding the current view in place could make Motif die hard.

The initial symptom looked unrelated:

```text
segmentation fault (core dumped)
```

A debug backtrace showed the eventual crash inside MaXX Motif's input-method path:

```text
XmImSetValues
XmImVaSetValues
XtSetValues
XtVaSetValues
panelResizeCB
XtCallCallbackList
_XmGMDoLayout
_XmGMHandleGeometryManager
_XtMakeGeometryRequest
XtSetValues
XtVaSetValues
```

The actual bug was in the temporary `panelResizeCB()` shim. The callback handled a parent resize by calling `XtVaSetValues()` on the child `XmList` to change its geometry. That child geometry request re-entered Motif's geometry manager, which resized the parent, which invoked `panelResizeCB()` again, producing recursive geometry negotiation until Motif finally crashed in `XmImSetValues()`.

In short:

```text
Refresh / Hidden Files
    -> rebuild current XmList in place
    -> preferred geometry changes
    -> parent resize callback
    -> XtVaSetValues(child geometry)
    -> child geometry request
    -> parent geometry manager
    -> parent resize callback
    -> repeat until Motif dies
```

The fix is to configure the child from the parent's resize path with parent-side geometry operations such as `XtMoveWidget()` / `XtResizeWidget()` rather than issuing a new negotiated `XtSetValues()` geometry request from inside the resize callback.

Verdict: the filesystem code was innocent. The bootstrap geometry shim had built a recursive Motif feedback loop, and Motif eventually just died in the input-method code.
