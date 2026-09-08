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
 * The corresponding IRIX 6.5.7m definitions are:
 *   typedef unsigned int  uint_t;
 *   typedef unsigned long ulong_t;
 *   typedef unsigned int  lock_t;
 *   typedef int           toid_t;
 *   typedef __uint64_t    k_machreg_t;
 *
 * On x86-64, ULONG is intentionally not used for ulong_t: this is the host C
 * 'unsigned long' compatibility type expected by generic SGI source, whereas
 * ARCS ULONG remains the explicit 32-bit firmware ABI type in <arcs/types.h>.
 */
typedef unsigned char   uchar_t;
typedef unsigned short  ushort_t;
typedef unsigned int    uint_t;
typedef unsigned long   ulong_t;

typedef unsigned int    lock_t;
typedef int             processorid_t;
typedef int             toid_t;
typedef __uint64_t      k_machreg_t;
typedef __int64_t       k_smachreg_t;

#endif
