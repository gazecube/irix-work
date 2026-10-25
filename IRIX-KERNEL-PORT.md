# IRIX kernel features on Linux: inventory and plan

Purpose: the recompiled IRIX runtime (irix-re/recomp/runtime) currently fakes
a number of IRIX kernel services in userland. On Gentoo the kernel is ours,
so each of them can become a real kernel feature, shipped as one patch series
(kept in /etc/portage/patches/sys-kernel/gentoo-sources/ so updates reapply it).

Status key: DONE = in the series and tested, WIP = written, not yet in series,
FAKE = emulated in userland today, GAP = missing or stubbed.

## Patch series

| # | Feature                          | Status | Notes |
|---|----------------------------------|--------|-------|
| 0001 | dsk/dks<c>d<t>[l<l>]s<n> disk names | DONE (VM-tested) | sd.c, partitions/core.c, lib/vsprintf.c. Needs udev rule 59-irix-persistent.rules (stock rules only know sd*, nvme*, vd*) |
| 0002 | irix_spty: /dev/ptc + /dev/tty[q-z]NN | integrating | irix-re/kernel/spty. Source port of SGI kern/io/spty.c; STREAMS message model, termio and flow control still to do |
| 0003 | hwgfs: /hw graph as source of truth | planned | layout in kpatch/HW-LAYOUT.md; replaces udev naming; ioconfig-style daemon fills /dev |
| 0004 | share groups (sproc, prctl share masks) | FAKE | see below |
| 0005 | usema / blockproc / unblockproc | FAKE | see below |
| 0006 | raw block devices (/dev/rdsk) | GAP | ported from IRIX; Linux has none |
| 0007 | SGI system calls (syssgi, sysmp, schedctl, prctl) | FAKE | see below |

## What the runtime fakes today

Where: irix-re/recomp/runtime/*.c. Counts are keyword hits, a rough guide only.

### 1. Process model (highest value)

- sproc(): b_sproc() in IrixCLibMoreBridge.c, guest syscall 1129 in IrixGuest.c.
  Implemented with fork(). A fork child does not share memory created after the
  fork, so share groups are only partly emulated (documented in project notes:
  "memory created after the fork is not shared"). This is the cause of the
  soundscheme transport problems.
  Kernel replacement: real share groups (PR_SADDR, PR_SFDS, PR_SDIR, PR_SUMASK,
  PR_SULIMIT, PR_SID) built on CLONE_VM / CLONE_FILES / CLONE_FS plus a
  group object; new-mapping propagation for PR_SADDR.
- prctl(): irix_compat_prctl in IrixCompat.c. Handles PR_MAXPROCS, PR_MAXPPROCS,
  PR_GETSTACKSIZE, PR_GETSHMASK, PR_TERMCHILD, PR_GETNSHARE, PR_LASTSHEXIT;
  everything else is unhandled.
- blockproc/unblockproc: process-shared semaphore table in IrixCLibMoreBridge.c
  (block_table_init, b_blockproc, b_unblockproc). These are the primitive under
  uspsema/usvsema. Kernel replacement: per-process block counters with
  futex-style wakeups, keyed by pid.
- usinit/usnewsema/uspsema/usvsema: shared-arena semaphores (usema). Faked on
  top of the above plus a shared mmap arena.

### 2. SGI-specific system calls

- syssgi: irix_compat_syssgi in IrixCompat.c. Implemented: SGI_SETPGID,
  SGI_SYSCONF (sub-names 1-5 only; ACL/AUDIT/INF/MAC/CAP report absent),
  SGI_SETGROUPS/GETGROUPS, SGI_SSYNC, SGI_GETPGID, SGI_GETSID, SGI_FDHI,
  SGI_ELFMAP, SGI_RXEV_GET, SGI_INVENT (hardware inventory, sub 1 and 2),
  SGI_CONST (pagesize, param, scache), SGI_XLV_ATTR (empty), SGI_TOSSTSAVE,
  SGI_NOHANG, SGI_USE_FP_BCOPY. Unhandled sub-commands print
  "UNIMPLEMENTED IRIX SGI_..." and fail with EINVAL.
- sysmp: MP_NPROCS, MP_NAPROCS, MP_SCHED, MP_KERNADDR, MP_PGSIZE, MP_NUMNODES,
  MP_STAT, MP_SASZ, MP_SAGET, MP_SAGET1. The sar-style counters
  (MP_SAGET) are synthesized from /proc in IrixKernelInfo.c.
- sginap, schedctl: thin wrappers around nanosleep / sched_*.

### 3. Hardware inventory and the device graph

- SGI_INVENT feeds getinvent()/hinv. Today it is a hand-built table. With
  hwgfs (0003) it would be generated from the graph, which is also what IRIX
  does (INFO_LBL_INVENT, _detail_invent).
- /dev/dsk, /dev/rdsk, /dev/scsi: today created by a separate Python daemon
  (irix-devices.service, for mediad) from /dev/sdX. With 0001 the kernel names
  the disks itself and this daemon is partly obsolete; /dev/scsi/* (generic
  SCSI) still needs a source.
- /dev/kmem and /dev/mem: synthetic file in IrixGuest.c / IrixCompat.c so
  programs that peek at kernel structures read fabricated data. A real
  /dev/kmem is not planned; a read-only synthetic view served by hwgfs or
  a small module is the realistic target.

### 4. Terminals and STREAMS

- _getpty: bridge__getpty in IrixLibcBridge.c already prefers /dev/ptc and
  falls back to Unix98 ptys. 0002 removes the fallback in practice.
- STREAMS I_STR ioctls: handled in IrixGuest.c (strioctl). Only the pty clone
  control set exists; no general STREAMS (I_PUSH, getmsg/putmsg) support.
- TCGETA / termio: mapped to termios in the runtime.

### 5. Filesystem personality

- stat/xstat, statvfs, getdents, lseek64, fstat64: ABI translation in the
  runtime (struct layouts, big-endian guest). A kernel cannot help with these;
  they stay in the runtime.
- XFS: IRIX XFS differs from Linux XFS (see the v4 note). The kernel mounts
  IRIX-era v4 filesystems with CONFIG_XFS_SUPPORT_V4.
- EFS: not supported in mainline for write; a read-only or ported driver is
  a candidate if EFS volumes need mounting.
- NFS/mediad: mediad-linux handles removable media.

### 6. Not kernel work (stays in the runtime)

Endianness, N32 calling convention, rld/dlopen, signal frame layout, libc
bridging, X11/GL bridging, audio (libaudio to PulseAudio).

## Suggested order

1. 0002 finish: boot-test /dev/ptc and /dev/ttyq0, drop the Unix98 fallback.
2. 0005 then 0004: usema and share groups. They unblock soundscheme and make
   every multi-threaded IRIX program behave like it does on IRIX.
3. 0003 hwgfs: biggest piece; start with the vertex/edge/label API and
   SCSI disks, then SGI_INVENT from the graph, then PCI/net/input.
4. 0006 raw devices, 0007 remaining syssgi/sysmp commands as programs need them.

## Open questions

- Share groups: how faithful? IRIX share groups differ from Linux threads in
  that members are separate processes with shared address space regions.
- Version policy: carry the series against gentoo-sources 6.18.x only, or
  track newer LTS releases as they appear.
- Where to hold the sources: a git repo of the patch series (quilt-style) next
  to irix-re/kernel, so the same files serve /etc/portage/patches.
