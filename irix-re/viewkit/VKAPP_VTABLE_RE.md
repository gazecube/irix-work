# VkApp vtable reverse-engineering notes

Target: IRIX 6.5.0 `usr/lib32/libvk.so.1`.

The original SGI C++ vtable is present as `__vtbl__5VkApp` at `0x0d6b8d30`, size `0x150` bytes. Entries are 12-byte SGI ABI records; for this class the first two words of each record are zero and the third word is the function pointer.

## Recovered VkApp vtable

```text
+0x000 RTTI/type record -> __T_5VkApp
+0x00c VkApp::~VkApp()
+0x018 VkApp::className()
+0x024 VkApp::show()
+0x030 VkApp::hide()
+0x03c VkComponent::realize()
+0x048 VkComponent::okToQuit()
+0x054 VkComponent::operator Widget() const
+0x060 VkComponent::setAttribute(const char *, void *)
+0x06c VkComponent::attributeList()
+0x078 VkComponent::getAttribute(const char *, void **)
+0x084 VkComponent::widgetDestroyed()
+0x090 VkApp::afterRealizeHook()
+0x09c VkApp::run()
+0x0a8 VkApp::terminate(int)
+0x0b4 VkApp::handlePendingEvents()
+0x0c0 VkApp::quitYourself()
+0x0cc VkApp::handleRawEvent(XEvent *)
+0x0d8 VkApp::raise()
+0x0e4 VkApp::lower()
+0x0f0 VkApp::iconify()
+0x0fc VkApp::open()
+0x108 VkApp::busyCursor()
+0x114 VkApp::normalCursor()
+0x120 VkApp::busy(const char *, VkSimpleWindow *)
+0x12c VkApp::veryBusy(const char *, VkSimpleWindow *)
+0x138 VkApp::notBusy()
+0x144 VkApp::progressing(const char *)
```

## Architectural consequence

`VkApp` is a `VkComponent`-derived object whose virtual interface keeps the full inherited component contract at the front and app-specific lifecycle/event/cursor operations after it.

This matches the already recovered `VkImdApp` vtable extension, whose final slots are:

```text
+0x150 VkImdApp::putenv(...)
+0x15c VkImdApp::umask(...)
+0x168 VkImdApp::saveEnv()
+0x174 VkImdApp::loadEnv()
```

Therefore the SGI desktop subclass extends the base application virtual interface rather than replacing ViewKit window/component behavior.

## Main-window state

`VkApp::setMainWindow(VkSimpleWindow *)` at `0x0d62fcc0` is exactly a store to `this + 0x64`.

`VkApp::mainWindow() const` at `0x0d631520` first returns `+0x64` when set and otherwise falls back to the registered window list.

This is shallow state, not an ownership transfer.

## High-value lifecycle hooks for reconstruction

The following vtable slots are the most relevant for Clogin/Pandora:

```text
+0x090 afterRealizeHook
+0x09c run
+0x0a8 terminate
+0x0b4 handlePendingEvents
+0x0c0 quitYourself
+0x0cc handleRawEvent
+0x0fc open
```

A portable ViewKit layer should preserve the distinction between `open()`, `show()`, realization, event-loop entry, and `afterRealizeHook()` rather than collapsing them into one helper if Clogin relies on ordering.

## What is not yet claimed

This note maps the virtual interface and already-proven main-window field. It does not yet assign semantic names to every non-virtual `VkApp` data member or fully reconstruct `VkAppInitialize()`.

The next lifecycle pass should trace:

- constructor initialization sequence,
- XtAppContext field and ownership,
- display/shell creation,
- window-list initialization,
- `run_first()` vs `run()`,
- exact realize/open/show ordering,
- `terminate()`/`quitYourself()` interaction.
