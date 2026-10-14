#ifndef IID_IRIX_GUEST_H
#define IID_IRIX_GUEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRIX_GUEST_GPR_COUNT 32
#define IRIX_GUEST_MAX_SEGMENTS 256
#define IRIX_GUEST_MAX_FUNCTIONS 131072
#define IRIX_GUEST_FUNCTION_CACHE_SIZE 262144

#define IRIX_GUEST_FUNCTION_NATIVE     0x00000001u
#define IRIX_GUEST_FUNCTION_RECOMPILED 0x00000002u
#define IRIX_GUEST_FUNCTION_INTERIOR   0x00000004u

typedef uint32_t IrixGuestAddr;

typedef struct IrixGuestElfInfo {
    IrixGuestAddr entry;
    IrixGuestAddr phdr;
    uint16_t phent;
    uint16_t phnum;
    IrixGuestAddr gp_value;
    IrixGuestAddr load_base;
    IrixGuestAddr init_entry;
    char interp[256];
} IrixGuestElfInfo;

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
typedef uint32_t (*IrixGuestQuarkifyEncoded)(struct IrixGuestRuntime *rt,
                                             uint32_t encoded);

typedef struct IrixGuestFunctionEntry {
    IrixGuestAddr address;
    uint32_t size;
    IrixGuestFunction function;
    const char *name;
    uint32_t flags;
} IrixGuestFunctionEntry;

typedef struct IrixGuestRuntime {
    IrixGuestMemory memory;
    IrixGuestAddr trap_pc;
    IrixGuestAddr current_pc;
    IrixGuestAddr fault_addr;
    uint32_t trap_code;
    int trapped;
    IrixGuestAddr errno_proxy_addr;
    const char *rootfs;
    IrixGuestAddr program_break;
    uint64_t alarm_deadline_ns;
    IrixGuestAddr compat_alloc_cur;
    IrixGuestQuarkifyEncoded xrm_quarkify_encoded;
    IrixGuestFunctionEntry functions[IRIX_GUEST_MAX_FUNCTIONS];
    IrixGuestFunctionEntry *function_cache[IRIX_GUEST_FUNCTION_CACHE_SIZE];
    size_t function_count;
    void *profile_state;
    void *jit_state;
} IrixGuestRuntime;

/*
 * Resolve an absolute IRIX path to the native (x86) file for it.  Under an
 * ordinary reconstructed root that is root+path.  When the root is the real
 * Linux "/" the host owns most of those names (/sbin/sh, /usr/lib32/libXt.so,
 * ...), so a file only counts if /usr/lib/irix-recomp.manifest lists it;
 * anything else is looked up in IRIX_RECOMP_FALLBACK_ROOT (the build tree).
 * Returns 0 and fills out when the file exists, -1 otherwise.
 */
int irix_recomp_resolve(const char *root, const char *path, char *out, size_t n);

int irix_guest_execve(IrixGuestRuntime *rt, const char *guest_path,
                       char *const argv[], char *const envp[]);

void irix_guest_runtime_init(IrixGuestRuntime *rt);
void irix_guest_runtime_destroy(IrixGuestRuntime *rt);
int irix_guest_map(IrixGuestRuntime *rt, IrixGuestAddr base, uint32_t size, unsigned flags);
void *irix_guest_ptr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t size);
int64_t irix_guest_read_fd(IrixGuestRuntime *rt, int fd,
                           IrixGuestAddr addr, size_t count);
int64_t irix_guest_write_fd(IrixGuestRuntime *rt, int fd,
                            IrixGuestAddr addr, size_t count);
const char *irix_guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr, size_t max_len);

uint8_t irix_guest_read8(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint16_t irix_guest_read16be(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint32_t irix_guest_read32be(IrixGuestRuntime *rt, IrixGuestAddr addr);
uint64_t irix_guest_read64be(IrixGuestRuntime *rt, IrixGuestAddr addr);
void irix_guest_write8(IrixGuestRuntime *rt, IrixGuestAddr addr, uint8_t value);
void irix_guest_write16be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint16_t value);
void irix_guest_write32be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value);
void irix_guest_write64be(IrixGuestRuntime *rt, IrixGuestAddr addr, uint64_t value);
void irix_guest_set_errno_proxy(IrixGuestRuntime *rt, IrixGuestAddr addr);
void irix_guest_set_rootfs(IrixGuestRuntime *rt, const char *rootfs);
const char *irix_guest_resolve_path(IrixGuestRuntime *rt, const char *guest_path,
                                    char *buffer, size_t buffer_size,
                                    const char *operation, int optional);
float irix_guest_fpr_get_s(const IrixGuestCpu *cpu, unsigned reg);
double irix_guest_fpr_get_d(const IrixGuestCpu *cpu, unsigned reg);
void irix_guest_fpr_set_s(IrixGuestCpu *cpu, unsigned reg, float value);
void irix_guest_fpr_set_d(IrixGuestCpu *cpu, unsigned reg, double value);

int irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr address,
                                 IrixGuestFunction function, const char *name);
int irix_guest_register_function_range(IrixGuestRuntime *rt, IrixGuestAddr address,
                                       uint32_t size, IrixGuestFunction function,
                                       const char *name);
int irix_guest_register_recompiled_function(IrixGuestRuntime *rt,
                                            IrixGuestAddr address,
                                            IrixGuestFunction function,
                                            const char *name);
int irix_guest_register_recompiled_function_range(IrixGuestRuntime *rt,
                                                  IrixGuestAddr address,
                                                  uint32_t size,
                                                  IrixGuestFunction function,
                                                  const char *name);
IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt,
                                                  IrixGuestAddr address);
int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr address);
int irix_guest_call_entry(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address, IrixGuestFunctionEntry *entry);
void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code);
void irix_guest_syscall(IrixGuestRuntime *rt, IrixGuestCpu *cpu);
int irix_guest_poll_alarm(IrixGuestRuntime *rt, IrixGuestCpu *cpu);
void irix_guest_xt_repair_class_resources(IrixGuestRuntime *rt, IrixGuestAddr wc);
IrixGuestAddr irix_guest_xt_ensure_resource_table(IrixGuestRuntime *rt, IrixGuestAddr table, uint32_t num_resources);
int irix_guest_xt_normalize_resource(IrixGuestRuntime *rt, IrixGuestAddr res);
int irix_guest_load_elf32be_info(IrixGuestRuntime *rt, const char *path,
                                  IrixGuestElfInfo *info_out);
int irix_guest_load_elf32be(IrixGuestRuntime *rt, const char *path,
                             IrixGuestAddr *entry_out);

#ifdef __cplusplus
}
#endif

#endif
