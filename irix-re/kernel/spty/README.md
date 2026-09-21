# IRIX PTC/PTS kernel port

This directory ports the IRIX System V pseudo-terminal driver from the SGI
kernel source in `kern/io/spty.c` (revision 3.62) onto Linux kernel
primitives.

The design goal is deliberately stronger than a userspace `/dev/ptc` to
`/dev/ptmx` pathname shim. IRIX-visible PTC/PTS behavior remains the target,
with Linux acting as the kernel substrate.

## Source-derived invariants

The original driver is a paired PTC/PTS STREAMS driver. It allocates one
`struct pty` per pair, supports at most 1000 pairs, divides historical
device numbers into five groups of 200, and exposes controller-side IRIX
ioctls including `ISPTM`, `UNLKPT`, `SVR4SOPEN`, `TIOCPKT`, termio,
and window-size operations.

The current Linux module establishes:

- a real kernel `/dev/ptc` clone device;
- IRIX-style slave nodes named `/dev/tty[q-z][0-99]`;
- one persistent state object per controller/slave pair;
- the original PTC/PTS state-bit model;
- bidirectional controller/slave byte queues;
- packet-mode control-byte delivery;
- `I_STR` dispatch for the clone-control ioctls;
- IRIX-encoded `TIOCGWINSZ` and `TIOCSWINSZ`;
- poll/wakeup and pair-lifecycle infrastructure.

It intentionally does not route the master through Linux `ptmx`.

This is not yet the complete `spty.c` port. Remaining source-driven work
includes the STREAMS message model, termio/termios operations, automatic
`stty_ld` behavior, flow control, remote mode, packet-mode stop/flush
notifications, and hangup semantics.

## Device-number ABI

IRIX libc `getpty.c` does not identify a controller solely by pathname.
After opening `/dev/ptc`, it inspects `st_rdev` and expects the historical
IRIX PTC major families, each covering 200 pseudo terminals. The Linux
`miscdevice` number assigned to this module is therefore host implementation
detail and must not leak through the IRIX stat/fstat personality.

The compatibility runtime must virtualize `st_rdev` for these descriptors so
IRIX libc observes the expected PTC/PTS major/minor identity and can derive the
matching `/dev/tty[q-z][0-99]` slave name.

## Build

From `irix-re/kernel`:

    make

Then, as root:

    insmod irix_spty.ko
    ls -l /dev/ptc /dev/ttyq0

The next porting passes should continue directly from the SGI `spty.c`,
`getpty.c`, and associated STREAMS/termio headers rather than substituting
Linux PTY semantics where the IRIX behavior is known.
