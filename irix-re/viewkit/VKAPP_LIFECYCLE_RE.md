# VkApp lifecycle and event-loop reconstruction

Target: IRIX 6.5.0 libvk.so.1, N32 MIPS-III build from the recovered shared-object corpus.

This note records only behavior directly recovered from the original IRIX binary. MaXX/ViewKlass headers are secondary reference and are not used as proof of implementation details.

## Relevant exported symbols

- VkAppInitialize__5VkAppGPcPiPPcP3ArgUiPGP10_WidgetRec_vP16XrmOptionDescReci at 0x0d634e60, size 1460
- run_first__5VkAppGv at 0x0d635980, size 64
- run__5VkAppGv at 0x0d62ffe8, size 60
- runOneEvent__5VkAppGPGR7_XEvent_c at 0x0d630068, size 168
- terminate__5VkAppGi at 0x0d641380, size 52
- handlePendingEvents__5VkAppGv at 0x0d641458, size 96
- handlePendingEvents__5VkAppGPGR7_XEvent_c at 0x0d6414b8, size 104
- quitYourself__5VkAppGv at 0x0d64190c, size 892
- open__5VkAppGv at 0x0d641c88, size 140
- afterRealizeHook__5VkAppGv at 0x0d635a44, size 8

## VkApp::run()

Binary behavior at 0x0d62ffe8:

1. Save `this`.
2. Call `run_first()`.
3. Repeatedly call `runOneEvent(this, NULL)` forever.

Recovered equivalent shape:

```cpp
void VkApp::run()
{
    run_first();
    for (;;) {
        runOneEvent(0);
    }
}
```

The exact C++ declaration of the null callback/filter argument should be taken from the matching public declaration, but the runtime behavior is proven.

## VkApp::run_first()

At 0x0d635980:

- clears byte `this + 0x7c`
- performs one virtual dispatch through the VkApp vtable

The exact semantic name of the byte at +0x7c and the virtual slot should be kept unresolved until the slot is matched against the already-recovered VkApp vtable map. Do not assign a reconstructed name yet.

## VkApp::runOneEvent()

At 0x0d630068 the sequence is:

1. Call `XtAppNextEvent(this + 0x70, &event)`.
2. If an optional caller-supplied event callback/filter is non-NULL, call it with `&event`.
3. If that callback returns nonzero, return immediately without dispatching the event.
4. Call `XtDispatchEvent(&event)`.
5. If `XtDispatchEvent` returns nonzero, return immediately.
6. Perform a VkApp virtual dispatch with the event pointer. This is the raw-event hook in the previously recovered vtable family; exact slot/name should be cross-checked against the vtable note rather than guessed from position alone.
7. Call `handleRawHelpEvent(this, &event)`.
8. Return.

The GOT entries resolve exactly to:

- 0x0d6c7220 -> XtAppNextEvent
- 0x0d6c7224 -> XtDispatchEvent
- 0x0d6c7884 -> handleRawHelpEvent__5VkAppGP7_XEvent

This proves that ViewKit does not replace the Xt event loop. It wraps the normal Xt queue/dispatch path with a caller filter, one application virtual hook, and ViewKit help-event handling.

## VkApp::handlePendingEvents()

The no-argument form at 0x0d641458 is a tight loop around `handleOnePendingEvent(this, NULL)`:

```cpp
while (handleOnePendingEvent(0)) {
}
```

The callback/filter form at 0x0d6414b8 is the same loop while forwarding the caller-supplied filter:

```cpp
while (handleOnePendingEvent(filter)) {
}
```

The GOT entry at 0x0d6c7878 resolves to `handleOnePendingEvent`.

This is important for reconstruction: pending-event draining is framework convenience around a single-event primitive, not a separate event subsystem.

## VkApp::afterRealizeHook()

At 0x0d635a44 the base implementation is exactly:

```asm
jr   $ra
nop
```

Therefore the base `VkApp::afterRealizeHook()` is intentionally empty. Any behavior observed there in a derived application must come from the subclass override, not hidden base-class logic.

## VkApp::open()

At 0x0d641c88:

- reads a count at `this + 0x44`
- indexes a ViewKit component/window list rooted at `this + 0x20`
- for each returned object, performs a virtual call on that object

The list indexing call resolves to `VkComponentList::operator[](int)`.

This proves that `VkApp::open()` iterates the application's registered window/component collection and opens each object through virtual dispatch. The exact object-list member name and virtual slot should be copied from the recovered vtable/class map only after cross-checking, not guessed here.

## End of VkAppInitialize(): widget realization chain

The final portion of `VkAppInitialize()` is now directly resolved.

After the application shell/base widget has been constructed and stored, the function calls:

1. `VkComponent::installDestroyHandler(this)`
2. `VkComponent::getResources(...)`
3. `VkComponent::setDefaultResources(...)`
4. `XtVaGetValues(baseWidget, ...)`
5. `XtVaSetValues(baseWidget, ...)`
6. an optional caller-supplied initialization callback when non-NULL
7. `XtRealizeWidget(baseWidget)`
8. a VkApp virtual hook immediately after realization
9. `XtWindow(baseWidget)`
10. `XDefineCursor(display/window, cursor)`
11. `VkInitHelp()`

Resolved GOT entries for this tail include:

- 0x0d6c7bd8 -> installDestroyHandler__11VkComponentGv
- 0x0d6c7bec -> getResources__11VkComponent...
- 0x0d6c7be8 -> setDefaultResources__11VkComponent...
- 0x0d6c7204 -> XtVaGetValues
- 0x0d6c7208 -> XtVaSetValues
- 0x0d6c7288 -> XtRealizeWidget
- 0x0d6c728c -> XtWindow
- 0x0d6c7290 -> XDefineCursor
- 0x0d6c824c -> VkInitHelp

The virtual call immediately after `XtRealizeWidget` is consistent with the already-recovered `afterRealizeHook` family, but the exact slot mapping should be cited from the vtable note before treating that name as fully resolved.

## Initialization state proven from VkAppInitialize()

Early `VkAppInitialize()` explicitly zeroes or initializes several VkApp members before creating the Xt application shell:

- `this + 0x64 = 0` (previously proven `_mainWindow`)
- `this + 0x68 = 0`
- `this + 0x6c = 0`
- `this + 0x70 = 0` before Xt application-context creation
- `this + 0x74 = 0`
- `this + 0x78 = 0`
- byte `this + 0x48 = 0`
- words `this + 0x4c`, `+0x50`, `+0x54` = 0

The function later stores the Xt application context at `this + 0x70`, confirming that offset's role.

Semantic names for the remaining unresolved fields should not be invented until matched against symbols/DWARF/derived-class usage.

## Architectural conclusion

IRIX ViewKit's core application lifecycle is deliberately thin:

```text
VkApp initialization
  -> configure Xt application shell/resources
  -> install component destroy coupling
  -> realize shell
  -> invoke virtual post-realize hook
  -> install cursor/help integration

VkApp::run
  -> one first-run virtual hook
  -> XtAppNextEvent
  -> optional caller filter
  -> XtDispatchEvent
  -> app raw-event hook
  -> ViewKit help-event hook
  -> repeat
```

This is strong evidence that Clogin/Pandora should rely on ordinary Xt/Motif realization and event semantics wherever possible. ViewKit contributes lifecycle glue and virtual extension points, not a separate rendering or event model.

## Next work

1. Cross-reference the two unresolved virtual calls in `run_first()` and the end of `VkAppInitialize()` against `VKAPP_VTABLE_RE.md`.
2. Decode `handleOnePendingEvent()` so its exact Xt polling primitive and filter behavior are documented.
3. Decode `terminate()` and the high-level branches in `quitYourself()`.
4. Feed the proven realization ordering into the portable ViewKit/Pandora layer only where it removes reconstructed convenience behavior without changing current working Clogin presentation.
