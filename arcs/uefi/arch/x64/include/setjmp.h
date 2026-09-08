/*
 * Native x86-64/UEFI implementation of the setjmp/longjmp contract consumed
 * by the original standalone libsc sources.
 *
 * The pristine SGI <setjmp.h> describes the MIPS register save layout and
 * therefore cannot represent an X64 execution context.  Keep the source-level
 * API unchanged while supplying the architecture-specific storage and
 * implementation from EDK II BaseLib beneath the original sources.
 */
#ifndef ARCS_UEFI_X64_SETJMP_H
#define ARCS_UEFI_X64_SETJMP_H

#include <Library/BaseLib.h>

typedef BASE_LIBRARY_JUMP_BUFFER jmp_buf[1];

#define setjmp(env)      ((int)SetJump(&(env)[0]))
#define longjmp(env,val) LongJump(&(env)[0], (UINTN)((val) ? (val) : 1))

#endif /* ARCS_UEFI_X64_SETJMP_H */
