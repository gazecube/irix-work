# VkSimpleWindow view ownership reverse-engineering notes

Target: IRIX 6.5.0 `usr/lib32/libvk.so.1`.

## Relevant symbols

```text
0x0d63931c VkSimpleWindow::addView(Widget)
0x0d63e210 VkSimpleWindow::addView(VkComponent *)
0x0d68236c VkSimpleWindow::removeView()
0x0d62e7c4 VkSimpleWindow::~VkSimpleWindow()
0x0d638ed0 VkQuickHelp::createVkQuickHelp(...)
```

## Stored view widget

`VkSimpleWindow::addView(Widget)` stores the supplied widget at object offset `+0x34`.

The previously recovered accessor `viewWidget()` returns this same field.

## addView(VkComponent *)

The component overload performs this binary-proven sequence:

```text
widget = component->baseWidget()
addView(widget)
component->show()
```

The final call resolves through the `VkComponent` virtual record at `+0x024`, which is the `show()` slot.

Therefore ViewKit inserts the component's own Xt widget into the window and then shows the component. It does not create a second ViewKit-owned view wrapper around the component.

## removeView()

`VkSimpleWindow::removeView()` is only:

```cpp
if (_viewWidget != NULL)
    XtUnmanageChild(_viewWidget);

_viewWidget = NULL;
```

There is no `XtDestroyWidget()` call and no C++ `delete` of a component.

This is strong ownership evidence: `removeView()` disconnects presentation but does not own the lifetime of the supplied view component.

## What the VkSimpleWindow destructor does own

The destructor examines a pointer in the window private-state block at:

```text
this + 0x18 -> private state
private + 0x0c -> internally-created component
```

Constructor disassembly proves this pointer receives the return value of:

```text
VkQuickHelp::createVkQuickHelp(...)
```

The destructor invokes that object's virtual destructor before continuing with window teardown.

Therefore the component explicitly deleted by `VkSimpleWindow::~VkSimpleWindow()` is the internal `VkQuickHelp` helper, not the object previously passed to `addView(VkComponent *)`.

## Window teardown sequence relevant to contained views

The destructor then:

1. removes the window's structure/state event handler from its base widget,
2. calls `VkApp::removeWindow(this)`,
3. invokes `VkComponent::~VkComponent()` for the window itself.

The base `VkComponent` destructor ultimately calls `XtDestroyWidget()` on the window's base widget. Xt recursively destroys the child widget hierarchy.

If a user-supplied view is itself a `VkComponent` whose base widget is a child of that hierarchy, its registered `widgetDestroyedCallback` runs and its virtual `widgetDestroyed()` path is used. The base implementation only clears that component's `_baseWidget` pointer.

Thus recursive Xt widget destruction still does not imply deletion of the contained C++ `VkComponent` object.

## Ownership model

The binary evidence supports this split:

```text
VkSimpleWindow owns:
  window widget hierarchy
  internal VkQuickHelp helper
  its own ViewKit/Xt registration state

VkSimpleWindow does not own:
  VkComponent passed to addView(VkComponent *)
```

The view component owns its own C++ lifetime. The window only manages and presents its widget.

## Reconstruction consequence

Portable ViewKit code should not make `VkSimpleWindow::removeView()` delete or destroy a supplied view component.

Likewise, destroying a `VkSimpleWindow` should allow Xt child destruction to invalidate the view component's base widget while leaving the component object alive unless some higher-level owner explicitly deletes it.

For Pandora this means `PandoraWindow` and `PandoraComponent` should remain separately owned objects. Window teardown may destroy Pandora's widget hierarchy, but should not silently transfer C++ ownership of `PandoraComponent` into `VkSimpleWindow`.
