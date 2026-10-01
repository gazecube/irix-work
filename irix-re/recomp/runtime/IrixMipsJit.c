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
    uint64_t trace_ra;
    IrixGuestAddr trace_ra_pc;
    int trace_ra_valid;
    IrixGuestAddr shadow_ra[4096];
    size_t shadow_ra_depth;
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

static int jit_guest_exec_mapped(IrixMipsJitState *s, IrixGuestAddr pc)
{
    size_t i;
    for (i = 0; i < s->rt->memory.count; ++i) {
        IrixGuestSegment *seg = &s->rt->memory.segments[i];
        uint64_t end = (uint64_t)seg->guest_base + (uint64_t)seg->size;
        if ((uint64_t)pc >= seg->guest_base &&
            (uint64_t)pc + 4u <= end)
            return (seg->flags & 4u) != 0;
    }
    return 0;
}

static void shadow_return_hook(uc_engine *uc, uint64_t address,
                               uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    IrixGuestAddr pc = (IrixGuestAddr)(uint32_t)address;
    uint32_t insn, op, rs, rd, fn;
    uint64_t ra64 = 0;
    IrixGuestAddr ra;

    (void)size;
    if (!irix_guest_ptr(s->rt, pc, 4u))
        return;

    insn = irix_guest_read32be(s->rt, pc);
    op = insn >> 26;
    rs = (insn >> 21) & 31u;
    rd = (insn >> 11) & 31u;
    fn = insn & 63u;

    /*
     * Narrow rld startup trace.  This region contains the continuation that
     * receives the RI-recovered syscall result and the epilogue currently
     * restoring RA=0.  Keep it local so we can identify the real failing
     * operation without enabling the global instruction tracer.
     */
    if (pc >= 0x0fb71040u && pc <= 0x0fb710d8u) {
        uint64_t v0 = 0, a0 = 0, a1 = 0, a2 = 0, a3 = 0, sp = 0, t9 = 0;
        (void)uc_reg_read(uc, UC_MIPS_REG_V0, &v0);
        (void)uc_reg_read(uc, UC_MIPS_REG_A0, &a0);
        (void)uc_reg_read(uc, UC_MIPS_REG_A1, &a1);
        (void)uc_reg_read(uc, UC_MIPS_REG_A2, &a2);
        (void)uc_reg_read(uc, UC_MIPS_REG_A3, &a3);
        (void)uc_reg_read(uc, UC_MIPS_REG_SP, &sp);
        (void)uc_reg_read(uc, UC_MIPS_REG_T9, &t9);
        fprintf(stderr,
                "IRIX rld 710 trace: pc=%08x insn=%08x "
                "v0=%08x a0-a3=%08x,%08x,%08x,%08x "
                "sp=%08x ra=%08x t9=%08x shadow_depth=%zu\n",
                pc, insn, (uint32_t)v0,
                (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3,
                (uint32_t)sp, (uint32_t)ra64, (uint32_t)t9,
                s->shadow_ra_depth);
    }

    /* JAL always links to pc+8. */
    if (op == 0x03u) {
        if (s->shadow_ra_depth < 4096u)
            s->shadow_ra[s->shadow_ra_depth++] = pc + 8u;
        return;
    }

    /* JALR only matters to us when the architectural link register is RA. */
    if (op == 0u && fn == 0x09u && rd == 31u) {
        if (s->shadow_ra_depth < 4096u)
            s->shadow_ra[s->shadow_ra_depth++] = pc + 8u;
        return;
    }

    /*
     * PIC startup/rld code also uses the REGIMM branch-and-link forms as
     * calls (most commonly BGEZAL $zero, target, i.e. BAL).  The shadow
     * return stack used to ignore these completely, so an RI raised inside a
     * callee could expose RA==0 with no shadow return available to repair it.
     * Record only taken branches; the likely forms share the same link
     * semantics but annul the delay slot when not taken.
     */
    if (op == 0x01u) {
        unsigned rtno = (insn >> 16) & 31u;
        int is_link = (rtno == 0x10u || rtno == 0x11u ||
                       rtno == 0x12u || rtno == 0x13u);
        if (is_link) {
            uint64_t rsv64 = 0;
            int take = 0;
            if (uc_reg_read(uc, gpr_regs[rs], &rsv64) == UC_ERR_OK) {
                int64_t rsv = (int64_t)(int32_t)(uint32_t)rsv64;
                if (rtno == 0x10u || rtno == 0x12u) /* BLTZAL[/L] */
                    take = rsv < 0;
                else                                  /* BGEZAL[/L] */
                    take = rsv >= 0;
                if (take && s->shadow_ra_depth < 4096u)
                    s->shadow_ra[s->shadow_ra_depth++] = pc + 8u;
            }
            return;
        }
    }

    if (!(op == 0u && fn == 0x08u && rs == 31u))
        return;

    if (uc_reg_read(uc, UC_MIPS_REG_RA, &ra64) != UC_ERR_OK)
        return;
    ra = (IrixGuestAddr)(uint32_t)ra64;

    if (s->shadow_ra_depth) {
        IrixGuestAddr expected = s->shadow_ra[s->shadow_ra_depth - 1u];

        if (ra == expected) {
            --s->shadow_ra_depth;
            return;
        }

        if (!jit_guest_exec_mapped(s, ra) &&
            jit_guest_exec_mapped(s, expected)) {
            uint64_t repaired = (uint64_t)(int64_t)(int32_t)expected;
            fprintf(stderr,
                    "IRIX JIT HACK: repairing bogus jr ra at pc=%08x "
                    "ra=%08x -> %08x shadow_depth=%zu\n",
                    pc, ra, expected, s->shadow_ra_depth);
            (void)uc_reg_write(uc, UC_MIPS_REG_RA, &repaired);
            --s->shadow_ra_depth;
        }
    }
}

static void trace_code_hook(uc_engine *uc, uint64_t address,
                            uint32_t size, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    IrixGuestAddr pc = (IrixGuestAddr)(uint32_t)address;
    uint64_t ra = 0, sp = 0;

    (void)size;
    (void)uc_reg_read(uc, UC_MIPS_REG_SP, &sp);
    if (uc_reg_read(uc, UC_MIPS_REG_RA, &ra) == UC_ERR_OK) {
        if (s->trace_ra_valid && ra != s->trace_ra) {
            uint32_t prev_word = 0;
            IrixGuestAddr load_addr = 0;
            if (irix_guest_ptr(s->rt, s->trace_ra_pc, 4u))
                prev_word = irix_guest_read32be(s->rt, s->trace_ra_pc);

            /*
             * If the instruction that changed RA was LD ra,imm(sp), show
             * the exact guest address it loaded.  This uses the live Unicorn
             * SP, not the runtime CPU snapshot.
             */
            if ((prev_word >> 26) == 0x37u &&
                ((prev_word >> 21) & 31u) == 29u &&
                ((prev_word >> 16) & 31u) == 31u) {
                int16_t imm = (int16_t)(prev_word & 0xffffu);
                load_addr = (IrixGuestAddr)((uint32_t)sp +
                                             (int32_t)imm);
            }

            fprintf(stderr,
                    "IRIX JIT RA change: after_pc=%08x insn=%08x "
                    "ra:%08x->%08x next_pc=%08x live_sp=%08x "
                    "load_addr=%08x\n",
                    s->trace_ra_pc, prev_word,
                    (uint32_t)s->trace_ra, (uint32_t)ra, pc,
                    (uint32_t)sp, load_addr);
        }
        s->trace_ra = ra;
        s->trace_ra_pc = pc;
        s->trace_ra_valid = 1;
    }
    s->last_pc = pc;
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

    /*
     * Bring-up cheat #2: some IRIX/rld paths walk well beyond a data mapping
     * while constructing loader state.  Mapping one page per miss exhausts
     * IRIX_GUEST_MAX_SEGMENTS quickly, so grow a single large RW slab instead.
     * Cap it at the next existing guest segment (or 16 MiB) to avoid overlap.
     * Never make these pages executable.
     */
    if (type == UC_MEM_READ_UNMAPPED ||
        type == UC_MEM_WRITE_UNMAPPED) {
        IrixGuestAddr page = (IrixGuestAddr)((uint32_t)address & ~0xfffu);
        uint64_t limit = (uint64_t)page + UINT64_C(0x01000000);
        uint64_t next_base = UINT64_C(0x100000000);
        size_t i;
        int adjacent = 0;

        for (i = 0; i < s->rt->memory.count; ++i) {
            IrixGuestSegment *seg = &s->rt->memory.segments[i];
            uint64_t end = (uint64_t)seg->guest_base + (uint64_t)seg->size;

            if ((seg->flags & 2u) &&
                (uint64_t)page >= end &&
                (uint64_t)page < end + UINT64_C(0x00010000))
                adjacent = 1;

            if ((uint64_t)seg->guest_base > (uint64_t)page &&
                (uint64_t)seg->guest_base < next_base)
                next_base = seg->guest_base;
        }

        if (next_base < limit)
            limit = next_base;

        limit &= ~UINT64_C(0xfff);

        if (adjacent && !irix_guest_ptr(s->rt, page, 1u) &&
            limit > (uint64_t)page) {
            uint64_t span64 = limit - (uint64_t)page;
            uint32_t span = span64 > UINT32_MAX ?
                            UINT32_MAX & ~0xfffu : (uint32_t)span64;
            size_t idx = s->rt->memory.count;

            if (span >= 0x1000u &&
                irix_guest_map(s->rt, page, span, 3u) == 0) {
                IrixGuestSegment *seg = &s->rt->memory.segments[idx];
                uc_err e = uc_mem_map_ptr(uc, page, span,
                                          UC_PROT_READ | UC_PROT_WRITE,
                                          seg->host_base);
                if (e == UC_ERR_OK) {
                    s->mapped[idx] = 1;
                    fprintf(stderr,
                            "IRIX JIT HACK: elastically mapped RW slab "
                            "%08x+%08x for %s at addr=%08x pc=%08x\n",
                            page, span,
                            type == UC_MEM_READ_UNMAPPED ? "read" : "write",
                            (uint32_t)address, s->last_pc);
                    return true;
                }
                fprintf(stderr,
                        "IRIX JIT HACK: elastic slab %08x+%08x failed: %s\n",
                        page, span, uc_strerror(e));
            }
        }
    }

    /*
     * Bring-up cheat: tolerate low NULL-adjacent DATA accesses by lazily
     * materializing a zero-filled 64 KiB cushion.  Keep it non-executable so
     * bogus control flow to NULL still faults loudly.
     */
    if ((type == UC_MEM_READ_UNMAPPED ||
         type == UC_MEM_WRITE_UNMAPPED) &&
        address < UINT64_C(0x00010000)) {
        size_t idx = s->rt->memory.count;
        if (!irix_guest_ptr(s->rt, 0u, 1u)) {
            if (irix_guest_map(s->rt, 0u, 0x00010000u, 3u) == 0) {
                IrixGuestSegment *seg = &s->rt->memory.segments[idx];
                uc_err e = uc_mem_map_ptr(uc, 0u, 0x00010000u,
                                          UC_PROT_READ | UC_PROT_WRITE,
                                          seg->host_base);
                if (e == UC_ERR_OK) {
                    s->mapped[idx] = 1;
                    fprintf(stderr,
                            "IRIX JIT HACK: mapped zero-filled low-memory "
                            "cushion for %s at addr=%08x pc=%08x\n",
                            type == UC_MEM_READ_UNMAPPED ? "read" : "write",
                            (uint32_t)address, s->last_pc);
                    return true;
                }
                fprintf(stderr,
                        "IRIX JIT HACK: low-memory cushion map failed: %s\n",
                        uc_strerror(e));
            }
        } else {
            size_t i;
            for (i = 0; i < s->rt->memory.count; ++i) {
                IrixGuestSegment *seg = &s->rt->memory.segments[i];
                uint64_t end = (uint64_t)seg->guest_base + seg->size;
                if (seg->guest_base == 0u && end >= 0x00010000u) {
                    if (!s->mapped[i]) {
                        uc_err e = uc_mem_map_ptr(uc, 0u, 0x00010000u,
                                                  UC_PROT_READ | UC_PROT_WRITE,
                                                  seg->host_base);
                        if (e == UC_ERR_OK)
                            s->mapped[i] = 1;
                    }
                    if (s->mapped[i])
                        return true;
                    break;
                }
            }
        }
    }

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

static void trace_stack_write_hook(uc_engine *uc, uc_mem_type type,
                                   uint64_t address, int size,
                                   int64_t value, void *opaque)
{
    IrixMipsJitState *s = (IrixMipsJitState *)opaque;
    uint64_t pc = 0, sp = 0, ra = 0;
    uint64_t uvalue = (uint64_t)value;
    int interesting = 0;

    (void)type;

    /*
     * Current failing frame restores RA from 0x7fdfe128.  Also report any
     * guest stack write whose value is 0x1000 so we can catch the producer
     * if the exact frame shifts slightly between runs.
     */
    if (address <= UINT64_C(0x7fdfe128) &&
        address + (uint64_t)size > UINT64_C(0x7fdfe128))
        interesting = 1;
    if (address >= UINT64_C(0x7f000000) &&
        address < UINT64_C(0x80000000) &&
        (uint32_t)uvalue == 0x00001000u)
        interesting = 1;

    if (!interesting)
        return;

    (void)uc_reg_read(uc, UC_MIPS_REG_PC, &pc);
    (void)uc_reg_read(uc, UC_MIPS_REG_SP, &sp);
    (void)uc_reg_read(uc, UC_MIPS_REG_RA, &ra);

    fprintf(stderr,
            "IRIX JIT stack write: pc=%08x addr=%08x size=%d "
            "value=%016llx sp=%08x ra=%08x last_pc=%08x\n",
            (uint32_t)pc, (uint32_t)address, size,
            (unsigned long long)uvalue,
            (uint32_t)sp, (uint32_t)ra, s->last_pc);
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
     * Bring-up cheat: maintain a shadow call/return stack.  If guest state
     * restores an obviously bogus RA and then executes jr ra, substitute the
     * most recent executable architectural return address.
     */
    err = uc_hook_add(s->uc, &hook, UC_HOOK_CODE,
                      (void *)shadow_return_hook, s, 1, 0);
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
    if (s->trace_code) {
        err = uc_hook_add(s->uc, &hook, UC_HOOK_MEM_WRITE,
                          (void *)trace_stack_write_hook, s, 1, 0);
        if (err != UC_ERR_OK)
            goto fail;
    }
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
        s->trace_ra_valid = 0;
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

        if (s->trace_code && pc >= 0x0fb71670u && pc <= 0x0fb71690u &&
            irix_guest_ptr(rt, 0x0fb71670u, 64u)) {
            unsigned k;
            fprintf(stderr, "IRIX JIT memset-loop context:");
            for (k = 0; k < 16u; ++k)
                fprintf(stderr, " %08x",
                        irix_guest_read32be(rt, 0x0fb71670u + k * 4u));
            fprintf(stderr, "\n");
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
                uint32_t prev1 = 0, prev2 = 0;
                uint32_t target0 = 0, target1 = 0, target2 = 0, target3 = 0;
                IrixGuestAddr branch_target = 0;
                unsigned op = word >> 26;

                if (irix_guest_ptr(rt, ipc + 4u, 4u))
                    next_word = irix_guest_read32be(rt, ipc + 4u);
                if (ipc >= 4u && irix_guest_ptr(rt, ipc - 4u, 4u))
                    prev1 = irix_guest_read32be(rt, ipc - 4u);
                if (ipc >= 8u && irix_guest_ptr(rt, ipc - 8u, 4u))
                    prev2 = irix_guest_read32be(rt, ipc - 8u);

                if (op == 0x01u || op == 0x04u || op == 0x05u ||
                    op == 0x06u || op == 0x07u || op == 0x14u ||
                    op == 0x15u || op == 0x16u || op == 0x17u) {
                    branch_target =
                        ipc + 4u +
                        (IrixGuestAddr)((int32_t)(int16_t)(word & 0xffffu) << 2);
                    if (irix_guest_ptr(rt, branch_target, 16u)) {
                        target0 = irix_guest_read32be(rt, branch_target + 0u);
                        target1 = irix_guest_read32be(rt, branch_target + 4u);
                        target2 = irix_guest_read32be(rt, branch_target + 8u);
                        target3 = irix_guest_read32be(rt, branch_target + 12u);
                    }
                }

                fprintf(stderr,
                        "IRIX JIT: unhandled CPU interrupt intno=%u pc=%08x "
                        "last_pc=%08x last_block=%08x "
                        "prev=%08x,%08x word=%08x delay=%08x target=%08x "
                        "target_words=%08x,%08x,%08x,%08x; "
                        "falling back to interpreter\n",
                        s->interrupt_no, ipc, s->last_pc, s->last_block,
                        prev2, prev1, word, next_word, branch_target,
                        target0, target1, target2, target3);
            }
            if (s->interrupt_no == 20u) { /* EXCP_RI */
                /*
                 * Do not single-step an RI instruction and then immediately
                 * re-enter Unicorn.  MIPS delay-slot/branch exception state is
                 * internal to Unicorn and is not cleared by writing the guest
                 * PC back after an interpreter step.  In particular, a valid
                 * branch immediately following a syscall can otherwise loop:
                 * Unicorn repeatedly reports the old branch PC even though the
                 * interpreter advanced cpu->pc.
                 *
                 * RI is already an exceptional slow path.  Hand the remainder
                 * of this guest call to the interpreter so execution resumes
                 * from a clean architectural state.
                 */
                {
                    uint32_t w2 = 0, w3 = 0;
                    IrixGuestAddr shadow_top =
                        s->shadow_ra_depth ?
                        s->shadow_ra[s->shadow_ra_depth - 1u] : 0u;
                    if (irix_guest_ptr(rt, ipc + 8u, 4u))
                        w2 = irix_guest_read32be(rt, ipc + 8u);
                    if (irix_guest_ptr(rt, ipc + 12u, 4u))
                        w3 = irix_guest_read32be(rt, ipc + 12u);
                    fprintf(stderr,
                            "IRIX JIT: RI recovery pc=%08x word=%08x "
                            "a3=%08x ra=%08x v0=%08x gp=%08x sp=%08x "
                            "next2=%08x,%08x shadow_depth=%zu shadow_top=%08x "
                            "-> interpreter\n",
                            ipc, word,
                            (uint32_t)cpu->gpr[7],
                            (uint32_t)cpu->gpr[31],
                            (uint32_t)cpu->gpr[2],
                            (uint32_t)cpu->gpr[28],
                            (uint32_t)cpu->gpr[29],
                            w2, w3,
                            s->shadow_ra_depth, shadow_top);
                }

                /*
                 * The normal JIT path repairs a bogus jr $ra from the shadow
                 * call stack in shadow_return_hook().  Once RI hands execution
                 * to the interpreter that hook will never run, so carry the
                 * same repair across the backend boundary here.  This matters
                 * for rld/libc startup: Unicorn can expose RA==0 after its RI
                 * exception plumbing even though we have a live architectural
                 * caller on the shadow stack.
                 */
                if (!synthetic_return &&
                    !jit_guest_exec_mapped(s, (IrixGuestAddr)(uint32_t)cpu->gpr[31]) &&
                    s->shadow_ra_depth) {
                    IrixGuestAddr expected =
                        s->shadow_ra[s->shadow_ra_depth - 1u];
                    if (jit_guest_exec_mapped(s, expected)) {
                        fprintf(stderr,
                                "IRIX JIT: RI fallback repairing ra=%08x -> %08x "
                                "shadow_depth=%zu\n",
                                (uint32_t)cpu->gpr[31], expected,
                                s->shadow_ra_depth);
                        cpu->gpr[31] =
                            (uint64_t)(int64_t)(int32_t)expected;
                        --s->shadow_ra_depth;
                    }
                }

                /*
                 * Keep the live architectural RA.  For top-level JIT calls we
                 * installed a synthetic sentinel only at entry; restoring the
                 * original (often zero) RA here destroys the real rld/libc
                 * caller that was established after startup began.
                 *
                 * Interpret only until the current callee returns to that live
                 * RA, then resume the JIT from the returned PC.  This avoids
                 * re-entering Unicorn at the RI branch itself (which is what
                 * caused the stale RI loop) without unwinding the whole guest
                 * process back to AppRunner.
                 */
                {
                    IrixGuestAddr live_ra =
                        (IrixGuestAddr)(uint32_t)cpu->gpr[31];
                    if (irix_mips_interp_call(rt, cpu, ipc) != 0 ||
                        rt->trapped) {
                        if (synthetic_return)
                            cpu->gpr[31] = original_ra;
                        return -1;
                    }
                    {
                        uint32_t rw0 = 0, rw1 = 0, rw2 = 0, rw3 = 0;
                        if (irix_guest_ptr(rt, cpu->pc, 16u)) {
                            rw0 = irix_guest_read32be(rt, cpu->pc + 0u);
                            rw1 = irix_guest_read32be(rt, cpu->pc + 4u);
                            rw2 = irix_guest_read32be(rt, cpu->pc + 8u);
                            rw3 = irix_guest_read32be(rt, cpu->pc + 12u);
                        }
                        fprintf(stderr,
                                "IRIX JIT: RI interpreter returned to %08x "
                                "(live_ra=%08x return_pc=%08x) "
                                "resume_words=%08x,%08x,%08x,%08x "
                                "a0-a3=%08x,%08x,%08x,%08x\n",
                                (uint32_t)cpu->pc, live_ra, return_pc,
                                rw0, rw1, rw2, rw3,
                                (uint32_t)cpu->gpr[4],
                                (uint32_t)cpu->gpr[5],
                                (uint32_t)cpu->gpr[6],
                                (uint32_t)cpu->gpr[7]);
                    }
                    if (cpu->pc == return_pc)
                        break;

                    /*
                     * The interpreter just executed the callee's jr $ra on
                     * our behalf. shadow_return_hook() therefore never saw
                     * that return and could not pop the matching JIT shadow
                     * frame. Leaving it behind is fatal: a later legitimate
                     * top-level jr $ra with architectural RA==0 is "repaired"
                     * back to this stale continuation, creating a loop.
                     */
                    if (cpu->pc == live_ra && s->shadow_ra_depth &&
                        s->shadow_ra[s->shadow_ra_depth - 1u] == live_ra) {
                        --s->shadow_ra_depth;
                        fprintf(stderr,
                                "IRIX JIT: RI fallback consumed shadow return "
                                "%08x depth=%zu\n",
                                live_ra, s->shadow_ra_depth);
                    }
                    /*
                     * Flushing TBs is not enough here.  Unicorn's MIPS backend
                     * keeps delay-slot/exception state outside the translated
                     * block cache, and the next uc_emu_start() can replay the
                     * pre-RI path even when PC has been moved to live_ra.
                     *
                     * Recreate the Unicorn CPU after the interpreter has
                     * completed the faulting callee.  Guest memory lives in
                     * the runtime and is remapped into the new engine on the
                     * next loop iteration, so this resets only Unicorn's
                     * hidden CPU/TB state.  Preserve our shadow call stack
                     * across the reset.
                     */
                    {
                        size_t saved_depth = s->shadow_ra_depth;
                        IrixGuestAddr *saved_shadow = 0;
                        if (saved_depth) {
                            saved_shadow = (IrixGuestAddr *)malloc(
                                saved_depth * sizeof(*saved_shadow));
                            if (!saved_shadow) {
                                if (synthetic_return)
                                    cpu->gpr[31] = original_ra;
                                return -1;
                            }
                            memcpy(saved_shadow, s->shadow_ra,
                                   saved_depth * sizeof(*saved_shadow));
                        }

                        fprintf(stderr,
                                "IRIX JIT: recreating Unicorn after RI "
                                "recovery at pc=%08x shadow_depth=%zu\n",
                                (uint32_t)cpu->pc, saved_depth);

                        if (s->uc)
                            (void)uc_close(s->uc);
                        rt->jit_state = 0;
                        free(s);

                        s = jit_state(rt);
                        if (!s) {
                            free(saved_shadow);
                            if (synthetic_return)
                                cpu->gpr[31] = original_ra;
                            return irix_mips_interp_call(rt, cpu, cpu->pc);
                        }
                        s->cpu = cpu;
                        if (saved_depth) {
                            memcpy(s->shadow_ra, saved_shadow,
                                   saved_depth * sizeof(*saved_shadow));
                            s->shadow_ra_depth = saved_depth;
                            free(saved_shadow);
                        }
                    }
                    continue;
                }
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
