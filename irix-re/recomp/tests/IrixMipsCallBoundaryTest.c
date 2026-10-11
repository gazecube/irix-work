#include "IrixGuest.h"
#include "IrixMipsInterp.h"
#include <stdio.h>
#include <string.h>

static void unused_translation(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu;
}

int main(void)
{
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixMipsTranslatedFrame frame;
    int ok = 1;
    irix_guest_runtime_init(&rt);
    memset(&cpu, 0, sizeof(cpu));
    if (irix_guest_map(&rt, 0x10000u, 0x100u, 7u)) return 1;
    irix_guest_register_recompiled_function_range(
        &rt, 0x10000u, 0x100u, unused_translation, "test_parent");
    /* A hidden helper inside the parent's recovered range returns 42. */
    irix_guest_write32be(&rt, 0x10080u, 0x03e00008u); /* jr ra */
    irix_guest_write32be(&rt, 0x10084u, 0x2402002au); /* li v0,42 */
    /* The caller's continuation returns 99 to its own caller. */
    irix_guest_write32be(&rt, 0x10040u, 0x3c1f0009u); /* lui ra,9 */
    irix_guest_write32be(&rt, 0x10044u, 0x03e00008u); /* jr ra */
    irix_guest_write32be(&rt, 0x10048u, 0x24020063u); /* li v0,99 */
    irix_mips_interp_push_translated(&frame, 0x10000u, 0x100u, 0x90000u);
    rt.current_pc = 0x10038u;
    cpu.gpr[31] = 0x10040u;
    if (irix_guest_call(&rt, &cpu, 0x10080u) || cpu.gpr[2] != 42u) {
        fprintf(stderr, "nested helper ran caller continuation: v0=%llu\n",
                (unsigned long long)cpu.gpr[2]);
        ok = 0;
    }
    /* A continuation handed off by generated C must retain the parent's
       captured return address even though a prior jal changed live ra. */
    rt.current_pc = 0x10038u;
    cpu.gpr[31] = 0x10020u;
    if (irix_mips_interp_call(&rt, &cpu, 0x10040u) || cpu.gpr[2] != 99u ||
        cpu.pc != 0x90000u) {
        fprintf(stderr, "translated continuation lost its outer return\n");
        ok = 0;
    }
    irix_mips_interp_pop_translated(&frame);
    irix_guest_runtime_destroy(&rt);
    return ok ? 0 : 1;
}
