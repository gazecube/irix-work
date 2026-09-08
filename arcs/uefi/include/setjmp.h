/*
 * Native UEFI implementation of the setjmp/longjmp contract consumed by
 * original libsc parser sources.  Keep the source-level API unchanged and
 * use EDK II's architecture-specific X64 context save/restore underneath.
 */
#ifndef ARCS_UEFI_SETJMP_H
#define ARCS_UEFI_SETJMP_H

#include <Library/BaseLib.h>

typedef BASE_LIBRARY_JUMP_BUFFER jmp_buf[1];

#define setjmp(env)      (int)SetJump(&(env)[0])
#define longjmp(env,val) LongJump(&(env)[0], (UINTN)((val) ? (val) : 1))

#endif /* ARCS_UEFI_SETJMP_H */
