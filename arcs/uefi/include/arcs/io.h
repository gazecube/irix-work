/*
 * ARCS I/O interface used by the original standalone command machinery.
 *
 * This is the architecture-neutral portion of the SGI <arcs/io.h> contract
 * required by libsc while the native UEFI implementations live below it.
 */
#ifndef _ARCS_IO_H
#define _ARCS_IO_H

#include <arcs/types.h>

#define StandardIn      0
#define StandardOut     1

#define PathNameMax             (255)
#define FileNameLengthMax       (32)

extern LONG Execute(CHAR *, LONG, CHAR *[], CHAR *[]);

#endif /* _ARCS_IO_H */