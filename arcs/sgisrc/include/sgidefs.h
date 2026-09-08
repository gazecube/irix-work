/*
 * x86-64 UEFI compatibility boundary for the original SGI <sgidefs.h>.
 *
 * The SGI header defines these types from the active MIPS ABI.  The
 * reconstructed PROM is a native X64 UEFI application, so retain the
 * original names and 64-bit data-model semantics without pretending that
 * the compiler is targeting a MIPS ISA.
 */
#ifndef __SGIDEFS_H__
#define __SGIDEFS_H__

#define _MIPS_ISA_MIPS1        1
#define _MIPS_ISA_MIPS2        2
#define _MIPS_ISA_MIPS3        3
#define _MIPS_ISA_MIPS4        4

#define _MIPS_SIM_ABI32        1
#define _MIPS_SIM_NABI32       2
#define _MIPS_SIM_ABI64        3

/*
 * Source compatibility for code whose only _MIPS_SIM distinction is the
 * width of PROM addresses.  X64 has the pointer/long widths of the SGI
 * ABI64 build used by that code path.
 */
#ifndef _MIPS_SIM
#define _MIPS_SIM              _MIPS_SIM_ABI64
#endif
#ifndef _ABI64
#define _ABI64                 _MIPS_SIM_ABI64
#endif

#ifndef _MIPS_SZINT
#define _MIPS_SZINT            32
#endif
#ifndef _MIPS_SZLONG
#define _MIPS_SZLONG           64
#endif
#ifndef _MIPS_SZPTR
#define _MIPS_SZPTR            64
#endif

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)
typedef int                    __int32_t;
typedef unsigned int           __uint32_t;
typedef long                   __int64_t;
typedef unsigned long          __uint64_t;
typedef __int64_t              __psint_t;
typedef __uint64_t             __psunsigned_t;
typedef __int64_t              __scint_t;
typedef __uint64_t             __scunsigned_t;
#endif

#endif /* !__SGIDEFS_H__ */
