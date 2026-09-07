# VkImdApp reverse-engineering notes

Target: IRIX 6.5.0 `VkImdApp` as implemented by `libimdXutil.so`.

## Primary evidence

Binary:

`usr/lib32/internal/libimdXutil.so`

The binary retains debug strings identifying the original translation unit as:

`VkImdApp.c++`

Compiler provenance embedded in the object identifies the SGI N32/MIPS3 build environment used for this library.

## Recovered symbols

```text
097a3a78 __ct__8VkImdAppGPcPiPPcP16XrmOptionDescReci
097afac8 __dt__8VkImdAppGv
097a3988 terminate__8VkImdAppGi
097afd74 putenv__8VkImdAppGPCc
097afca0 umask__8VkImdAppGUl
097afb8c saveEnv__8VkImdAppGPCc
097a34dc loadEnv__8VkImdAppGv
097a37b8 _doPutenv__8VkImdAppGPCc
097a36ac _initConfigWatcher__8VkImdAppGv
097a35e8 _getPath__8VkImdAppSGv
097a35ac _envVarsChangeCB__8VkImdAppSGP16VkCallbackObjectPvT2
097da370 __vtbl__8VkImdApp
```

Debug strings also recover the original identifiers:

```text
DESKTOP_SESSION
desktopEnv
theVkImdApp
envManager
```

## Constructor

`VkImdApp::VkImdApp(...)` at `0x097a3a78` first calls the base `VkApp` constructor:

```text
__ct__5VkAppGPcPiPPcP16XrmOptionDescReci
```

It then installs the `VkImdApp` vtable and initializes the derived state.

Binary-proven derived offsets:

```text
+0x80  envManager
+0x84  ImdConfigWatcher *
```

The `+0x80` object is allocated and constructed as an `ImdDictionaryOfTempl`. Debug strings name this member `envManager`.

The constructor zeros `+0x84`, publishes the current object through the recovered global `theVkImdApp`, then performs this high-level sequence:

```text
VkApp::VkApp(...)
install VkImdApp vtable
create envManager
configWatcher = NULL
theVkImdApp = this
status = _getPath()
if (status != 1)
    status = loadEnv()
if (status == 0)
    _initConfigWatcher()
VkApp::useSGIHelp()
```

The distinction between `_getPath()` return values is binary-proven, but their semantic enum names are not yet recovered.

## desktopEnv path

`_getPath()` at `0x097a35e8` calls:

```text
imdCreateSessionPath(NULL, 1, &status)
```

If a session path is returned, the function copies it into the static `desktopEnv` buffer and appends:

```text
/
desktopenv
```

Therefore the environment persistence file is built as:

```text
<Imd session path>/desktopenv
```

The string `DESKTOP_SESSION` is also present in the same original source unit and is part of this environment/session subsystem.

## Environment manager behavior

`_doPutenv()` at `0x097a37b8` is the internal environment mutation path.

The disassembly proves that it:

1. duplicates the supplied `NAME=VALUE` string,
2. locates the `=` separator with `strchr`,
3. builds an `ImdString` key for the variable name,
4. checks/removes any previous entry for that key in `envManager`,
5. queries the current process environment with `getenv`,
6. preserves prior environment state in the dictionary when necessary,
7. calls libc `putenv` with the new string.

This means `envManager` is not simply the current environment. It tracks enough prior state to restore mutations during application termination.

## terminate()

`VkImdApp::terminate(int)` at `0x097a3988` constructs an iterator over `envManager`.

For every saved entry it removes the item from the dictionary and releases the associated storage, then delegates to:

```text
VkApp::terminate(int)
```

This proves that `VkImdApp` adds environment cleanup/restoration semantics around the otherwise normal `VkApp` termination path.

The exact value-restoration operation for each iterator entry still needs a final pass through the dictionary item representation before source-equivalent code is claimed.

## Config watcher

`_initConfigWatcher()` at `0x097a36ac` is guarded by:

```text
if (this->configWatcher != NULL)
    return;
```

It constructs an `ImdString` containing the recovered `desktopEnv` path and allocates an `ImdConfigWatcher` using:

```text
ImdConfigWatcher(this->appContext, desktopEnv)
```

The `XtAppContext` used here is loaded from `VkApp` offset `+0x70`.

The watcher pointer is stored at `VkImdApp + 0x84`.

The method then registers `VkImdApp::_envVarsChangeCB` with the watcher through `VkCallbackObject::addCallback()` and calls:

```text
ImdConfigWatcher::beginWatching()
```

If watcher startup reports failure, the watcher is destroyed and `+0x84` is reset to NULL.

This is direct IRIX evidence that the desktop environment file was live-watched through the Xt application context.

## Destructor

`VkImdApp::~VkImdApp()` at `0x097afac8`:

1. restores the `VkImdApp` vtable,
2. clears the recovered `theVkImdApp` global,
3. destroys the config watcher at `+0x84` when present,
4. destroys the environment dictionary at `+0x80`,
5. calls `VkApp::~VkApp()`,
6. conditionally deletes the complete object for the deleting-destructor variant.

No separate window-management layer is introduced here.

## VkImdApp vtable extension

The tail of `__vtbl__8VkImdApp` is binary-resolved as:

```text
+0x150 putenv__8VkImdAppGPCc
+0x15c umask__8VkImdAppGUl
+0x168 saveEnv__8VkImdAppGPCc
+0x174 loadEnv__8VkImdAppGv
```

These are explicit virtual extension points supplied by `VkImdApp` beyond the inherited `VkApp` contract.

This is important architecturally: environment persistence is a desktop-specific extension, not hidden behavior inside base ViewKit.

## Public putenv wrapper

`VkImdApp::putenv(const char *)` at `0x097afd74` first calls `_doPutenv()`.

When that succeeds it dispatches through the `saveEnv` virtual slot so the modified environment is persisted to `desktopEnv`.

This separates the operation into:

```text
_doPutenv()  -> process environment mutation + undo bookkeeping
saveEnv()    -> persistent desktop-session environment file update
```

## umask wrapper

`VkImdApp::umask(unsigned long)` calls libc `umask`, preserves the old value for its return value, formats the new setting using the recovered format string:

```text
umask %o
```

and then persists that command through the same environment/session save mechanism.

This explains why `desktopenv` represents more than simple `NAME=VALUE` entries.

## Architectural conclusion

`VkImdApp` does not replace or significantly alter ViewKit application/window lifecycle.

Its recovered role is narrowly desktop-specific:

```text
VkApp
  + environment mutation bookkeeping
  + desktopenv persistence
  + live ImdConfigWatcher
  + SGI help enablement
```

This supports keeping Clogin's component/window lifecycle reconstruction in the base ViewKit layer while treating desktop environment/session behavior as a separate Imd extension.

## Remaining work

- Recover exact `envManager` dictionary value type and restoration semantics.
- Resolve `_envVarsChangeCB` callback payload exactly.
- Recover the `saveEnv()` file rewrite/update algorithm source-equivalently.
- Map all inherited `VkApp` vtable records so the precise base/derived virtual boundary is documented.
