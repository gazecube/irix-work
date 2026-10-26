# irix-compat

Native x86-64 Linux builds of IRIX userland commands. The IRIX sources
(`~/Projects/irix-657m-src`) are compiled unmodified, with glibc headers plus
this layer. Build with `make`; binaries land in `build/`.

Built so far: chkconfig, setmnt, devnm (new, IRIX ships no source), uadmin,
suattr, nvram, getty, init, fsstat, ioconfig, mount.

- `include/irix_compat.h` is force-included. It supplies IRIX-only constants
  and redirects `sysconf`, `statvfs64`, `sys_errlist`.
- `include/` has the IRIX headers the sources expect (`sys/capability.h`,
  `capability.h`, `pfmt.h`, `sys/uadmin.h`, `sys/syssgi.h`, `sat.h`,
  `sys/mac*.h`, `clearance.h`, `priv.h`, `sys/systeminfo.h`).
- `src/irix_compat.c` is `libirixcompat.a`.

Behaviour worth knowing:

- Capabilities: IRIX's 64-bit masks map onto Linux capability numbers; the
  extra IRIX bits (MAC, INF, STREAMS, SVIPC, LINK_DIR, DAC_EXECUTE) are the
  extension bits of kernel patch 0003. On a stock kernel those bits are
  ignored. `suattr` sets real capability sets (capset plus ambient) but keeps
  Linux's root semantics, i.e. IRIX's "capabilities plus superuser" mode
  (`sysconf(_SC_CAP)` returns 1).
- `/etc/capability` (`user:default:allowed`) is read for the clearance check;
  `IRIX_CAPABILITY_FILE` overrides the path.
- `uadmin` maps to reboot(2) after sync (AD_HALT halt, AD_BOOT restart,
  AD_POWEROFF power off).
- `syssgi(SGI_GETNVRAM/SGI_SETNVRAM)` is backed by a `name=value` file,
  `/var/lib/irix/nvram` (`IRIX_NVRAM_FILE` overrides), until it is backed by
  the ARCS environment.
- `statvfs64` uses the kernel's `irix_statvfs64` syscall (patch 0009) and
  falls back to a magic-number table on a stock kernel.
- MAC, audit (SAT) and clearance are absent, as on a non-trusted IRIX.
- `termio`: IRIX's struct termio (with c_ospeed) and `ioctl(TCGETA/TCSETA*)`
  are translated to Linux termios by `irix_ioctl`; LDSETT/LDGETT are no-ops.
- Standard streams: SVR4 `fdopen(fd)` after `fclose(stdout)` re-attached the
  standard stream; the compat `fdopen` rebinds stdin/stdout/stderr (getty).
- `syslog()` also appends to `$IRIX_SYSLOG_FILE` when set (debugging aid).
- `getdtablehi()` returns the highest open descriptor + 1 (from /proc/self/fd).

Testing: `make CFLAGS="... -static ..."` builds static binaries for a bare
initramfs; the QEMU guest used for that lives outside the repo.

`init` (built as `build/init`; install it only under another name, e.g.
`irix-init`, and boot it with `init=` / `rdinit=`; it does not replace OpenRC):

- Two transforms are applied to a copy of init.c in `build/` by the Makefile:
  SVR4 FILE internals (`fp->_flag` -> `irix_fflag(fp)`) and the console
  device numbers (IRIX duart 58,0 -> Linux /dev/console 5,1).
- `sgikopt()` reads `name=value` from `/proc/cmdline`, so `initstate=s` on the
  kernel command line boots single user, like a PROM `setenv initstate s`.
- STREAMS `I_SETSIG` on `/etc/initpipe` becomes O_ASYNC + F_SETOWN.
- `sysmp(MP_KERNADDR, ...)` fails, so init keeps its default process table size.
- Verified in QEMU as PID 1: sysinit entries, run level 2 wait entry, getty
  respawn, `initstate=s` single-user mode.

Commands written for this port (IRIX's own are EFS/NFS/hwgraph specific):

- `fsstat`: IRIX exit codes (0 okay, 1 needs checking, 2 mounted, 3 error);
  XFS counts as okay (journal replay), ext2/3/4 report their state flag, a
  mounted root is never dirty.
- `ioconfig`: makes sure /hw is mounted (hwgfs) and that /dev/dsk aliases
  exist; the kernel (patch 0001) and udev create them, so there is no ioctl
  alias dance.
- `mount`: translates IRIX options (`-T types`, `-c`, `-t fd`, IRIX-only fs
  types) and runs the system mount ($IRIX_REAL_MOUNT overrides it).
