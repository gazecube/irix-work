#include "IrixMipsJit.h"
#include "IrixMipsInterp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef IRIX_HAVE_UNICORN
#include <unicorn/unicorn.h>

typedef enum IrixJitStopReason {
    IRIX_JIT_STOP_NONE = 0,
    IRIX_JIT_STOP_NATIVE,
    IRIX_JIT_STOP_INTERRUPT,
    IRIX_JIT_STOP_MEMORY,
    IRIX_JIT_STOP_TRAP_SKIP,
    IRIX_JIT_STOP_TRAP_TAKEN
} IrixJitStopReason;

typedef struct IrixMipsJitState {
    uc_engine *uc;
    IrixGuestRuntime *rt;
    IrixGuestCpu *cpu;
    unsigned char mapped[IRIX_GUEST_MAX_SEGMENTS];
    size_t hooked_function_count;
    IrixJitStopReason stop_reason;
    IrixGuestAddr stop_address;
    IrixGuestAddr last_block;
    IrixGuestAddr last_pc;
    uint32_t interrupt_no;
    uint64_t interrupt_gpr[32];
    int interrupt_gpr_valid;
    uint64_t fault_address;
    int fault_type;
    uint64_t fault_ra;
    uint64_t fault_t9;
    uint64_t fault_gp;
    uint64_t fault_sp;
    int trace_code;
} IrixMipsJitState;

static const int gpr_regs[32] = {
    UC_MIPS_REG_0, UC_MIPS_REG_1, UC_MIPS_REG_2, UC_MIPS_REG_3,
    UC_MIPS_REG_4, UC_MIPS_REG_5, UC_MIPS_REG_6, UC_MIPS_REG_7,
    UC_MIPS_REG_8, UC_MIPS_REG_9, UC_MIPS_REG_10, UC_MIPS_REG_11,
    UC_MIPS_REG_12, UC_MIPS_REG_13, UC_MIPS_REG_14, UC_MIPS_REG_15,
    UC_MIPS_REG_16, UC_MIPS_REG_17, UC_MIPS_REG_18, UC_MIPS_REG_19,
    UC_MIPS_REG_20, UC_MIPS_REG_21, UC_MIPS_REG_22, UC_MIPS_REG_23,
    UC_MIPS_REG_24, UC_MIPS_REG_25, UC_MIPS_REG_26, UC_MIPS_REG_27,
    UC_MIPS_REG_28, UC_MIPS_REG_29, UC_MIPS_REG_30, UC_MIPS_REG_31
};

static const int fpr_regs[32] = {
    UC_MIPS_REG_F0, UC_MIPS_REG_F1, UC_MIPS_REG_F2, UC_MIPS_REG_F3,
    UC_MIPS_REG_F4, UC_MIPS_REG_F5, UC_MIPS_REG_F6, UC_MIPS_REG_F7,
    UC_MIPS_REG_F8, UC_MIPS_REG_F9, UC_MIPS_REG_F10, UC_MIPS_REG_F11,
    UC_MIPS_REG_F12, UC_MIPS_REG_F13, UC_MIPS_REG_F14, UC_MIPS_REG_F15,
    UC_MIPS_REG_F16, UC_MIPS_REG_F17, UC_MIPS_REG_F18, UC_MIPS_REG_F19,
    UC_MIPS_REG_F20, UC_MIPS_REG_F21, UC_MIPS_REG_F22, UC_MIPS_REG_F23,
    UC_MIPS_REG_F24, UC_MIPS_REG_F25, UC_MIPS_REG_F26, UC_MIPS_REG_F27,
    UC_MIPS_REG_F28, UC_MIPS_REG_F29, UC_MIPS_REG_F30, UC_MIPS_REG_F31
};

static int sync_cpu_to_unicorn(IrixMipsJitState *s)
{
    unsigned i;
    uint64_t v;
    uint32_t fcsr;
    for (i = 1; i < 32u; ++i) {
        v = s->cpu->gpr[i];
        if (uc_reg_write(s->uc, gpr_regs[i], &v) != UC_ERR_OK)
            return -1;
    }
    v = s->cpu->hi;
    if (uc_reg_write(s->uc, UC_MIPS_REG_HI, &v) != UC_ERR_OK)
        return -1;
    v = s->cpu->lo;
    if (uc_reg_write(s->uc, UC_MIPS_REG_LO, &v) != UC_ERR_OK)
        return -1;
    for (i = 0; i < 32u; ++i) {
        v = s->cpu->fpr[i];
        if (uc_reg_write(s->uc, fpr_regs[i], &v) != UC_ERR_OK)
            return -1;
    }
    fcsr = s->cpu->fcr31;
    if (uc_reg_write(s->uc, UC_MIPS_REG_FCSR, &fcsr) != UC_ERR_OK)
        return -1;
    v = (uint32_t)s->cpu->pc;
    if (uc_reg_write(s->uc, UC_MIPS_REG_PC, &v) != UC_ERR_OK)
        return -1;
    return 0;
}

static int sync_cpu_from_unicorn(IrixMipsJitState *s)
{
    unsigned i;
    uint64_t v;
    uint32_t fcsr = 0;
    for (i = 1; i < 32u; ++i) {
        v = 0;
        if (uc_reg_read(s->uc, gpr_regs[i], &v) != UC_ERR_OK)
            return -1;
        s->cpu->gpr[i] = v;
    }
    v = 0;
    if (uc_reg_read(s->uc, UC_MIPS_REG_HI, &v) != UC_ERR_OK)
        return -1;
    s->cpu->hi = v;
    v = 0;
    if (uc_reg_read(s->uc, UC_MIPS_REG_LO, &v) != UC_ERR_OK)
        return -1;
    s->cpu->lo = v;
    for (i = 0; i < 32u; ++i) {
        v = 0;
        if (uc_reg_read(s->uc, fpr_regs[i], &v) != UC_ERR_OK)
            return -1;
        s->cpu->fpr[i] = v;
    }
    if (uc_reg_read(s->uc, UC_MIPS_REG_FCSR, &fcsr) != UC_ERR_OK)
        return -1;
    s->cpu->fcr31 = fcsr;
    v = 0;
    if (uc_reg_read(s->uc, UC_MIPS_REG_PC, &v) != UC_ERR_OK)
        return -1;
    s->cpu->pc = (IrixGuestAddr)(uint32_t)v;
    s->cpu->gpr[0] = 0;
    s->rt->current_pc = s->cpu->pc;
    return 0;
}

static int map_guest_segments(IrixMipsJitState *s)
{
    size_t i;
    for (i = 0; i < s->rt->memory.count; ++i) {
        IrixGuestSegment *seg = &s->rt->memory.segments[i];
        size_t map_size;
        uc_err err;
        if (s->mapped[i])
            continue;
        if (seg->guest_base & 0xfffu) {
            fprintf(stderr,
                    "IRIX JIT: skip unaligned guest segment base=%08x size=%x\n",
                    seg->guest_base, seg->size);
            s->mapped[i] = 2;
            continue;
        }
        map_size = ((size_t)seg->size + 0xfffu) & ~(size_t)0xfffu;
        if (!map_size)
            return -1;
        err = uc_mem_map_ptr(s->uc, seg->guest_base, map_size,
                             UC_PROT_ALL, seg->host_base);
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "IRIX JIT: uc_mem_map_ptr %08x+%zx failed: %s\n",
                    seg->guest_base, map_size, uc_strerror(err));
            return -1;
        }
        s->mapped[i] = 1;
    }
    return 0;
}

static void native_entry_hook(uc_engine *uc, uint64_t address,
                              uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    IrixGuestFunctionEntry *entry;
    (void)size;
    entry = irix_guest_find_function(s->rt, (IrixGuestAddr)address);
    if (!entry || entry->address != (IrixGuestAddr)address ||
        !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE))
        return;
    s->stop_reason = IRIX_JIT_STOP_NATIVE;
    s->stop_address = (IrixGuestAddr)address;
    (void)uc_emu_stop(uc);
}

static void block_hook(uc_engine *uc, uint64_t address,
                       uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    (void)uc;
    (void)size;
    s->last_block = (IrixGuestAddr)(uint32_t)address;
}

static void trace_code_hook(uc_engine *uc, uint64_t address,
                            uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    (void)uc;
    (void)size;
    s->last_pc = (IrixGuestAddr)(uint32_t)address;
}

static void trap_instruction_hook(uc_engine *uc, uint64_t address,
                                  uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    IrixGuestAddr pc = (IrixGuestAddr)(uint32_t)address;
    uint32_t insn, op, rs, rtno, fn;
    uint64_t rsv = 0, rtv = 0;
    int take = 0;
    int is_trap = 0;
    int16_t simm;

    (void)size;
    if (!irix_guest_ptr(s->rt, pc, 4u))
        return;

    insn = irix_guest_read32be(s->rt, pc);
    op = insn >> 26;
    rs = (insn >> 21) & 31u;
    rtno = (insn >> 16) & 31u;
    fn = insn & 63u;
    simm = (int16_t)(insn & 0xffffu);

    if (op == 0u &&
        (fn == 0x30u || fn == 0x31u || fn == 0x32u ||
         fn == 0x33u || fn == 0x34u || fn == 0x36u)) {
        is_trap = 1;
        if (uc_reg_read(uc, gpr_regs[rs], &rsv) != UC_ERR_OK ||
            uc_reg_read(uc, gpr_regs[rtno], &rtv) != UC_ERR_OK)
            return;
        switch (fn) {
        case 0x30u: take = (int64_t)rsv >= (int64_t)rtv; break; /* TGE */
        case 0x31u: take = rsv >= rtv; break;                    /* TGEU */
        case 0x32u: take = (int64_t)rsv < (int64_t)rtv; break;  /* TLT */
        case 0x33u: take = rsv < rtv; break;                     /* TLTU */
        case 0x34u: take = rsv == rtv; break;                    /* TEQ */
        case 0x36u: take = rsv != rtv; break;                    /* TNE */
        }
    } else if (op == 0x01u &&
               (rtno == 0x08u || rtno == 0x09u || rtno == 0x0au ||
                rtno == 0x0bu || rtno == 0x0cu || rtno == 0x0eu)) {
        uint64_t imm64 = (uint64_t)(int64_t)simm;
        is_trap = 1;
        if (uc_reg_read(uc, gpr_regs[rs], &rsv) != UC_ERR_OK)
            return;
        switch (rtno) {
        case 0x08u: take = (int64_t)rsv >= (int64_t)simm; break; /* TGEI */
        case 0x09u: take = rsv >= imm64; break;                   /* TGEIU */
        case 0x0au: take = (int64_t)rsv < (int64_t)simm; break;  /* TLTI */
        case 0x0bu: take = rsv < imm64; break;                    /* TLTIU */
        case 0x0cu: take = rsv == imm64; break;                   /* TEQI */
        case 0x0eu: take = rsv != imm64; break;                   /* TNEI */
        }
    }

    if (!is_trap)
        return;

    s->last_pc = pc;
    s->stop_address = take ? pc : pc + 4u;
    s->stop_reason = take ? IRIX_JIT_STOP_TRAP_TAKEN
                          : IRIX_JIT_STOP_TRAP_SKIP;
    if (s->trace_code) {
        fprintf(stderr,
                "IRIX JIT trap shim: pc=%08x insn=%08x take=%d rs=%08x rt=%08x\n",
                pc, insn, take, (uint32_t)rsv, (uint32_t)rtv);
    }
    (void)uc_emu_stop(uc);
}

static void interrupt_hook(uc_engine *uc, uint32_t intno, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    uint64_t pc = 0;
    unsigned i;

    /*
     * Snapshot the architectural GPRs while Unicorn is still inside the
     * exception callback.  Its MIPS exception plumbing rewrites PC to
     * uc->next_pc before invoking UC_HOOK_INTR, and some builds expose
     * unstable register state after uc_emu_start() returns.  Syscalls need
     * the exact pre-handler ABI state, especially v0.
     */
    s->interrupt_gpr[0] = 0;
    s->interrupt_gpr_valid = 1;
    for (i = 1; i < 32u; ++i) {
        uint64_t v = 0;
        if (uc_reg_read(uc, gpr_regs[i], &v) != UC_ERR_OK) {
            s->interrupt_gpr_valid = 0;
            break;
        }
        s->interrupt_gpr[i] = v;
    }

    (void)uc_reg_read(uc, UC_MIPS_REG_PC, &pc);
    s->stop_reason = IRIX_JIT_STOP_INTERRUPT;
    s->interrupt_no = intno;
    s->stop_address = (IrixGuestAddr)(uint32_t)pc;
    (void)uc_emu_stop(uc);
}

static bool invalid_memory_hook(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size,
                                int64_t value, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    (void)size;
    (void)value;
    s->stop_reason = IRIX_JIT_STOP_MEMORY;
    s->fault_address = address;
    s->fault_type = (int)type;
    s->fault_ra = 0;
    s->fault_t9 = 0;
    s->fault_gp = 0;
    s->fault_sp = 0;
    (void)uc_reg_read(uc, UC_MIPS_REG_RA, &s->fault_ra);
    (void)uc_reg_read(uc, UC_MIPS_REG_T9, &s->fault_t9);
    (void)uc_reg_read(uc, UC_MIPS_REG_GP, &s->fault_gp);
    (void)uc_reg_read(uc, UC_MIPS_REG_SP, &s->fault_sp);
    return false;
}

static int install_native_hooks(IrixMipsJitState *s)
{
    size_t i;
    for (i = s->hooked_function_count; i < s->rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &s->rt->functions[i];
        IrixGuestFunctionEntry *winner;
        uc_hook hook;
        uc_err err;
        if (!(entry->flags & IRIX_GUEST_FUNCTION_NATIVE))
            continue;
        winner = irix_guest_find_function(s->rt, entry->address);
        if (winner != entry)
            continue;
        err = uc_hook_add(s->uc, &hook, UC_HOOK_CODE,
                          (void *)native_entry_hook, s,
                          entry->address, entry->address);
        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "IRIX JIT: native hook %08x %s failed: %s\n",
                    entry->address, entry->name ? entry->name : "?",
                    uc_strerror(err));
            return -1;
        }
    }
    s->hooked_function_count = s->rt->function_count;
    return 0;
}

static IrixMipsJitState *jit_state(IrixGuestRuntime *rt)
{
    IrixMipsJitState *s = (IrixMipsJitState *)rt->jit_state;
    uc_err err;
    uc_hook hook;
    uint32_t status;

    if (s)
        return s;

    s = (IrixMipsJitState *)calloc(1, sizeof(*s));
    if (!s)
        return 0;
    s->rt = rt;

    err = uc_open(UC_ARCH_MIPS,
                  (uc_mode)(UC_MODE_MIPS64 | UC_MODE_BIG_ENDIAN),
                  &s->uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "IRIX JIT: uc_open failed: %s\n", uc_strerror(err));
        free(s);
        return 0;
    }
#if UC_API_MAJOR >= 2
    err = uc_ctl_set_cpu_model(s->uc, UC_CPU_MIPS64_R4000);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "IRIX JIT: R4000 CPU model failed: %s\n",
                uc_strerror(err));
        uc_close(s->uc);
        free(s);
        return 0;
    }

    /*
     * The IID runtime already owns the IRIX guest virtual address space.
     * Unicorn's normal CPU TLB mode treats uc_mem_map[_ptr]() addresses as
     * physical while uc_emu_start() takes virtual PCs, which makes the first
     * userspace fetch raise EXCP_TLBL.  UC_TLB_VIRTUAL gives us the intended
     * flat vaddr==mapped-address model and leaves IRIX VM policy to IID.
     */
    err = uc_ctl_tlb_mode(s->uc, UC_TLB_VIRTUAL);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "IRIX JIT: virtual TLB mode failed: %s\n",
                uc_strerror(err));
        uc_close(s->uc);
        free(s);
        return 0;
    }
#endif

    /* R4000 N32 userspace: CU1 + FR + UX + KSU=user. */
    status = 0x24000030u;
    (void)uc_reg_write(s->uc, UC_MIPS_REG_CP0_STATUS, &status);

    s->trace_code = getenv("IRIX_JIT_TRACE") != 0;

    err = uc_hook_add(s->uc, &hook, UC_HOOK_BLOCK,
                      (void *)block_hook, s, 1, 0);
    if (err != UC_ERR_OK)
        goto fail;

    /*
     * Unicorn's R4000 path currently reports several architectural MIPS trap
     * instructions as RI.  Intercept them before execution so false traps can
     * remain on the JIT fast path instead of dropping into the interpreter.
     */
    err = uc_hook_add(s->uc, &hook, UC_HOOK_CODE,
                      (void *)trap_instruction_hook, s, 1, 0);
    if (err != UC_ERR_OK)
        goto fail;

    if (s->trace_code) {
        err = uc_hook_add(s->uc, &hook, UC_HOOK_CODE,
                          (void *)trace_code_hook, s, 1, 0);
        if (err != UC_ERR_OK)
            goto fail;
    }
    err = uc_hook_add(s->uc, &hook, UC_HOOK_INTR,
                      (void *)interrupt_hook, s, 1, 0);
    if (err != UC_ERR_OK)
        goto fail;
    err = uc_hook_add(s->uc, &hook, UC_HOOK_MEM_UNMAPPED,
                      (void *)invalid_memory_hook, s, 1, 0);
    if (err != UC_ERR_OK)
        goto fail;

    rt->jit_state = s;
    return s;

fail:
    fprintf(stderr, "IRIX JIT: hook setup failed: %s\n", uc_strerror(err));
    uc_close(s->uc);
    free(s);
    return 0;
}

static int is_syscall_word(uint32_t word)
{
    return (word & 0xfc00003fu) == 0x0000000cu;
}

static int is_break_word(uint32_t word)
{
    return (word & 0xfc00003fu) == 0x0000000du;
}

int irix_mips_jit_available(void)
{
    return 1;
}

int irix_mips_jit_requested(void)
{
    const char *v = getenv("IRIX_EXEC_BACKEND");
    if (v && *v) {
        if (!strcmp(v, "static") || !strcmp(v, "recomp") ||
            !strcmp(v, "interp") || !strcmp(v, "interpreter") ||
            !strcmp(v, "off"))
            return 0;
        return !strcmp(v, "jit") || !strcmp(v, "dynarec") ||
               !strcmp(v, "unicorn");
    }
#ifdef IRIX_DEFAULT_JIT
    return 1;
#else
    return 0;
#endif
}

int irix_mips_jit_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                       IrixGuestAddr address)
{
    IrixMipsJitState *s = jit_state(rt);
    IrixGuestAddr return_pc;
    IrixGuestAddr original_ra;
    int synthetic_return = 0;

    if (!s)
        return irix_mips_interp_call(rt, cpu, address);

    s->cpu = cpu;
    original_ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
    return_pc = original_ra;
    if (!return_pc) {
        return_pc = 0x7fff0000u;
        while (return_pc >= 0x70000000u &&
               irix_guest_ptr(rt, return_pc, 1u))
            return_pc -= 0x1000u;
        if (return_pc < 0x70000000u)
            return irix_mips_interp_call(rt, cpu, address);
        cpu->gpr[31] = (uint64_t)(int64_t)(int32_t)return_pc;
        synthetic_return = 1;
    }
    cpu->pc = address;
    rt->current_pc = address;

    for (;;) {
        IrixGuestAddr pc;
        uc_err err;

        if (map_guest_segments(s) != 0 ||
            install_native_hooks(s) != 0 ||
            sync_cpu_to_unicorn(s) != 0) {
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return irix_mips_interp_call(rt, cpu, cpu->pc);
        }

        s->stop_reason = IRIX_JIT_STOP_NONE;
        s->stop_address = 0;
        s->interrupt_no = 0;
        s->interrupt_gpr_valid = 0;
        s->last_block = 0;
        s->last_pc = 0;
        s->fault_address = 0;
        s->fault_type = 0;
        s->fault_ra = 0;
        s->fault_t9 = 0;
        s->fault_gp = 0;
        s->fault_sp = 0;
        pc = cpu->pc;

        if (s->trace_code && irix_guest_ptr(rt, pc, 16u)) {
            fprintf(stderr,
                    "IRIX JIT enter pc=%08x words=%08x,%08x,%08x,%08x status=%08x\n",
                    pc,
                    irix_guest_read32be(rt, pc + 0u),
                    irix_guest_read32be(rt, pc + 4u),
                    irix_guest_read32be(rt, pc + 8u),
                    irix_guest_read32be(rt, pc + 12u),
                    0x24000030u);
        }

        err = uc_emu_start(s->uc, pc, return_pc, 0, 0);
        if (sync_cpu_from_unicorn(s) != 0) {
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return -1;
        }

        /*
         * For MIPS exceptions use the register image captured by
         * interrupt_hook(), before Unicorn unwinds back to uc_emu_start().
         */
        if (s->stop_reason == IRIX_JIT_STOP_INTERRUPT &&
            s->interrupt_gpr_valid) {
            unsigned i;
            for (i = 1; i < 32u; ++i)
                cpu->gpr[i] = s->interrupt_gpr[i];
            cpu->gpr[0] = 0;
            if (s->trace_code) {
                fprintf(stderr,
                        "IRIX JIT interrupt snapshot: intno=%u v0=%08x "
                        "a0-a3=%08x,%08x,%08x,%08x\n",
                        s->interrupt_no,
                        (uint32_t)cpu->gpr[2],
                        (uint32_t)cpu->gpr[4],
                        (uint32_t)cpu->gpr[5],
                        (uint32_t)cpu->gpr[6],
                        (uint32_t)cpu->gpr[7]);
            }
        }

        if (s->stop_reason == IRIX_JIT_STOP_TRAP_SKIP) {
            cpu->pc = s->stop_address;
            rt->current_pc = cpu->pc;
            continue;
        }

        if (s->stop_reason == IRIX_JIT_STOP_TRAP_TAKEN) {
            cpu->pc = s->stop_address;
            rt->current_pc = cpu->pc;
            rt->fault_addr = 0;
            irix_guest_trap(rt, cpu, 0xeeee4013u);
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return -1;
        }

        if (s->stop_reason == IRIX_JIT_STOP_NATIVE) {
            IrixGuestFunctionEntry *entry =
                irix_guest_find_function(rt, s->stop_address);
            IrixGuestAddr resume =
                (IrixGuestAddr)(uint32_t)cpu->gpr[31];
            cpu->pc = s->stop_address;
            rt->current_pc = s->stop_address;
            if (!entry || entry->address != s->stop_address ||
                !(entry->flags & IRIX_GUEST_FUNCTION_NATIVE) ||
                irix_guest_call_entry(rt, cpu, s->stop_address, entry) != 0) {
                if (synthetic_return)
                    cpu->gpr[31] = original_ra;
                return -1;
            }
            if (rt->trapped) {
                if (synthetic_return)
                    cpu->gpr[31] = original_ra;
                return -1;
            }
            cpu->pc = resume;
            rt->current_pc = resume;
#if UC_API_MAJOR >= 2
            /* Native bridges and rld compatibility hooks can patch guest
               code/data through the shared host mapping. Drop translated
               blocks before resuming so self-modifying/relocated code is
               never stale. */
            (void)uc_ctl_flush_tb(s->uc);
#endif
            if (resume == return_pc)
                break;
            continue;
        }

        if (s->stop_reason == IRIX_JIT_STOP_INTERRUPT) {
            IrixGuestAddr ipc = s->stop_address;
            uint32_t word = 0;

            /*
             * Unicorn reports MIPS exception classes directly.  Do not infer
             * "syscall" merely because an unrelated later exception happens
             * near a syscall instruction: that turned EXCP_RI into a bogus
             * second syscall with whatever value happened to be in v0.
             */
            if (s->interrupt_no == 17u) { /* EXCP_SYSCALL */
                if (!ipc && s->last_pc)
                    ipc = s->last_pc;
                if (irix_guest_ptr(rt, ipc, 4u))
                    word = irix_guest_read32be(rt, ipc);
                if (!is_syscall_word(word) &&
                    ipc >= 4u && irix_guest_ptr(rt, ipc - 4u, 4u)) {
                    uint32_t prev = irix_guest_read32be(rt, ipc - 4u);
                    if (is_syscall_word(prev)) {
                        ipc -= 4u;
                        word = prev;
                    }
                }
                cpu->pc = ipc;
                rt->current_pc = ipc;
                if (is_syscall_word(word)) {
                    irix_guest_syscall(rt, cpu);
                    if (rt->trapped) {
                        if (synthetic_return)
                            cpu->gpr[31] = original_ra;
                        return -1;
                    }
                    cpu->pc = ipc + 4u;
                    rt->current_pc = cpu->pc;
                    continue;
                }
            } else if (s->interrupt_no == 18u) { /* EXCP_BREAK */
                if (s->last_pc)
                    ipc = s->last_pc;
                if (irix_guest_ptr(rt, ipc, 4u))
                    word = irix_guest_read32be(rt, ipc);
                cpu->pc = ipc;
                rt->current_pc = ipc;
                if (is_break_word(word)) {
                    irix_guest_trap(rt, cpu, (word >> 6) & 0xfffffu);
                    if (synthetic_return)
                        cpu->gpr[31] = original_ra;
                    return -1;
                }
            } else {
                /*
                 * For RI/TLB/FPE/etc., the code hook is the best offending
                 * instruction address.  Unicorn may expose next_pc through
                 * the interrupt callback.
                 */
                if (s->last_pc)
                    ipc = s->last_pc;
                if (irix_guest_ptr(rt, ipc, 4u))
                    word = irix_guest_read32be(rt, ipc);
                cpu->pc = ipc;
                rt->current_pc = ipc;
            }

            {
                uint32_t next_word = 0;
                IrixGuestAddr branch_target = 0;
                unsigned op = word >> 26;

                if (irix_guest_ptr(rt, ipc + 4u, 4u))
                    next_word = irix_guest_read32be(rt, ipc + 4u);

                if (op == 0x01u || op == 0x04u || op == 0x05u ||
                    op == 0x06u || op == 0x07u || op == 0x14u ||
                    op == 0x15u || op == 0x16u || op == 0x17u) {
                    branch_target =
                        ipc + 4u +
                        (IrixGuestAddr)((int32_t)(int16_t)(word & 0xffffu) << 2);
                }

                fprintf(stderr,
                        "IRIX JIT: unhandled CPU interrupt intno=%u pc=%08x "
                        "last_pc=%08x last_block=%08x word=%08x "
                        "delay=%08x target=%08x; falling back to interpreter\n",
                        s->interrupt_no, ipc, s->last_pc, s->last_block,
                        word, next_word, branch_target);
            }
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return irix_mips_interp_call(rt, cpu, ipc);
        }

        if (s->stop_reason == IRIX_JIT_STOP_MEMORY) {
            IrixGuestAddr src = s->last_pc;
            uint32_t w0 = 0, wm1 = 0, wm2 = 0, wm3 = 0;
            if (src && irix_guest_ptr(rt, src, 4u))
                w0 = irix_guest_read32be(rt, src);
            if (src >= 4u && irix_guest_ptr(rt, src - 4u, 4u))
                wm1 = irix_guest_read32be(rt, src - 4u);
            if (src >= 8u && irix_guest_ptr(rt, src - 8u, 4u))
                wm2 = irix_guest_read32be(rt, src - 8u);
            if (src >= 12u && irix_guest_ptr(rt, src - 12u, 4u))
                wm3 = irix_guest_read32be(rt, src - 12u);

            rt->trapped = 1;
            rt->trap_pc = src ? src : cpu->pc;
            rt->fault_addr = (IrixGuestAddr)(uint32_t)s->fault_address;
            rt->trap_code = 0xeeee0004u;
            fprintf(stderr,
                    "IRIX JIT: memory fault type=%d fetchpc=%08x addr=%08x "
                    "last_pc=%08x last_block=%08x "
                    "ra=%08x t9=%08x gp=%08x sp=%08x "
                    "insn[-3..0]=%08x,%08x,%08x,%08x\n",
                    s->fault_type, cpu->pc, rt->fault_addr,
                    src, s->last_block,
                    (uint32_t)s->fault_ra, (uint32_t)s->fault_t9,
                    (uint32_t)s->fault_gp, (uint32_t)s->fault_sp,
                    wm3, wm2, wm1, w0);
            cpu->pc = rt->trap_pc;
            rt->current_pc = cpu->pc;
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return -1;
        }

        if (err != UC_ERR_OK) {
            fprintf(stderr,
                    "IRIX JIT: Unicorn stopped at pc=%08x: %s; "
                    "falling back to interpreter\n",
                    cpu->pc, uc_strerror(err));
            if (synthetic_return)
                cpu->gpr[31] = original_ra;
            return irix_mips_interp_call(rt, cpu, cpu->pc);
        }

        if (cpu->pc == return_pc)
            break;

        fprintf(stderr,
                "IRIX JIT: unexpected clean stop pc=%08x return=%08x; "
                "falling back to interpreter\n",
                cpu->pc, return_pc);
        if (synthetic_return)
            cpu->gpr[31] = original_ra;
        return irix_mips_interp_call(rt, cpu, cpu->pc);
    }

    if (synthetic_return)
        cpu->gpr[31] = original_ra;
    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}

void irix_mips_jit_shutdown(IrixGuestRuntime *rt)
{
    IrixMipsJitState *s;
    if (!rt)
        return;
    s = (IrixMipsJitState *)rt->jit_state;
    if (!s)
        return;
    if (s->uc)
        (void)uc_close(s->uc);
    free(s);
    rt->jit_state = 0;
}

void irix_mips_jit_guest_stub(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)irix_mips_jit_call(rt, cpu, cpu->pc);
}

#else

int irix_mips_jit_available(void)
{
    return 0;
}

int irix_mips_jit_requested(void)
{
    const char *v = getenv("IRIX_EXEC_BACKEND");
    if (v && *v) {
        if (!strcmp(v, "static") || !strcmp(v, "recomp") ||
            !strcmp(v, "interp") || !strcmp(v, "interpreter") ||
            !strcmp(v, "off"))
            return 0;
        return !strcmp(v, "jit") || !strcmp(v, "dynarec") ||
               !strcmp(v, "unicorn");
    }
#ifdef IRIX_DEFAULT_JIT
    return 1;
#else
    return 0;
#endif
}

int irix_mips_jit_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                       IrixGuestAddr address)
{
    static int warned;
    if (!warned) {
        fprintf(stderr,
                "IRIX JIT requested but Unicorn was not found at build time; "
                "using interpreter fallback\n");
        warned = 1;
    }
    return irix_mips_interp_call(rt, cpu, address);
}

void irix_mips_jit_shutdown(IrixGuestRuntime *rt)
{
    (void)rt;
}

void irix_mips_jit_guest_stub(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)irix_mips_interp_call(rt, cpu, cpu->pc);
}

#endif
