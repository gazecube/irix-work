#ifndef IRIX_N32_ABI_H
#define IRIX_N32_ABI_H

#include "IrixGuest.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IrixN32AbiArgumentKind {
    IRIX_N32_ABI_I32,
    IRIX_N32_ABI_U32,
    IRIX_N32_ABI_PTR32,
    IRIX_N32_ABI_I64,
    IRIX_N32_ABI_U64,
    IRIX_N32_ABI_F32,
    IRIX_N32_ABI_F64
} IrixN32AbiArgumentKind;

#define IRIX_N32_ABI_ARG_VARIADIC 0x1u

typedef struct IrixN32AbiArgument {
    IrixN32AbiArgumentKind kind;
    unsigned flags;
    uint64_t bits;
} IrixN32AbiArgument;

/*
 * Populate the N32 argument registers and stack image for a translated call.
 * `bits` contains the native bit representation of the declared scalar type.
 * This intentionally covers scalar arguments only; aggregates and return
 * value conversion belong to generated, signature-aware thunks.
 */
int irix_n32_abi_pack_arguments(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                const IrixN32AbiArgument *arguments,
                                size_t argument_count);

#ifdef __cplusplus
}
#endif

#endif
