#ifndef ARCS_UEFI_COMPAT_SYS_TYPES_H
#define ARCS_UEFI_COMPAT_SYS_TYPES_H

#include <Uefi.h>
#include <stddef.h>
#include <sgidefs.h>

/*
 * X64 compatibility subset of the original IRIX <sys/types.h> environment
 * used by standalone ARCS sources.  Keep the original SGI names and widths,
 * but avoid importing the full kernel header graph into the UEFI build.
 *
 * These definitions are taken from IRIX 6.5.7m sys/types.h and
 * sys/bsd_types.h.  Where the original definition was ABI-conditional, use
 * the width matching a 64-bit machine/standalone environment rather than the
 * host libc definition.
 */

/* POSIX extension names from IRIX sys/types.h. */
typedef unsigned char   uchar_t;
typedef unsigned short  ushort_t;
typedef unsigned int    uint_t;
typedef unsigned long   ulong_t;

/* BSD compatibility names used throughout libsk. */
typedef unsigned char   unchar;
typedef unsigned char   u_char;
typedef unsigned short  ushort;
typedef unsigned short  u_short;
typedef unsigned int    uint;
typedef unsigned int    u_int;
typedef unsigned long   ulong;
typedef unsigned long   u_long;

/* Core address and standalone device/addressing types. */
typedef char            *addr_t;
typedef char            *caddr_t;
typedef long             daddr_t;
typedef __uint32_t       dev_t;
typedef unsigned long    paddr_t;
typedef unsigned long    iopaddr_t;
typedef __uint32_t       pfn_t;
typedef __uint32_t       inst_t;

/* SGI machine/NUMA identifiers. */
typedef signed short     nasid_t;
typedef signed short     cnodeid_t;
typedef signed char      partid_t;
typedef signed short     moduleid_t;
typedef signed short     cmoduleid_t;

/* Standalone synchronization/runtime identifiers. */
typedef unsigned int     lock_t;
typedef signed short     cpuid_t;
typedef int              processorid_t;
typedef int              toid_t;

/*
 * Machine register types.
 *
 * IRIX makes machreg_t 64-bit for MIPS3/MIPS4, and the existing standalone
 * k_machreg_t contract is likewise 64-bit.  X64 is also a 64-bit machine
 * register environment, so preserve that semantic width here.
 */
typedef __uint64_t       machreg_t;
typedef __uint64_t       k_machreg_t;
typedef __int64_t        k_smachreg_t;

#endif
