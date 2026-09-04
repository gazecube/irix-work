# telldesktop reverse-engineering notes

Target: `/usr/lib/desktop/telldesktop` from the recovered IRIX `desktop_eoe` tree.

## Binary identity

- ELF 32-bit MSB executable
- MIPS N32 / MIPS-III
- dynamically linked
- stripped, but retains debug sections
- build string: IRIX 6.5 build dated 1998-04-29

Dynamic dependencies include:

- `libimdXutil.so`
- `libX11.so.1`
- `libCsup.so`
- `libC.so.2`
- `libc.so.1`

The executable imports `tellDesktop` from `libimdXutil.so` and `XOpenDisplay` from Xlib.

The only user-facing strings recovered from the executable itself are:

```text
usage: %s command
telldesktop (%s):  Can't open display
```

This strongly indicates that the executable is a thin CLI front-end around the real `tellDesktop()` transport in `libimdXutil.so`.

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

The desktop integration guide also documents:

```text
/usr/lib/desktop/telldesktop quit
```

as the supported way to stop the desktop/file-manager background process before restarting it.

## Architectural consequence

Do not model original IRIX `telldesktop` as a session-startup script.

The recovered architecture is instead:

```text
telldesktop CLI
    |
    v
tellDesktop(Display *, command)
    |
    v
X11 desktop/file-manager IPC
    |
    v
fm -b / desktopManager
```

The MaXX shell utility currently named `telldesktop` should therefore be treated as a compatibility implementation, not as evidence for the original IRIX architecture.

## Matching later library evidence

The recovered `libimdXutil.so` contains source-unit evidence for:

```text
getDtWindow.c++
tellDesktop.c++
tellToolchest.c++
toolchestConf.c++
dynaMenuWindow.c++
```

This makes `libimdXutil.so::tellDesktop()` the primary transport RE target.

## Current confidence

High confidence:

- original binary is a thin wrapper
- one command string is passed through to a `tellDesktop` API
- communication is display/X11 scoped
- target is the desktop-background file manager / desktopManager
- `quit`/`close` stop that desktop background component

Still to recover exactly:

- `tellDesktop()` signature beyond the observed `Display *` plus command argument
- desktop target-window discovery details
- X atom/property names
- whether transport is ClientMessage, property mutation, Selection protocol, or a combination
- exact return/error semantics

## Portable implementation policy

Preserve the original public CLI contract while isolating the transport behind one function:

```c
int tellDesktop(Display *display, const char *command);
```

The first Linux port should not reinterpret individual commands in the CLI. The command string should remain opaque at the front-end boundary and be delivered to the reconstructed desktop IPC layer. This keeps compatibility with original callers and leaves command parsing in the desktop/file-manager side, where IRIX placed it.
