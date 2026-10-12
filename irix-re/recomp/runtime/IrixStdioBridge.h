#ifndef IID_IRIX_STDIO_BRIDGE_H
#define IID_IRIX_STDIO_BRIDGE_H

#include <stddef.h>
#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

int irix_stdio_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_stdio_bridge_lookup(const char *name);

/* Write n bytes to the stream a guest FILE * names: a bridge slot (host
 * FILE), a real guest FILE (its descriptor, stdin/stdout/stderr included),
 * or, for an unusable handle, stderr.  Returns bytes written or -1. */
long irix_stdio_write_stream(IrixGuestRuntime *rt, IrixGuestAddr handle,
                             const char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif
