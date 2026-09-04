# VkApp window registration and last-window exit

Target: IRIX 6.5.0 `usr/lib32/libvk.so.1`.

This note closes the missing lifecycle link between `VkSimpleWindow` destruction and `VkApp::terminate()`.

## Relevant original symbols

```text
0x0d636a60 addWindow__5VkAppGP14VkSimpleWindow
0x0d62d67c removeWindow__5VkAppGP14VkSimpleWindow
0x0d62fcc0 setMainWindow__5VkAppGP14VkSimpleWindow
0x0d641380 terminate__5VkAppGi
0x0d62e7c4 __dt__14VkSimpleWindowGv
0x0d6b8ee8 newWindowCallback__5VkApp
0x0d6b8eec windowDestroyedCallback__5VkApp
```

## Embedded window list layout

`VkApp` contains an embedded `VkComponentList` beginning at `this + 0x20`.

The list count is at:

```text
VkApp + 0x44
```

This is proven by the application-wide `open()`, `show()`, and `hide()` implementations, which loop from index zero while comparing against `this + 0x44` and retrieve components from the list beginning at `this + 0x20`.

Therefore `+0x44` is not an independent quit flag. It is the window-list count field inside the embedded list.

## VkApp::addWindow(VkSimpleWindow *)

Binary flow at `0x0d636a60`:

```text
callCallbacks(newWindowCallback, window)

if (windowList.count == 0) {
    if (mainWindow == NULL)
        setMainWindow(window)
}

windowList.add(window)
```

The callback-name object used by the first call is exactly:

```text
VkApp::newWindowCallback
```

The component-list add target is the embedded list at `this + 0x20`.

Architectural consequences:

- New-window notification occurs before insertion into the window list.
- The first registered window becomes the main window automatically only when no explicit main window is already present.
- `setMainWindow()` remains a shallow pointer store at `VkApp + 0x64`.

## VkApp::removeWindow(VkSimpleWindow *)

Binary flow at `0x0d62d67c`:

```text
callCallbacks(windowDestroyedCallback, window)
windowList.remove(window)

if (mainWindow == window)
    setMainWindow(NULL)

if (windowList.count == 0)
    terminate(0)
```

The callback-name object used by the first call is exactly:

```text
VkApp::windowDestroyedCallback
```

The list removal target is again the embedded `VkComponentList` at `this + 0x20`.

The final call is virtual through the `VkApp` vtable `terminate` slot, with argument zero.

For base `VkApp`, the implementation is already proven to be:

```cpp
void VkApp::terminate(int status)
{
    exit(status);
}
```

Therefore the normal base-class behavior is:

```text
last registered VkSimpleWindow removed
    -> VkApp::terminate(0)
    -> exit(0)
```

## VkSimpleWindow destructor link

`VkSimpleWindow::~VkSimpleWindow()` at `0x0d62e7c4` calls:

```text
VkApp::removeWindow(this)
```

through the global ViewKit application object before delegating into lower-level cleanup/base destruction.

This closes the previously missing lifecycle chain:

```text
VkSimpleWindow destruction
    -> VkApp::removeWindow(window)
        -> windowDestroyedCallback
        -> remove from embedded window list
        -> clear mainWindow if necessary
        -> if no windows remain: terminate(0)
            -> exit(0)
```

## Relationship to VkApp::quitYourself()

`quitYourself()` does not directly call `terminate()`.

Instead, it validates and deletes/hides windows according to `quitMode`. Once window destruction reaches the final registered `VkSimpleWindow`, the destructor/removal path above performs the actual application termination.

This explains the apparent separation observed in the earlier quit reconstruction:

```text
quitYourself()
    -> destroy accepted windows
    -> VkSimpleWindow::~VkSimpleWindow()
    -> VkApp::removeWindow()
    -> final list count becomes zero
    -> terminate(0)
```

## Reconstruction requirement

A faithful portable ViewKit implementation should preserve this separation:

1. `quitYourself()` performs policy and destruction.
2. `VkSimpleWindow` unregisters itself during destruction.
3. `VkApp::removeWindow()` owns the last-window termination rule.

Do not collapse these into a direct `quitYourself() -> exit()` shortcut, because that would bypass the original callback ordering, main-window clearing, and window-list lifecycle semantics.
