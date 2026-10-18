#include "IrixN32Abi.h"

#include <string.h>

static int is_float_kind(IrixN32AbiArgumentKind kind)
{
    return kind == IRIX_N32_ABI_F32 || kind == IRIX_N32_ABI_F64;
}

static int is_valid_kind(IrixN32AbiArgumentKind kind)
{
    return kind >= IRIX_N32_ABI_I32 && kind <= IRIX_N32_ABI_F64;
}

static uint64_t scalar_bits(const IrixN32AbiArgument *arg)
{
    uint32_t word = (uint32_t)arg->bits;
    switch (arg->kind) {
    case IRIX_N32_ABI_I32:
    case IRIX_N32_ABI_U32:
        /* N32 extends 32-bit integer argument words to 64 bits in GPRs. */
        return (uint64_t)(int64_t)(int32_t)word;
    case IRIX_N32_ABI_PTR32:
        return (uint64_t)word;
    case IRIX_N32_ABI_F32:
        return (uint64_t)word << 32;
    case IRIX_N32_ABI_I64:
    case IRIX_N32_ABI_U64:
    case IRIX_N32_ABI_F64:
        return arg->bits;
    }
    return 0;
}

int irix_n32_abi_pack_arguments(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                const IrixN32AbiArgument *arguments,
                                size_t argument_count)
{
    size_t i;
    uint64_t stack_base;

    if (!rt || !cpu || (argument_count && !arguments))
        return -1;
    stack_base = cpu->gpr[29];

    /* Validate before mutating any register or guest stack memory. */
    for (i = 0; i < argument_count; ++i) {
        if (!is_valid_kind(arguments[i].kind) ||
            (arguments[i].flags & ~IRIX_N32_ABI_ARG_VARIADIC) != 0)
            return -1;
        if ((arguments[i].flags & IRIX_N32_ABI_ARG_VARIADIC) &&
            arguments[i].kind == IRIX_N32_ABI_F32)
            return -1; /* C default argument promotions require an F64 value. */
    }
    if (argument_count > 8 &&
        ((stack_base & 7u) != 0 || stack_base > UINT32_MAX ||
         argument_count - 8 >
             ((uint64_t)UINT32_MAX + 1u - stack_base) / 8u))
        return -1;
    for (i = 8; i < argument_count; ++i) {
        IrixGuestAddr address = (IrixGuestAddr)(stack_base +
                                                (uint64_t)(i - 8) * 8u);
        if (!irix_guest_ptr(rt, address, 8u))
            return -1;
    }

    for (i = 0; i < argument_count; ++i) {
        const IrixN32AbiArgument *arg = &arguments[i];
        int fp_register = is_float_kind(arg->kind) &&
                          !(arg->flags & IRIX_N32_ABI_ARG_VARIADIC);
        if (i < 8) {
            if (fp_register) {
                if (arg->kind == IRIX_N32_ABI_F32) {
                    uint32_t word = (uint32_t)arg->bits;
                    float value;
                    memcpy(&value, &word, sizeof(value));
                    irix_guest_fpr_set_s(cpu, (unsigned)(12 + i), value);
                } else {
                    uint64_t bits = arg->bits;
                    double value;
                    memcpy(&value, &bits, sizeof(value));
                    irix_guest_fpr_set_d(cpu, (unsigned)(12 + i), value);
                }
            } else {
                cpu->gpr[4 + i] = scalar_bits(arg);
            }
        } else {
            uint64_t slot = (uint64_t)(i - 8);
            IrixGuestAddr address = (IrixGuestAddr)(stack_base + slot * 8u);
            irix_guest_write64be(rt, address, scalar_bits(arg));
        }
    }
    return 0;
}
