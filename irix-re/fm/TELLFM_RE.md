# TellFM reverse-engineering notes

Target: original IRIX `fm` exported symbol:

```text
TellFM__GP9_XDisplayP10_WidgetReciPPc
```

Recovered C++ boundary:

```c
TellFM(Display *display, WidgetRec *widget, int argc, char **argv)
```

The original function includes UI/fmserv fallback and argument preparation.  The X11 transport itself is separable and is implemented in `TellFM.c` as `sgiTellFMTransport()`.

## Service selection

The fm service selection name is constructed per host and uid:

```text
fm-<hostname>-<uid>
```

The client interns that selection and obtains its owner with `XGetSelectionOwner()`.

## Request window

The original creates a temporary 1x1 request window:

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

The argument vector is serialized with:

```c
XStringListToTextProperty(argv, argc, &property)
```

and stored on the request window under:

```text
_FM_ARGS
```

using `XSetTextProperty()`.

The original function also performs argument preparation before this step, including current-directory and relative-path handling.  That policy is intentionally kept separate from the transport reconstruction.

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

The receiver therefore acknowledges request completion by deleting `_FM_ARGS` from the caller-owned request window.

On the original success path, `TellFM()` calls `exit(0)` after receiving this acknowledgement.  The portable transport returns success instead so it can be reused as a library routine.

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
