# ViewKit / ViewKlass integration

`clogin` is not a flat Motif application. The original IRIX binary imports the ViewKit object model directly through `libvk.so.1`.

Binary-proven imports used by Pandora include:

```text
VkApp
VkComponent
VkSimpleWindow
VkCallbackObject
VkGetResource
VkApp::setMainWindow()
VkSimpleWindow::addView()
VkComponent::baseWidget()
VkComponent::setDefaultResources()
```

The current Linux target uses ViewKlass as the first portability implementation of that interface.

The reconstruction is prefix-agnostic. Preferred discovery is through normal system prefixes, `CMAKE_PREFIX_PATH`, or an explicit `VIEWKLASS_ROOT` CMake cache variable. `/opt/MaXX` remains only as a transitional compatibility fallback for existing MaXX installations.

Examples:

```sh
cmake -S irix-re -B irix-re/build \
  -DCMAKE_PREFIX_PATH=/usr/local
```

or:

```sh
cmake -S irix-re -B irix-re/build \
  -DVIEWKLASS_ROOT=/some/prefix
```

A conventional installation can therefore use paths such as:

```text
/usr/local/include/Vk
/usr/local/lib/libVk.so
/usr/local/lib64/libVk.so
```

without any dependency on `/opt/MaXX`.

The top-level CMake build probes for ViewKlass automatically. When it is found, the `viewkit_probe` executable is built. It creates a real `VkApp`, `VkSimpleWindow`, and child view, so this is a build/runtime probe rather than a header-only check.

Build:

```sh
cmake -S irix-re -B irix-re/build
cmake --build irix-re/build --target viewkit_probe
```

Run from an X session:

```sh
./irix-re/build/viewkit/viewkit_probe
```

Expected result: a small ViewKlass window containing `ViewKlass is alive`.

## Pandora consequence

The reconstructed Pandora UI should be migrated onto this ownership model:

```text
VkApp
  -> PandoraWindow : VkSimpleWindow
       -> PandoraComponent : VkComponent
            -> PandoraComponentUI widget subtree
```

This matches the original binary imports and avoids treating ViewKit-owned shell/view relationships as arbitrary Motif attachments.

ViewKlass is a compatibility implementation, not evidence for unknown IRIX internals. Original `clogin` binary behavior and original SGI resources remain the source of truth when behavior differs.
