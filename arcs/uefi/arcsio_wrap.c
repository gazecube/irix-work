/*
 * The pristine IRIX libsk ARCS I/O layer (Open/Read/Write/Close/Seek/...),
 * compiled unmodified.  The four calls that the UEFI console also needs are
 * renamed here so saio_compat.c / prom_runtime_compat.c can route
 * StandardIn/StandardOut to the GOP console and everything else to the
 * original device and filesystem machinery.
 */
#define Open  ArcsOpen
#define Read  ArcsRead
#define Write ArcsWrite
#define Close ArcsClose
#include "../sgisrc/stand/arcs/lib/libsk/lib/arcsio.c"
