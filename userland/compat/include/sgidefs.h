/* IRIX <sgidefs.h> for a native build: an i386 build is N32, an x86_64 build is N64 */
#ifndef __SGIDEFS_H__
#define __SGIDEFS_H__
#define _MIPS_ISA_MIPS1 1
#define _MIPS_ISA_MIPS2 2
#define _MIPS_ISA_MIPS3 3
#define _MIPS_ISA_MIPS4 4
#define _MIPS_SIM_ABI32 1
#define _MIPS_SIM_NABI32 2
#define _MIPS_SIM_ABI64 3
#define _ABIO32 _MIPS_SIM_ABI32
#define _ABIN32 _MIPS_SIM_NABI32
#define _ABI64 _MIPS_SIM_ABI64
#ifndef _MIPS_SIM
#if __SIZEOF_POINTER__ == 8
#define _MIPS_SIM _ABI64
#define _MIPS_SZLONG 64
#define _MIPS_SZPTR 64
#else
#define _MIPS_SIM _ABIN32
#define _MIPS_SZLONG 32
#define _MIPS_SZPTR 32
#endif
#define _MIPS_SZINT 32
#define _MIPS_ISA _MIPS_ISA_MIPS3
#endif
#include <stdint.h>
typedef intptr_t __psint_t;
typedef uintptr_t __psunsigned_t;
typedef intptr_t __scint_t;
typedef uintptr_t __scunsigned_t;
#endif
