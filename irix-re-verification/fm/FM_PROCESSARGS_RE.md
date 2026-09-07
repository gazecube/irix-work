# FmApp::processArgs reverse-engineering notes

Target binary: original IRIX `/usr/sbin/fm` from `desktop_eoe`.

Recovered symbol:

```text
processArgs__5FmAppGiPPc
0x10022b18
size 3160 bytes
```

Recovered boundary:

```c
FmApp::processArgs(int argc, char **argv)
```

## getopt grammar

The exact getopt option string is:

```text
bADo:pv:h:
```

The jump table maps the options to these branches exactly:

```text
-b -> 0x10022c20
-A -> 0x10022c28
-D -> 0x10022c34
-o -> 0x10022c40
-p -> 0x10022c50
-h -> 0x10022c58
-v -> 0x10022c64
```

The shipped usage string is:

```text
usage: fm [-bADp] -o <oldDir> -v <viewOpts> <dir>
```

## Background versus normal window startup

`-b` sets the background-mode flag. The later startup branch checks the global `theBgWin` singleton.

If no background window exists, FM allocates an `ImdBackWindow` and constructs it as:

```c
new ImdBackWindow("backWindow", app->baseWidget());
```

The constructor symbol is:

```text
__ct__13ImdBackWindowGPCcP10_WidgetRec
```

and the literal widget/component name is exactly:

```text
backWindow
```

Normal directory startup instead allocates `FmWindow` through:

```text
__ct__8FmWindowGPCcT1P5FmAppbP3ArgUi
```

with the first constructor string exactly:

```text
FileManager
```

The observed call shape is consistent with:

```c
FmWindow("FileManager",
         directory,
         app,
         alternateMode,
         args,
         argCount);
```

Before construction, `FmWindow::getWindowParameters()` builds the Xt/resource argument list.

## View option grammar

`-v` is parsed with `getsubopt()` using this exact token array:

```text
view
gallery
shelf
content
contentheight
windowWxH
```

### view

The accepted values are compared case-insensitively as:

```text
icon
list
column
```

and map to numeric values 0, 1 and 2 respectively.

The corresponding window state key is:

```text
LayoutStyle
```

### gallery

The value is compared against:

```text
on
```

The corresponding state key is:

```text
ShowAsGallery
```

### shelf

The value is compared against `on`.

The corresponding state key is:

```text
ShowShelf
```

### content

The value is compared against `on`.

The corresponding state key is:

```text
ShowContent
```

### contentheight

The value is parsed with:

```c
sscanf(value, "%d", &height);
```

The corresponding state key is:

```text
ContentHeight
```

### windowWxH

The value is parsed with:

```c
sscanf(value, "%dx%d", &width, &height);
```

These dimensions are folded into the resource/window-parameter setup before the new `FmWindow` is shown.

## Directory and target normalization

The post-option path handling uses the IID model layer rather than raw string-only path handling. Binary-proven calls include:

```text
basename()
dirname()
ImdGlobalEnv::protoDNAFromString()
ImdBroker::contextBroker()
ImdBroker::normalizeDNA()
ImdGlobalEnv::realPath()
ImdFileModel::dnaFromFullPath()
ImdFileModel::isFileSystemBasedClass()
ImdFileModel::fileNameFromDNA()
ImdDNA::selector()
```

This means startup arguments are normalized into `ImdDNA` identities before FM searches existing windows or creates a new one.

## Existing-window reuse

Before constructing a new normal `FmWindow`, `processArgs()` iterates `VkApp::windowList()`, dynamically casts entries to `FmWindow`, obtains each window's model identity and compares it with the normalized target `ImdDNA`.

When the target matches an existing window, FM brings/activates that window through virtual window methods instead of blindly creating a duplicate.

This is a key architectural behavior to preserve in the Linux implementation.

## Internal state vocabulary

The binary also contains the following internal control/state tokens used by FM startup/session/RPC paths:

```text
FmState
Tunnel
Umask
Audio
SetEnv
Sync
NoSync
```

These should not be treated as ordinary public getopt switches. They are part of FM's internal state/control language and should be reconstructed separately from the user-facing CLI.

## Architectural consequence

`FmApp::processArgs()` is the authoritative ingress for both normal command-line startup and newer `_FM_ARGS` TellFM requests.

The reconstruction should therefore keep this shape:

```text
argv / _FM_ARGS
    |
    v
processArgs-compatible normalization
    |
    +-> existing FmWindow reuse
    |
    +-> new FmWindow("FileManager", ...)
    |
    `-> -b -> singleton ImdBackWindow("backWindow", ...)
```

This provides the central startup boundary for the full FM reimplementation.
