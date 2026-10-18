#include "IrixN32Abi.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr, message) do { \
    if (!(expr)) { fprintf(stderr, "%s\n", message); ok = 0; } \
} while (0)

int main(void)
{
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    int ok = 1;
    const IrixN32AbiArgument args[] = {
        { IRIX_N32_ABI_I32, 0, UINT32_C(0xffffffff) },
        { IRIX_N32_ABI_U32, 0, UINT32_C(0xffffffff) },
        { IRIX_N32_ABI_PTR32, 0, UINT32_C(0xf1234567) },
        { IRIX_N32_ABI_I64, 0, UINT64_C(0x1122334455667788) },
        { IRIX_N32_ABI_F32, 0, UINT32_C(0x3f800000) },
        { IRIX_N32_ABI_F64, 0, UINT64_C(0x400921fb54442d18) },
        { IRIX_N32_ABI_F64, IRIX_N32_ABI_ARG_VARIADIC,
          UINT64_C(0x3ff8000000000000) },
        { IRIX_N32_ABI_U64, IRIX_N32_ABI_ARG_VARIADIC,
          UINT64_C(0x4008000000000000) },
        { IRIX_N32_ABI_PTR32, 0, UINT32_C(0x89abcdef) },
        { IRIX_N32_ABI_F32, 0, UINT32_C(0x40200000) },
        { IRIX_N32_ABI_I64, 0, UINT64_C(0x0123456789abcdef) }
    };

    irix_guest_runtime_init(&rt);
    memset(&cpu, 0, sizeof(cpu));
    if (irix_guest_map(&rt, 0x10000000u, 0x1000u, 3u) != 0) {
        fprintf(stderr, "could not map N32 ABI test stack\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }
    cpu.gpr[29] = 0x10000800u;
    CHECK(irix_n32_abi_pack_arguments(&rt, &cpu, args,
                                      sizeof(args) / sizeof(args[0])) == 0,
          "N32 scalar argument pack failed");

    CHECK(cpu.gpr[4] == UINT64_C(0xffffffffffffffff), "I32 GPR extension mismatch");
    CHECK(cpu.gpr[5] == UINT64_C(0xffffffffffffffff), "U32 GPR extension mismatch");
    CHECK(cpu.gpr[6] == UINT64_C(0x00000000f1234567), "pointer GPR extension mismatch");
    CHECK(cpu.gpr[7] == UINT64_C(0x1122334455667788), "I64 GPR mismatch");
    CHECK(irix_guest_fpr_get_s(&cpu, 16) == 1.0f, "F32 FPR placement mismatch");
    CHECK(irix_guest_fpr_get_d(&cpu, 17) == 3.141592653589793,
          "F64 FPR placement mismatch");
    CHECK(cpu.gpr[10] == UINT64_C(0x3ff8000000000000),
          "variadic F64 GPR placement mismatch");
    CHECK(cpu.gpr[11] == UINT64_C(0x4008000000000000),
          "variadic F64 GPR placement mismatch");
    CHECK(irix_guest_read64be(&rt, 0x10000800u) == UINT64_C(0x0000000089abcdef),
          "stack pointer right-justification mismatch");
    CHECK(irix_guest_read64be(&rt, 0x10000808u) == UINT64_C(0x4020000000000000),
          "stack F32 left-justification mismatch");
    CHECK(irix_guest_read64be(&rt, 0x10000810u) == UINT64_C(0x0123456789abcdef),
          "stack I64 placement mismatch");

    {
        IrixGuestCpu invalid_cpu;
        IrixN32AbiArgument invalid_args[9];
        size_t i;
        memset(&invalid_cpu, 0, sizeof(invalid_cpu));
        invalid_cpu.gpr[29] = 0x20000000u;
        for (i = 0; i < 9; ++i) {
            invalid_args[i].kind = IRIX_N32_ABI_U32;
            invalid_args[i].flags = 0;
            invalid_args[i].bits = (uint64_t)i;
        }
        CHECK(irix_n32_abi_pack_arguments(&rt, &invalid_cpu, invalid_args, 9) == -1,
              "unmapped N32 stack image was accepted");
        CHECK(invalid_cpu.gpr[4] == 0,
              "invalid N32 stack image partially changed argument registers");
        invalid_args[8].kind = IRIX_N32_ABI_F32;
        invalid_args[8].flags = IRIX_N32_ABI_ARG_VARIADIC;
        CHECK(irix_n32_abi_pack_arguments(&rt, &cpu, invalid_args, 9) == -1,
              "unpromoted variadic F32 was accepted");
    }

    irix_guest_runtime_destroy(&rt);
    return ok ? 0 : 1;
}
