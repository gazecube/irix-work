/*
 * ARCS I/O interface used by the original standalone command machinery.
 *
 * This is the architecture-neutral portion of the SGI <arcs/io.h> contract
 * required by libsc while the native UEFI implementations live below it.
 */
#ifndef _ARCS_IO_H
#define _ARCS_IO_H

#include <Uefi.h>

#define StandardIn      0
#define StandardOut     1

#define PathNameMax             (255)
#define FileNameLengthMax       (32)

/* SGI ARCS scalar names.  ARCS LONG/ULONG are 32-bit even though this PROM
 * implementation itself is a native X64 UEFI image. */
typedef INT32   LONG;
typedef UINT32  ULONG;
typedef CHAR8   CHAR;
typedef UINT8   UCHAR;
typedef VOID    VOID;

extern LONG Execute(CHAR *, LONG, CHAR *[], CHAR *[]);

#endif /* _ARCS_IO_H */
