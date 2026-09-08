/*
 * X64/UEFI compatibility form of the SGI <arcs/types.h> contract.
 *
 * The original IRIX header uses C 'long' for ARCS LONG/ULONG because that is
 * 32-bit in the original ABI.  Native x86-64 C makes long 64-bit, so keep the
 * ARCS ABI widths explicit here while leaving pristine SGI sources unchanged.
 */
#ifndef _ARCS_TYPES_H
#define _ARCS_TYPES_H

#include <Uefi.h>

#ifndef ARCS_UEFI_ARCS_SCALAR_TYPES
#define ARCS_UEFI_ARCS_SCALAR_TYPES

typedef CHAR8   CHAR;
typedef INT16   SHORT;
typedef INT32   LONG;
typedef UINT8   UCHAR;
typedef UINT16  USHORT;
typedef UINT32  ULONG;

typedef INT32   STATUS;
typedef CHAR   *PCHAR;
typedef SHORT  *PSHORT;
typedef LONG   *PLONG;
typedef UCHAR  *PUCHAR;
typedef USHORT *PUSHORT;
typedef ULONG  *PULONG;

/* VOID and BOOLEAN are already supplied by the EDK II compilation
 * environment.  Do not redeclare them here: EDK defines VOID as a macro and
 * BOOLEAN as its firmware scalar type. */
typedef VOID   *PVOID;

#endif /* ARCS_UEFI_ARCS_SCALAR_TYPES */

#endif /* _ARCS_TYPES_H */