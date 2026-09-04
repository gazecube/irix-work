# TellFM reverse-engineering notes

Target: original IRIX `fm` exported symbol:

```text
TellFM__GP9_XDisplayP10_WidgetReciPPc
```

Recovered C++ boundary:

```c
TellFM(Display *display, WidgetRec *widget, int argc, char **argv)
```

The original function combines argument preparation, fm service discovery,
fmserv startup fallback, and the synchronous X11 request transport.  The Linux
reconstruction keeps those concerns separable.

## Service selection

The fm service selection name is constructed as:

```text
fm-<local-hostname>-<uid>
```

The original builds this using `getLocalHostName()` and `getuid()`, then calls:

```c
XGetSelectionOwner(display, selectionAtom)
```

## Argument preparation

The original copies the argument vector into temporary storage with room for
one additional argument and a terminating NULL.

Only the trailing target argument is normalized.

Binary-proven behavior:

```text
if argc == 1 OR argv[argc-1][0] == '-':
    append getcwd(NULL, 1024)

else if argv[argc-1][0] != '/':
    construct ImdString from trailing argument
    if !ImdGlobalEnv::isURL(trailing):
        allocate 1024-byte buffer
        if realpath(trailing, buffer) succeeds:
            replace trailing argument with buffer
        else:
            retain original argument
```

Absolute targets are left unchanged.  URL-like targets are deliberately not
canonicalized through `realpath()`.

The portable reconstruction implements the same trailing-target policy.  Its
URL predicate is intentionally isolated as a compatibility stand-in until the
exact `ImdGlobalEnv::isURL()` implementation is ported.

## No-owner race and fmserv fallback

If the first `XGetSelectionOwner()` returns `None`, original `TellFM()` performs
an X-server-serialized recheck:

```text
XGrabServer(display)
owner = XGetSelectionOwner(display, selectionAtom)

if owner == None:
    XSetSelectionOwner(display,
                       selectionAtom,
                       XtWindow(widget),
                       CurrentTime)

XUngrabServer(display)
```

This prevents multiple clients from racing to start the fallback service.

The original then enters its fmserv startup path when no pre-existing owner was
found.  The binary proves that it forks and the child calls:

```text
/usr/lib/desktop/fmserv
```

with argv[0]:

```text
fmserv
```

One additional fmserv argument is still being resolved from the Widget/Vk
state.  That field should not be guessed in the portable implementation.

## Request window

Once an fm service owner exists, the original creates a temporary request
window:

```c
XCreateSimpleWindow(display,
                    root,
                    -1, -1,
                    1, 1,
                    0,
                    0,
                    0);
```

It selects:

```text
PropertyChangeMask
```

on that window.

## Request payload

The prepared argument vector is serialized with:

```c
XStringListToTextProperty(argv, argc, &property)
```

and stored on the temporary request window under:

```text
_FM_ARGS
```

using `XSetTextProperty()`.

## ClientMessage

A `ClientMessage` is sent to the fm selection owner with:

```text
type         = ClientMessage
window       = selection owner
message_type = _FM_ARGS
format       = 32
data.l[0]    = temporary request window
```

using `XSendEvent()` followed by `XFlush()`.

## Synchronous completion acknowledgement

The caller blocks in `XNextEvent()` until it observes:

```text
type   == PropertyNotify
window == temporary request window
atom   == _FM_ARGS
state  == PropertyDelete
```

The receiver therefore acknowledges request completion by deleting `_FM_ARGS`
from the caller-owned request window.

On the original success path, `TellFM()` calls `exit(0)` after receiving this
acknowledgement.  The portable transport returns success instead so it can be
reused as a library routine.

## Relationship to telldesktop

This is a separate protocol from the older `tellDesktop()` path:

```text
legacy telldesktop:
  root::_SGI_DESKS_DESKTOP_WINDOW
      -> desktop::_SGI_DESKS_DESKTOP_COMMAND

newer TellFM:
  selection fm-<hostname>-<uid>
      -> temporary window::_FM_ARGS
      -> ClientMessage
      -> PropertyDelete acknowledgement
```

Both should be preserved for compatibility.
