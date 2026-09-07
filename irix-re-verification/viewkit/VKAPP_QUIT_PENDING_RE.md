# VkApp pending-event and quit-path reverse-engineering notes

Target: IRIX 6.5.0 `usr/lib32/libvk.so.1`.

Primary evidence is the original IRIX binary. Names below are taken from original dynamic symbols where available. Member semantics are only assigned when supported by disassembly/call behavior.

## VkApp::terminate(int)

Original symbol:

```text
0x0d641380 terminate__5VkAppGi
```

The implementation is a direct wrapper around libc `exit`:

```text
VkApp::terminate(int status)
{
    exit(status);
}
```

There is no additional ViewKit shutdown machinery inside this base implementation.

## VkApp::handleOnePendingEvent(...)

Original symbol:

```text
0x0d64120c handleOnePendingEvent__5VkAppGPGR7_XEvent_c
```

Binary-proven member offsets used here:

```text
+0x6c Display *
+0x70 XtAppContext
```

The function begins with:

```text
XSync(display, False);
pending = XtAppPending(appContext);
```

If `pending == 0`, it returns 0 immediately.

If `pending & 0x6` is nonzero, it calls:

```text
XtAppProcessEvent(appContext, pending);
```

and returns the original pending mask.

The `0x6` mask corresponds to the non-X-event Xt pending classes represented by timer/alternate-input bits in the classic Xt input mask layout. The binary itself proves the mask and branch; symbolic Xt enum interpretation should remain tied to the platform headers when a source-equivalent implementation is written.

Otherwise the function handles an X event explicitly:

```text
XtAppNextEvent(appContext, &event);

if (optional_filter != NULL && optional_filter(&event))
    return pending;

if (XtDispatchEvent(&event))
    return pending;

virtual handleRawEvent(&event);
handleRawHelpEvent(&event);
return pending;
```

The virtual dispatch used after `XtDispatchEvent` is the already-mapped `VkApp::handleRawEvent(XEvent *)` slot at vtable record `+0x0cc`.

`handleRawHelpEvent__5VkAppGPR7_XEvent` is a non-virtual helper at `0x0d6301d4`.

### Consequence

ViewKit distinguishes two pending-event paths:

1. timer/alternate-input style Xt work is handed directly to `XtAppProcessEvent`,
2. X events are pulled with `XtAppNextEvent` so ViewKit can apply its optional filter, normal Xt dispatch, then raw-event/help hooks if Xt did not consume the event.

This is stronger evidence than modeling `handlePendingEvents()` as a generic `XtAppProcessEvent(XtIMAll)` loop.

## VkApp::quitYourself()

Original symbol:

```text
0x0d64190c quitYourself__5VkAppGv
```

### Reentrancy guard

The function checks byte member:

```text
this + 0x48
```

against 1. If already 1 it returns immediately. Otherwise it sets the byte to 1 for the duration of the quit sequence and clears it again before return.

Semantic name is reconstructed as a quit/reentrancy-in-progress flag; the original member name is not yet recovered.

### quitMode resource

The function calls original ViewKit helper:

```text
VkGetResource(...)
```

for resource:

```text
quitMode
```

with default string:

```text
each
```

It compares the resolved value case-insensitively against:

```text
all
```

using `strcasecmp`.

Thus the original ViewKit application shutdown behavior has at least these two policies:

```text
quitMode = each
quitMode = all
```

The exact public documentation wording for these modes is not claimed here; the strings and control-flow distinction are binary-proven.

### Snapshot of current components

Before applying quit policy, `quitYourself()` constructs a temporary `VkComponentList` on the stack and copies the application's current component/window entries into it.

Original calls used:

```text
VkComponentList::VkComponentList()
VkComponentList::operator[](int) const
VkComponentList::add(VkComponent *)
VkComponentList::~VkComponentList()
```

The live application list is accessed through an inline list object beginning at approximately `this + 0x20`, with the observed entry count at `this + 0x44`.

This snapshot is important: quit processing operates on a stable list even when callbacks/destructors mutate the application's live registered-window list.

### Virtual operations recovered in quit path

The temporary-list entries are checked with:

```text
VkComponent::isComponent(VkComponent *)
```

and the following already-mapped component virtuals are invoked:

```text
vtable +0x048  VkComponent::okToQuit()
vtable +0x030  VkComponent::hide()
vtable +0x00c  destructor
```

The destructor dispatch receives argument value 3 in the observed delete path, consistent with the SGI compiler's deleting-destructor convention. This note does not generalize that ABI flag beyond the observed call.

### `quitMode != all` branch

For modes other than `all` - including the binary default `each` - the function first walks the temporary component list backwards and checks every still-live component with `okToQuit()`.

If any component returns false, the destructive phase is abandoned.

If all checked components permit quitting, a second backwards pass invokes the deleting destructor on each surviving component.

This establishes an all-permission-first, then destroy phase for the default/non-`all` branch.

### `quitMode == all` branch

For `all`, the function walks the temporary list backwards and treats the application's `mainWindow()` specially.

Non-main-window components are considered individually. For a surviving component that permits quitting via `okToQuit()` the observed path performs:

```text
component->hide();
delete component;
```

After the non-main entries are processed, `mainWindow()` is fetched again and, if it still exists and is still a registered component, its `okToQuit()` result controls its final deleting-destructor call.

The repeated `mainWindow()` and `isComponent()` checks are significant: ViewKit assumes prior component operations may have changed the live object graph during shutdown.

### End of function

Before return:

```text
this[0x48] = 0;
```

and the temporary `VkComponentList` is destroyed.

`quitYourself()` itself does not directly call `VkApp::terminate()` in the recovered function body. Any process termination caused by closing/deleting the final window therefore happens through surrounding window/component lifecycle behavior rather than an unconditional `exit()` at the end of `quitYourself()`.

## Vtable status

There are no remaining unidentified `VkApp` vtable records in the recovered base vtable through `+0x144`; `VKAPP_VTABLE_RE.md` maps all entries. The pending-event decode above confirms the `+0x0cc` raw-event slot from an independent dynamic call site.

## Reconstruction consequences

A faithful portable ViewKit layer should preserve all of the following:

- `terminate(int)` is a direct process exit in the base class.
- `handleOnePendingEvent()` differentiates non-X Xt work from explicit X-event dispatch.
- optional event filters run before `XtDispatchEvent`.
- raw-event/help hooks run only when normal Xt dispatch does not consume the event.
- quit processing is reentrancy-guarded.
- quit behavior is controlled by the `quitMode` resource with default `each` and special value `all`.
- quit processing snapshots the component list before mutation-prone operations.
- `okToQuit()` is a real virtual veto point.
- `mainWindow()` receives special handling in the `all` path.
- `quitYourself()` does not itself unconditionally terminate the process.

## Next useful binary work

The next pass should decode the application window-registration/removal path, especially the code that updates the inline list at `VkApp +0x20` and count at `+0x44`. That should explain exactly when deleting the last `VkSimpleWindow` transitions into application termination, completing the chain between `quitYourself()` and `terminate()`.
