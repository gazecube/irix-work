/* Native-memory build of the translated-code runtime surface: guest addresses
   are host addresses and memory is native-endian. Shadows the big-endian
   runtime header of the same name for generated code compiled with -Iinclude. */
#ifndef NM_IRIXGUEST_H
#define NM_IRIXGUEST_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t IrixGuestAddr;
#define IRIX_GUEST_GPR_COUNT 32

typedef struct IrixGuestCpu {
    uint64_t gpr[IRIX_GUEST_GPR_COUNT];
    uint64_t hi;
    uint64_t lo;
    uint64_t fpr[32];
    uint32_t fcr31;
    IrixGuestAddr pc;
} IrixGuestCpu;

typedef struct IrixGuestRuntime {
    int trapped;
    IrixGuestAddr current_pc;
    IrixGuestAddr trap_pc;
    uint32_t trap_code;
} IrixGuestRuntime;

typedef void (*IrixGuestFunction)(IrixGuestRuntime *rt, IrixGuestCpu *cpu);

#define NM_PTR(a) ((void *)(uintptr_t)(IrixGuestAddr)(a))
#define NM_LD(T, a) ({ T v_; memcpy(&v_, NM_PTR(a), sizeof v_); v_; })
#define NM_ST(T, a, x) do { T v_ = (T)(x); memcpy(NM_PTR(a), &v_, sizeof v_); } while (0)

static inline uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr a)
{ (void)rt; return NM_LD(uint8_t, a); }
static inline uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr a)
{ (void)rt; return NM_LD(uint16_t, a); }
static inline uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr a)
{ (void)rt; return NM_LD(uint32_t, a); }
static inline uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr a)
{ (void)rt; return NM_LD(uint64_t, a); }
static inline void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr a, uint8_t v)
{ (void)rt; NM_ST(uint8_t, a, v); }
static inline void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr a, uint16_t v)
{ (void)rt; NM_ST(uint16_t, a, v); }
static inline void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr a, uint32_t v)
{ (void)rt; NM_ST(uint32_t, a, v); }
static inline void irix_guest_write64be(IrixGuestRuntime *rt, IrixGuestAddr a, uint64_t v)
{ (void)rt; NM_ST(uint64_t, a, v); }

static inline float irix_guest_fpr_get_s(const IrixGuestCpu *cpu, unsigned reg)
{ union { uint32_t u; float f; } v; v.u = (uint32_t)cpu->fpr[reg & 31u]; return v.f; }
static inline double irix_guest_fpr_get_d(const IrixGuestCpu *cpu, unsigned reg)
{ union { uint64_t u; double d; } v; v.u = cpu->fpr[reg & 31u]; return v.d; }
static inline void irix_guest_fpr_set_s(IrixGuestCpu *cpu, unsigned reg, float value)
{ union { uint32_t u; float f; } v; v.f = value;
  cpu->fpr[reg & 31u] = (cpu->fpr[reg & 31u] & UINT64_C(0xffffffff00000000)) | v.u; }
static inline void irix_guest_fpr_set_d(IrixGuestCpu *cpu, unsigned reg, double value)
{ union { uint64_t u; double d; } v; v.d = value; cpu->fpr[reg & 31u] = v.u; }

typedef struct IrixGuestFunctionEntry { IrixGuestAddr address; const char *name; } IrixGuestFunctionEntry;
IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr address);
int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address);
void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code);
int irix_mips_interp_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address);
void irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                  IrixGuestFunction function, const char *name);
void irix_guest_register_recompiled_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                             IrixGuestFunction function, const char *name);
#endif
