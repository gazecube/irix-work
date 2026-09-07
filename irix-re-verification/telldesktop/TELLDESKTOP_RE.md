# telldesktop reverse-engineering notes

Target: `/usr/lib/desktop/telldesktop` from the recovered IRIX `desktop_eoe` tree.

## Binary identity

- ELF 32-bit MSB executable
- MIPS N32 / MIPS-III
- dynamically linked
- stripped
- IRIX 6.5 era target

Dynamic dependencies include `libimdXutil.so` and Xlib.  The executable imports `tellDesktop` and `XOpenDisplay`.

The only user-facing strings recovered from the executable itself are:

```text
usage: %s command
telldesktop (%s):  Can't open display
```

The executable is a thin CLI front-end around the desktop transport.

## Exact wrapper behavior

MIPS disassembly proves the wrapper does:

```text
XOpenDisplay(NULL)
    |
    +-> Display.default_screen
    +-> Display.screens[default_screen].root
    |
    v
tellDesktop(display, root, argv[1])
    |
    v
exit(0)
```

The call boundary is therefore proven to pass three arguments:

```c
tellDesktop(Display *display, Window root, char *command)
```

The original caller ignores the return register.  The exact original return type is therefore not claimed yet.

## Legacy desktop IPC atoms

The matching original `fm` receiver contains and uses:

```text
_SGI_DESKS_DESKTOP_WINDOW
_SGI_DESKS_DESKTOP_COMMAND
_SGI_DESKS_BACKGROUND_WINDOW
_SGI_DESKS_BACKGROUND
```

During desktop initialization, `fm` calls the original `setWindowAsProperty()` with its root window, its desktop child window, and `_SGI_DESKS_DESKTOP_WINDOW`.  This proves that the root-window property advertises the desktop/file-manager recipient window.

The matching later library source-unit evidence contains:

```text
getWinByProp.c++
setWinAsProp.c++
getDtWindow.c++
tellDesktop.c++
```

which matches the observed publish/discover relationship.

## Command receive path

`fm` interns `_SGI_DESKS_DESKTOP_COMMAND` and stores that Atom in its desktop/background-window state.

Its property-change event handler recognizes `PropertyNotify` for that Atom and calls `XGetWindowProperty()` with the following proven parameters:

```text
window       = desktop window
property     = _SGI_DESKS_DESKTOP_COMMAND
long_offset  = 0
long_length  = 4096
delete       = False
req_type     = XA_STRING (31)
```

Before dispatching the returned command it requires:

```text
actual_type  = XA_STRING
actual_format = 8
bytes_after  = 0
```

The returned byte string is then compared against the desktop command language and dispatched inside `fm`.

This proves the legacy protocol shape:

```text
root window
    |
    | _SGI_DESKS_DESKTOP_WINDOW : XA_WINDOW/32
    v
desktop/fm window
    |
    | _SGI_DESKS_DESKTOP_COMMAND : XA_STRING/8
    v
PropertyNotify -> command dispatcher
```

## Separate TellFM protocol

Do not confuse `tellDesktop()` with the newer/internal `TellFM()` RPC path present in the same `fm` binary.

`TellFM(Display *, Widget, int, char **)` constructs a per-user/per-host X selection name exactly as:

```text
fm-<local-hostname>-<uid>
```

It then:

1. interns that selection Atom,
2. obtains its owner with `XGetSelectionOwner()`,
3. uses `XGrabServer()` / `XSetSelectionOwner()` when establishing an owner,
4. interns `_FM_ARGS`,
5. creates a temporary 1x1 window,
6. selects `PropertyChangeMask` on that temporary window,
7. serializes the argument vector with `XStringListToTextProperty()`,
8. stores `_FM_ARGS` on the temporary window,
9. sends a `ClientMessage` to the selection owner whose `data.l[0]` is the temporary window,
10. flushes,
11. waits for `PropertyNotify` on the temporary window,
12. treats `PropertyDelete` for `_FM_ARGS` as completion.

This is a synchronous FM service protocol and is distinct from the legacy `telldesktop` root-property transport.

## Original purpose

The IRIX `telldesktop(1)` manual describes the command as a shell front end for invoking file-manager functions when `fm` is running on the desktop background (`fm -b`).

The documented command language includes, among others:

```text
open
copy
link
reference
remove
print
new directory
permissions
get Info
fetch
fetchHost
openDir ...
restart
quit
close
setenv ENVVAR=value
shape on
shape off
shape pretty
shape fast
```

`close` is documented as a synonym for `quit`.

## Portable implementation policy

The frontend must keep the command string opaque.  Command parsing belongs in the desktop/file-manager receiver.

The Linux transport can now reproduce the receiver-proven legacy protocol:

```text
get desktop Window from root _SGI_DESKS_DESKTOP_WINDOW
XChangeProperty(desktop, _SGI_DESKS_DESKTOP_COMMAND, XA_STRING, 8, PropModeReplace, command, ...)
XFlush(display)
```

The exact original sender-side element count (`strlen(command)` versus `strlen(command) + 1`) is not yet binary-proven because the matching `libdesktopUtil.so` / `libimdXutil.so` implementation binary is not mounted in the current working set.  The portable implementation uses `strlen(command)` because the original receiver uses Xlib property retrieval and validates XA_STRING/8 rather than a stored terminator.

Remaining exact RE target:

- sender-side `getDesktopWindow()` failure/error semantics
- exact sender property element count
- exact return type/value of `tellDesktop()`

These do not block an interoperable Linux implementation of the recovered wire protocol.
