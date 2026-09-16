#ifndef IID_IRIX_GUEST_H
#define IID_IRIX_GUEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRIX_GUEST_GPR_COUNT 32
#define IRIX_GUEST_MAX_SEGMENTS 64
#define IRIX_GUEST_MAX_FUNCTIONS 32768

typedef uint32_t IrixGuestAddr;

typedef struct IrixGuestCpu {
    uint64_t gpr[IRIX_GUEST_GPR_COUNT];
    uint64_t hi;
    uint64_t lo;
    uint64_t fpr[32];
    uint32_t fcr31;
    IrixGuestAddr pc;
} IrixGuestCpu;

typedef struct IrixGuestSegment {
    IrixGuestAddr guest_base;
    uint32_t size;
    uint8_t *host_base;
    unsigned flags;
} IrixGuestSegment;

typedef struct IrixGuestMemory {
    IrixGuestSegment segments[IRIX_GUEST_MAX_SEGMENTS];
    size_t count;
} IrixGuestMemory;

struct IrixGuestRuntime;
typedef void (*IrixGuestFunction)(struct IrixGuestRuntime *rt, IrixGuestCpu *cpu);

typedef struct IrixGuestFunctionEntry {
    IrixGuestAddr address;
    IrixGuestFunction function;
    const char *name;
} IrixGuestFunctionEntry;

typedef struct IrixGuestRuntime {
    IrixGuestMemory memory;
    IrixGuestAddr trap_pc;
    uint32_t trap_code;
    int trapped;
    IrixGuestFunctionEntry functions[IRIX_GUEST_MAX_FUNCTIONS];
    size_t function_count;
} IrixGuestRuntime;

void irix_guest_runtime_init(IrixGuestRuntime *rt);
void irix_guest_runtime_destroy(IrixGuestRuntime *rt);
int irix_guest_map(IrixGuestRuntime *rt, IrixGuestAddr base, uint32_t size, unsigned flags);
void *irix_guest_ptr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size);

uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr addr);
void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value);
void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value);
void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value);
void irix_guest_write64be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint64_t value);
float irix_guest_fpr_get_s(const IrixGuestCpu *cpu, unsigned reg);
double irix_guest_fpr_get_d(const IrixGuestCpu *cpu, unsigned reg);
void irix_guest_fpr_set_s(IrixGuestCpu *cpu, unsigned reg, float value);
void irix_guest_fpr_set_d(IrixGuestCpu *cpu, unsigned reg, double value);

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name);
IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt,
                                                  IrixGuestAddr address);
int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address);
void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code);
int irix_guest_load_elf32be(IrixGuestRuntime *rt, const char *path,
                             IrixGuestAddr *entry_out);

#ifdef __cplusplus
}
#endif

#endif
