#include "IrixCxxBridge.h"

#include <stdio.h>
#include <string.h>

static IrixGuestRuntime *g_rt;

static void dump_words(IrixGuestRuntime *rt, const char *label, IrixGuestAddr addr)
{
    if (addr && irix_guest_ptr(rt, addr, 20u)) {
        fprintf(stderr, " %s=%08x,%08x,%08x,%08x,%08x",
                label,
                irix_guest_read32be(rt, addr + 0u),
                irix_guest_read32be(rt, addr + 4u),
                irix_guest_read32be(rt, addr + 8u),
                irix_guest_read32be(rt, addr + 12u),
                irix_guest_read32be(rt, addr + 16u));
    }
}

static void bridge___vec_delete2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    fprintf(stderr,
            "VEC_DELETE2 ABI a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
            (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
            (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
            (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
            (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
    dump_words(rt, "a0_words", (uint32_t)cpu->gpr[4]);
    dump_words(rt, "a3_words", (uint32_t)cpu->gpr[7]);
    dump_words(rt, "a4_words", (uint32_t)cpu->gpr[8]);
    dump_words(rt, "a5_words", (uint32_t)cpu->gpr[9]);
    fputc('\n', stderr);
    irix_guest_trap(rt, cpu, 0xeeee2102u);
}

void irix_cxx_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    (void)g_rt;
}

IrixGuestFunction irix_cxx_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "__vec_delete2")) return bridge___vec_delete2;
    return 0;
}
