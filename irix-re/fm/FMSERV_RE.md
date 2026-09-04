# fmserv reverse-engineering notes

Target:

```text
/usr/lib/desktop/fmserv
```

Binary identity:

```text
ELF 32-bit MSB
MIPS N32 / MIPS-III
IRIX 6.5
build date 1998-04-29
```

The binary is stripped but retains SGI debug sections and a rich dynamic symbol table.

## Role

`fmserv` is not the FM graphical file-manager process and is not a second copy of `fm`.

It implements the `FmService` class and provides shared desktop/file-manager services, including:

```text
workgroup coordination
FAM monitoring
client socket service
restart notification
shared workgroup data
```

Important exported methods include:

```text
FmService::run(char const *)
FmService::processFamEvent(FAMEvent)
FmService::setWorkGroupData(...)
FmService::removeWorkGroupData(...)
FmService::getWorkGroupData(...)
FmService::offerToRestartFm()
FmService::_serviceConnect()
FmService::_terminate()
```

## main()

The original `main()` is binary-proven to construct an `FmService` and call:

```c
service.run(argc >= 2 ? argv[1] : ":0.0");
```

Therefore the additional argument passed by the `TellFM()` no-owner fallback is the X display string.

The default display string embedded in `fmserv` is exactly:

```text
:0.0
```

## Service socket

`FmService::run()` constructs a per-user/per-host Unix-domain service socket using the recovered format:

```text
/tmp/.imd_socket-%d-%s
```

where the components are the current uid and hostname.

The service uses ordinary Unix socket operations including:

```text
socket
bind
chmod
listen
select
accept
read
write
```

## FAM integration

The service monitors:

```text
/usr/lib/filetype/desktop.otr
```

through FAM. Recovered diagnostics explicitly identify created/changed events for `desktop.otr`.

This connects FM service lifetime to desktop file-type database changes.

## Restart integration

The binary embeds and executes:

```text
/usr/lib/desktop/askRestartFm
```

through `FmService::offerToRestartFm()` when relevant desktop/filetype state changes require an FM restart.

## Relationship to TellFM

The reconstructed `TellFM()` no-owner path is therefore:

```text
client sees no fm-<host>-<uid> selection owner
        |
        v
XGrabServer()
        |
        +-> recheck selection owner
        +-> temporarily claim selection with caller widget
        |
        v
XUngrabServer()
        |
        v
fork()
        |
        `-> child execl("/usr/lib/desktop/fmserv",
                        "fmserv",
                        DisplayString(display),
                        NULL)
```

The exact selection/race behavior is documented in `TELLFM_RE.md`.

## Architectural consequence

The reconstructed desktop should preserve three distinct components rather than collapsing them:

```text
fm
  graphical file manager / desktop background

fmserv
  shared desktop/FAM/workgroup service

telldesktop / TellFM
  client IPC entry points
```

This separation matches the original SGI architecture and avoids turning `fmserv` into an incorrectly modeled FM launcher.
