# FM external command dispatcher reverse-engineering

Target: original IRIX `fm` function:

```text
propertyChangeEH__13ImdBackWindowSGP10_WidgetRecPvP7_XEventPc
```

This event handler consumes `_SGI_DESKS_DESKTOP_COMMAND` on the published desktop/background window. The property is read as `XA_STRING` / format 8, with a maximum request length of 4096 bytes.

## Exact command literals

The command table recovered from the original small-data/rodata sections includes:

```text
open
copy
link
reference
remove
new directory
print
startShare
stopShare
publish
permissions
get Info
fetch
restart
setenv
fetchHost
quit
close
opendir
select all
empty dumpster
dynamicMenuCmd
shape pretty
shape fast
shape on
shape off
```

`link` and `reference` both dispatch to the same reference action. `close` and `quit` share the same shutdown path.

## Direct action mapping

Binary-proven call targets:

```text
open           -> FmMenuActions::open()
copy           -> FmMenuActions::copy()
link           -> FmMenuActions::reference()
reference      -> FmMenuActions::reference()
remove         -> FmMenuActions::remove()
new directory  -> FmMenuActions::newDir()
print          -> FmMenuActions::print()
startShare     -> FmMenuActions::share()
stopShare      -> FmMenuActions::unshare()
publish        -> FmMenuActions::publish()
get Info       -> FmMenuActions::getInfo()
fetchHost      -> FmMenuActions::fetchHost()
select all     -> FmMenuActions::selectAll()
```

Additional direct application/window behavior:

```text
fetch          -> ImdBackWindow::showQuickFindDialog()
empty dumpster -> ImdBackWindow::emptyDumpster()
```

`permissions` enters the permissions-task launcher path rather than a simple menu callback.

## Dedicated command handlers

### restart

The command follows a dedicated restart path. It closes/tears down the current desktop state and launches:

```text
/usr/sbin/fm -b
```

The exact surrounding virtual calls are still being named, but the executable path and `-b` argument are binary-proven.

### setenv

`setenv` is recognized as a command prefix rather than only an exact token. The handler parses trailing text with the recovered format:

```text
%*s %s
```

and forwards the resulting environment assignment into the application state/environment path.

### quit / close

Both strings enter the same shutdown path. This is the implementation behind documented `telldesktop quit` / `telldesktop close` behavior.

### opendir

`opendir` is a prefix command. The handler parses trailing arguments, supports quoted text, constructs an FM/dirview launch command, and then launches it. The command-building details are still being reconstructed exactly.

### dynamicMenuCmd

This is an external dynamic-menu dispatch path. It parses additional command fields and forwards them to the dynamic menu handler.

### shape commands

The following commands manipulate the desktop icon/background layout shaping behavior:

```text
shape pretty
shape fast
shape on
shape off
```

These route into the background layout / icon-panel shape controls rather than ordinary file operations.

## Protocol relationship

Legacy external control is therefore:

```text
client
  -> root::_SGI_DESKS_DESKTOP_WINDOW
  -> desktop::_SGI_DESKS_DESKTOP_COMMAND
  -> PropertyNotify
  -> ImdBackWindow::propertyChangeEH()
  -> command dispatch
```

This is independent of the newer `_FM_ARGS` / `TellFM()` selection protocol.

## Reconstruction policy

The Linux FM port should keep transport and command dispatch separate:

```text
legacy X11 receiver
        |
        v
fmDispatchExternalCommand(command)
        |
        +-> file/menu actions
        +-> desktop/window actions
        +-> application/session actions
```

This preserves compatibility with original `telldesktop` callers while allowing the reconstructed Linux implementation to replace IRIX-specific internals one subsystem at a time.
