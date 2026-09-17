#include "IrixGuest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IrixGuestSegment *find_segment(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size)
{
    size_t i;
    uint64_t end = (uint64_t)addr + (uint64_t)size;
    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        uint64_t seg_end = (uint64_t)seg->guest_base + (uint64_t)seg->size;
        if ((uint64_t)addr >= seg->guest_base && end <= seg_end)
            return seg;
    }
    return 0;
}

static void memory_fault(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    if (!rt->trapped) {
        rt->trapped = 1;
        rt->trap_pc = rt->current_pc;
        rt->fault_addr = addr;
        rt->trap_code = 0xeeee0004u;
    }
}

void irix_guest_runtime_init(IrixGuestRuntime *rt)
{
    memset(rt, 0, sizeof(*rt));
}

void irix_guest_runtime_destroy(IrixGuestRuntime *rt)
{
    size_t i;
    for (i = 0; i < rt->memory.count; ++i)
        free(rt->memory.segments[i].host_base);
    memset(rt, 0, sizeof(*rt));
}

int irix_guest_map(IrixGuestRuntime *rt, IrixGuestAddr base, uint32_t size, unsigned flags)
{
    IrixGuestSegment *seg;
    if (!size || rt->memory.count >= IRIX_GUEST_MAX_SEGMENTS)
        return -1;
    if (find_segment(rt, base, size))
        return -1;
    seg = &rt->memory.segments[rt->memory.count];
    seg->host_base = (uint8_t *)calloc(1, size);
    if (!seg->host_base)
        return -1;
    seg->guest_base = base;
    seg->size = size;
    seg->flags = flags;
    ++rt->memory.count;
    return 0;
}

void *irix_guest_ptr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size)
{
    IrixGuestSegment *seg = find_segment(rt, addr, size);
    if (!seg)
        return 0;
    return seg->host_base + (uint32_t)(addr - seg->guest_base);
}

uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return 0; }
    return *p;
}

uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) { memory_fault(rt, addr); return 0; }
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 4);
    if (!p) { memory_fault(rt, addr); return 0; }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint32_t hi = irix_guest_read32be(rt, addr);
    uint32_t lo = irix_guest_read32be(rt, addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 1);
    if (!p) { memory_fault(rt, addr); return; }
    *p = value;
}

void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 2);
    if (!p) { memory_fault(rt, addr); return; }
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    uint8_t *p = (uint8_t *)irix_guest_ptr(rt, addr, 4);
    if (!p) { memory_fault(rt, addr); return; }
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

void irix_guest_write64be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint64_t value)
{
    irix_guest_write32be(rt, addr, (uint32_t)(value >> 32));
    irix_guest_write32be(rt, addr + 4, (uint32_t)value);
}

float irix_guest_fpr_get_s(const IrixGuestCpu *cpu, unsigned reg)
{
    union { uint32_t u; float f; } v;
    v.u = (uint32_t)cpu->fpr[reg & 31u];
    return v.f;
}

double irix_guest_fpr_get_d(const IrixGuestCpu *cpu, unsigned reg)
{
    union { uint64_t u; double d; } v;
    v.u = cpu->fpr[reg & 31u];
    return v.d;
}

void irix_guest_fpr_set_s(IrixGuestCpu *cpu, unsigned reg, float value)
{
    union { uint32_t u; float f; } v;
    v.f = value;
    cpu->fpr[reg & 31u] = (cpu->fpr[reg & 31u] & UINT64_C(0xffffffff00000000)) | v.u;
}

void irix_guest_fpr_set_d(IrixGuestCpu *cpu, unsigned reg, double value)
{
    union { uint64_t u; double d; } v;
    v.d = value;
    cpu->fpr[reg & 31u] = v.u;
}

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name)
{
    IrixGuestFunctionEntry *entry;
    if (!function || rt->function_count >= IRIX_GUEST_MAX_FUNCTIONS)
        return -1;
    entry = &rt->functions[rt->function_count++];
    entry->address = address;
    entry->function = function;
    entry->name = name;
    return 0;
}

IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    size_t i;
    for (i = 0; i < rt->function_count; ++i)
        if (rt->functions[i].address == address)
            return &rt->functions[i];
    return 0;
}

void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code)
{
    rt->trapped = 1;
    rt->trap_pc = cpu->pc;
    rt->trap_code = code;
}

static int try_trivial_stripped_ctor(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                                     IrixGuestAddr address)
{
    IrixGuestAddr object;
    uint32_t w0, w1, w2, w3, w4;

    if (!address || !irix_guest_ptr(rt, address, 20u))
        return 0;

    w0 = irix_guest_read32be(rt, address + 0u);
    w1 = irix_guest_read32be(rt, address + 4u);
    w2 = irix_guest_read32be(rt, address + 8u);
    w3 = irix_guest_read32be(rt, address + 12u);
    w4 = irix_guest_read32be(rt, address + 16u);

    if (w0 != 0x00801025u || w1 != 0xac800008u ||
        w2 != 0xac800004u || w3 != 0x03e00008u ||
        w4 != 0xac800000u)
        return 0;

    object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    if (!irix_guest_ptr(rt, object, 12u))
        return 0;

    irix_guest_write32be(rt, object + 8u, 0u);
    irix_guest_write32be(rt, object + 4u, 0u);
    irix_guest_write32be(rt, object + 0u, 0u);
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)object;
    cpu->gpr[0] = 0;
    return 1;
}

static void report_nearest_functions(IrixGuestRuntime *rt, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *below = 0;
    IrixGuestFunctionEntry *above = 0;
    size_t i;

    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *e = &rt->functions[i];
        if (e->address < address && (!below || e->address > below->address))
            below = e;
        if (e->address > address && (!above || e->address < above->address))
            above = e;
    }

    if (below)
        fprintf(stderr, " below=0x%08x:%s(+0x%x)",
                below->address, below->name ? below->name : "?",
                (uint32_t)(address - below->address));
    if (above)
        fprintf(stderr, " above=0x%08x:%s(-0x%x)",
                above->address, above->name ? above->name : "?",
                (uint32_t)(above->address - address));
}

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address)
{
    IrixGuestFunctionEntry *entry = irix_guest_find_function(rt, address);
    if (!entry) {
        IrixGuestAddr s4 = (IrixGuestAddr)(uint32_t)cpu->gpr[20];

        if (try_trivial_stripped_ctor(rt, cpu, address))
            return rt->trapped ? -1 : 0;

        fprintf(stderr,
                "INVALID GUEST CALL target=0x%08x caller=0x%08x s4=0x%08x gp=%08x t9=%08x sp=%08x ra=%08x",
                address, (uint32_t)cpu->pc, s4,
                (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[25],
                (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[31]);
        fprintf(stderr,
                " a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
        if (address && irix_guest_ptr(rt, address, 20u)) {
            fprintf(stderr,
                    " target_words=%08x,%08x,%08x,%08x,%08x",
                    irix_guest_read32be(rt, address + 0u),
                    irix_guest_read32be(rt, address + 4u),
                    irix_guest_read32be(rt, address + 8u),
                    irix_guest_read32be(rt, address + 12u),
                    irix_guest_read32be(rt, address + 16u));
        }
        report_nearest_functions(rt, address);
        if (s4 && irix_guest_ptr(rt, s4, 28u)) {
            fprintf(stderr,
                    " words=%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                    irix_guest_read32be(rt, s4 + 0u),
                    irix_guest_read32be(rt, s4 + 4u),
                    irix_guest_read32be(rt, s4 + 8u),
                    irix_guest_read32be(rt, s4 + 12u),
                    irix_guest_read32be(rt, s4 + 16u),
                    irix_guest_read32be(rt, s4 + 20u),
                    irix_guest_read32be(rt, s4 + 24u));
        }
        fputc('\n', stderr);
        rt->trapped = 1;
        rt->trap_pc = address;
        rt->trap_code = 0xeeee0003u;
        return -1;
    }
    {
        uint64_t caller_gp = cpu->gpr[28];
        cpu->pc = address;
        rt->current_pc = address;
        /* N32 PIC entry convention: t9 contains the callee address. */
        cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)address;
        entry->function(rt, cpu);
        /* $gp is callee-saved in the N32 ABI. Preserve the caller's value
           across all translated/native guest calls, including bridge entries
           that do not execute a guest epilogue themselves. */
        cpu->gpr[28] = caller_gp;
    }
    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}
