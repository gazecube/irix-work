#pragma once
#include <errno.h>
#include <stddef.h>
/* Legacy SCSI backend is unavailable. Linux device modules use Linux APIs. */
struct dsreq { int unused; };
#define STA_GOOD 0
static inline int inquiry12(struct dsreq *, void *, int, int) { errno = ENOTSUP; return -1; }
