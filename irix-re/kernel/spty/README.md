# IRIX PTC/PTS kernel port

This directory ports the IRIX System V pseudo-terminal driver from the SGI
kernel source in `kern/io/spty.c` (revision 3.62) onto Linux kernel
primitives.

The design goal is deliberately stronger than a userspace `/dev/ptc` to
`/dev/ptmx` pathname shim. The IRIX PTC/PTS state machine remains the
authority, with Linux acting as the kernel substrate.

## Source-derived invariants

The original driver is a paired PTC/PTS STREAMS driver. It allocates one
`struct pty` per pair, supports at most 1000 pairs, divides historical
device numbers into five groups of 200, and exposes controller-side IRIX
ioctls including `ISPTM`, `UNLKPT`, `SVR4SOPEN`, `TIOCPKT`, termio,
and window-size operations.

The initial Linux module establishes:

- a real kernel `/dev/ptc` clone device;
- one persistent IRIX-style state object per controller open;
- the original PTC/PTS state-bit model;
- `ISPTM`, `UNLKPT`, `SVR4SOPEN`, `TIOCPKT`, and winsize state;
- lifecycle and locking infrastructure for the slave/data-path port.

It intentionally does not route the master through Linux `ptmx`.

## Build

From `irix-re/kernel`:

    make

Then, as root:

    insmod irix_spty.ko
    ls -l /dev/ptc

The next porting pass adds the slave character devices and the bidirectional
data path, followed by the STREAMS message/termio behavior from `spty.c`.
