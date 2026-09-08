/*
 * Minimal standalone compatibility for <sys/debug.h>.
 *
 * Original libsc parser sources use ASSERT as a development invariant.  The
 * non-DEBUG IRIX definition is a no-op; retain exactly that behavior here
 * rather than importing kernel-only assertion machinery into UEFI.
 */
#ifndef __SYS_DEBUG_H__
#define __SYS_DEBUG_H__

#define ASSERT(x)       ((void)0)
#define METER(x)

#endif /* __SYS_DEBUG_H__ */
