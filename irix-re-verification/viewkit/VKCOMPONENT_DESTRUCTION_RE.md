# VkComponent destruction reverse-engineering notes

Target: IRIX 6.5.0 `usr/lib32/libvk.so.1`.

## Relevant symbols

```text
0x0d62f590 VkComponent::~VkComponent()
0x0d62f604 VkComponent::destroyVkComponent()
0x0d62d7a0 VkComponent::widgetDestroyedCallback(Widget, void *, void *)
0x0d62d7e8 VkComponent::widgetDestroyed()
0x0d6369a0 VkComponent::installDestroyHandler()
0x0d62feac VkComponent::removeDestroyHandler()
```

## Destroy callback installation

`installDestroyHandler()` registers `VkComponent::widgetDestroyedCallback` on the component's base widget (`this + 0x10`) using Xt callback registration and the component pointer as client data.

`removeDestroyHandler()` removes that same callback from the current base widget.

## External widget destruction path

`widgetDestroyedCallback(widget, component, callData)` first compares the callback widget against `component->_baseWidget` at `+0x10`.

If the widget is not the current base widget, the callback returns immediately.

If it matches, the callback dispatches the component virtual slot at vtable record `+0x084`, which is `VkComponent::widgetDestroyed()` in the base class.

The base implementation is exactly:

```cpp
void VkComponent::widgetDestroyed()
{
    _baseWidget = 0;
}
```

Therefore external Xt destruction of the widget does not delete the C++ `VkComponent` object. It only notifies the component and, in the base implementation, clears its widget pointer.

Derived classes may override `widgetDestroyed()`.

## Explicit C++ destruction path

`VkComponent::~VkComponent()` installs the base-class vtable and then calls `destroyVkComponent()` before running the `VkCallbackObject` base destructor.

`destroyVkComponent()` performs this sequence:

```text
callCallbacks(deleteCallback, NULL)

if (_baseWidget != NULL) {
    XtRemoveCallback(_baseWidget,
                     destroy-callback resource,
                     VkComponent::widgetDestroyedCallback,
                     this)

    XtDestroyWidget(_baseWidget)
    _baseWidget = NULL
}

free(_name)
_name = NULL

operator delete(_privateState)
_self = NULL
```

The callback is deliberately removed before `XtDestroyWidget()`. This prevents the component's own destroy callback from re-entering `widgetDestroyed()` while the C++ object is already being torn down.

## Recovered member consequences

Previously recovered `VkComponent` fields remain consistent with the destruction code:

```text
+0x0c name string
+0x10 base Widget
+0x14 pointer to the base-widget slot
+0x18 private state allocation
+0x1c self pointer
```

The destructor frees the name, destroys/frees the private state, and clears the self field.

## Ownership consequence

There are two deliberately distinct lifetime directions:

### Widget dies first

```text
Xt destroys base widget
  -> widgetDestroyedCallback
     -> virtual widgetDestroyed()
        -> base implementation sets _baseWidget = NULL

VkComponent object remains alive
```

### Component dies first

```text
VkComponent::~VkComponent()
  -> call delete callbacks
  -> remove Xt destroy callback
  -> XtDestroyWidget(_baseWidget)
  -> free component-owned state
  -> VkCallbackObject::~VkCallbackObject()
```

A portable reconstruction should preserve this asymmetry. Destroying the widget must not implicitly `delete this`, and destroying the C++ component should suppress its own widget-destroy notification while recursively destroying its widget hierarchy.

## Clogin consequence

For Pandora/ViewKit reconstruction, a `VkComponent` may remain as a live C++ object after its Xt widget has been destroyed by an owning parent widget. Code should therefore tolerate `baseWidget() == NULL` after parent/window teardown instead of assuming the component object vanished with the widget.
