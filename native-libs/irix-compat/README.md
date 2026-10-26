# irix-compat

Native x86-64 Linux builds of IRIX userland commands. The IRIX sources
(`~/Projects/irix-657m-src`) are compiled unmodified, with glibc headers plus
this layer. Build with `make`; binaries land in `build/`.

Built so far: chkconfig, setmnt, devnm (new, IRIX ships no source), uadmin,
suattr, nvram.

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
