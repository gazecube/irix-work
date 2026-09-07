# FmApp::processArgs reverse-engineering

Target:

```text
processArgs__5FmAppGiPPc
```

Address/size in the recovered IRIX `fm` binary:

```text
0x10022b18
3160 bytes
```

This function is a central ingress point used by normal FM command-line startup and by the newer `_FM_ARGS` ClientMessage receiver.

## Public usage

The binary contains the exact usage string:

```text
usage: fm [-bADp] -o <oldDir> -v <viewOpts> <dir>
```

## getopt grammar

The actual binary-proven getopt string is:

```text
bADo:pv:h:
```

Recognized options and exact dispatch branches:

```text
-A -> 0x10022c28
-D -> 0x10022c34
-b -> 0x10022c20
-h -> 0x10022c58
-o -> 0x10022c40
-p -> 0x10022c50
-v -> 0x10022c64
```

Argument-taking options are therefore:

```text
-o <arg>
-v <arg>
-h <arg>
```

The public usage text does not advertise every internal option accepted by the parser.

## Known option-state behavior

Binary-proven state transitions so far:

```text
-b -> sets the background/desktop-mode boolean
-o -> stores optarg as the old-directory / previous-directory value
-p -> sets a presentation-related boolean
-v -> enters view-option parsing
```

`-A`, `-D`, and `-h` have distinct internal state effects that are still being named from their consumers rather than guessed from the option letters.

## View option parsing

The `-v` branch parses an option value into a small enumerated state. Recovered literals associated with this area include:

```text
view
gallery
shelf
content
layouts
icon
list
column
```

The exact mapping of all `-v` forms to FM window state is being recovered from the later consumers in `processArgs()`.

## Internal state/RPC keywords

The same application path contains internal state/control keywords:

```text
FmState
Tunnel
Umask
Audio
SetEnv
Sync
NoSync
```

These are not assumed to be ordinary public command-line options. They participate in FM state restoration, tunneling, environment/configuration, and service communication paths.

Additional recovered literals include:

```text
-D
-p
True
FmApp
imdDirs
IMD_URL
```

## Relationship to TellFM

The newer protocol receiver performs:

```text
ClientMessage(_FM_ARGS)
    -> XGetTextProperty(requestWindow, _FM_ARGS)
    -> XTextPropertyToStringList()
    -> FmApp::processArgs(argc, argv)
    -> XDeleteProperty(requestWindow, _FM_ARGS)
```

Therefore `processArgs()` is the authoritative high-level action grammar for both newly-started FM instances and requests forwarded from another process.

## Reconstruction strategy

Do not replace this with a generic modern option parser until the original state consumers are recovered. Preserve:

1. public command-line grammar,
2. internal RPC/state tokens,
3. directory/view-state semantics,
4. existing-instance forwarding behavior,
5. desktop/background mode distinction.
