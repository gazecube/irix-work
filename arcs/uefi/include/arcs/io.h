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

/* Original IRIX 6.5.7m ARCS open-mode ABI from kern/sys/arcs/io.h. */
typedef enum openmode {
    OpenReadOnly,
    OpenWriteOnly,
    OpenReadWrite,
    CreateWriteOnly,
    CreateReadWrite,
    SupersedeWriteOnly,
    SupersedeReadWrite,
    OpenDirectory,
    CreateDirectory
} OPENMODE;

/* Keep the original ARCS standalone call signatures visible to pristine
 * libsc sources.  The UEFI implementations live at the platform boundary. */
extern LONG Open(CHAR *, OPENMODE, ULONG *);
extern LONG Close(ULONG);
extern LONG Read(ULONG, void *, ULONG, ULONG *);
extern LONG Write(ULONG, void *, ULONG, ULONG *);
extern LONG Execute(CHAR *, LONG, CHAR *[], CHAR *[]);

#endif /* _ARCS_IO_H */
