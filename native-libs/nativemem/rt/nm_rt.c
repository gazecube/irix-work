/* Native-memory runtime for translated N32 libraries (i386 first).
   - maps the swapped image at its link-time guest addresses
   - patches a jmp stub at every translated function so native code can call
     the guest address directly
   - dispatches guest calls to translated code or native functions (cdecl) */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "IrixGuest.h"
#include "nm_rt.h"

typedef struct { IrixGuestAddr addr; IrixGuestFunction fn; void *native; const char *name; } NmEntry;
static NmEntry *g_ent; static size_t g_n, g_cap;
static IrixGuestRuntime g_rt;
static uint32_t g_gp;
static uint32_t g_sp;
static void *g_pending_native;
static const NmImage *g_img;

static void nm_add(IrixGuestAddr a, IrixGuestFunction fn, void *native, const char *name)
{
    if (g_n == g_cap) { g_cap = g_cap ? g_cap * 2 : 256; g_ent = realloc(g_ent, g_cap * sizeof *g_ent); }
    g_ent[g_n++] = (NmEntry){ a, fn, native, name };
}

static int nm_cmp(const void *x, const void *y)
{ IrixGuestAddr a = ((const NmEntry *)x)->addr, b = ((const NmEntry *)y)->addr; return a < b ? -1 : a > b; }

static NmEntry *nm_find(IrixGuestAddr a)
{
    size_t lo = 0, hi = g_n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_ent[mid].addr < a) lo = mid + 1; else hi = mid;
    }
    return lo < g_n && g_ent[lo].addr == a ? &g_ent[lo] : NULL;
}

IrixGuestFunctionEntry *irix_guest_find_function(IrixGuestRuntime *rt, IrixGuestAddr a)
{
    static IrixGuestFunctionEntry fe;
    NmEntry *e = nm_find(a);
    (void)rt;
    fe.address = a; fe.name = e ? e->name : NULL;
    return &fe;
}

void irix_guest_trap(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t code)
{ rt->trapped = 1; rt->trap_pc = cpu->pc; rt->trap_code = code;
  fprintf(stderr, "nativemem: guest trap 0x%x at pc 0x%08x\n", code, cpu->pc); }
int irix_mips_interp_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr a)
{ (void)a; irix_guest_trap(rt, cpu, 0xeeee0004u); return -1; }

static void nm_import_marker(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; (void)cpu; }

void irix_guest_register_function(IrixGuestRuntime *rt, IrixGuestAddr a, IrixGuestFunction fn, const char *name)
{
    (void)rt;
    if (fn == nm_import_marker) nm_add(a, NULL, g_pending_native, name);
    else nm_add(a, fn, NULL, name);
}
void irix_guest_register_recompiled_function(IrixGuestRuntime *rt, IrixGuestAddr a, IrixGuestFunction fn, const char *name)
{ irix_guest_register_function(rt, a, fn, name); g_ent[g_n - 1].native = NULL; }

IrixGuestFunction nm_resolver(const char *name)
{
    void *p = dlsym(RTLD_DEFAULT, name);
    if (!p) return NULL;
    g_pending_native = p;
    return nm_import_marker;
}

typedef uint32_t (*nm_fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int irix_guest_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr a)
{
    NmEntry *e = nm_find(a);
    void *native = NULL;
    if (e && e->fn) { e->fn(rt, cpu); return rt->trapped ? -1 : 0; }
    if (e) native = e->native; else native = NM_PTR(a);
    if (!native) { fprintf(stderr, "nativemem: unresolved import %s\n", e ? e->name : "?");
                   irix_guest_trap(rt, cpu, 0xeeee1003u); return -1; }
    {
        uint32_t saved = g_sp;
        g_sp = (uint32_t)cpu->gpr[29] - 256u;
        uint64_t r;
        uint32_t lo = ((nm_fn8)native)((uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                                       (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                                       (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
        r = lo;
        g_sp = saved;
        cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)r;
    }
    return 0;
}

/* Entered from the jmp stub of translated function `index`. */
uint64_t nm_enter(uint32_t index, const uint32_t *args)
{
    IrixGuestCpu cpu;
    uint32_t saved = g_sp;
    int i;
    memset(&cpu, 0, sizeof cpu);
    for (i = 0; i < 8; i++) cpu.gpr[4 + i] = (uint64_t)(int64_t)(int32_t)args[i];
    cpu.gpr[28] = g_gp;
    cpu.gpr[25] = g_ent[index].addr;   /* PIC prologue derives $gp from $t9 */
    cpu.gpr[29] = g_sp;
    cpu.pc = g_ent[index].addr;
    g_rt.trapped = 0;
    g_ent[index].fn(&g_rt, &cpu);
    g_sp = saved;
    return ((uint64_t)(uint32_t)cpu.gpr[3] << 32) | (uint32_t)cpu.gpr[2];
}

extern void irix_register_recompiled_stack(IrixGuestRuntime *);
extern void irix_register_recompiled_stack_imports(IrixGuestRuntime *, IrixGuestFunction (*)(const char *));

static void *nm_map(uint32_t addr, size_t size)
{
    void *p = mmap(NM_PTR(addr), size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != NM_PTR(addr)) { fprintf(stderr, "nativemem: cannot map %08x+%zx\n", addr, size); abort(); }
    return p;
}

void nm_init(const NmImage *img)
{
    size_t i, stub_bytes;
    uint8_t *stubs, *s;
    g_img = img;
    g_gp = img->gp;
    nm_map(img->main_base, (img->main_size + 0xfff) & ~0xfffu);
    memcpy(NM_PTR(img->main_base), img->main, img->main_size);
    nm_map(img->small_base, (img->small_size + 0xfff) & ~0xfffu);
    memcpy(NM_PTR(img->small_base), img->small, img->small_size);
    g_sp = (uint32_t)(uintptr_t)mmap(NULL, 1u << 20, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0) + (1u << 20) - 64u;
    irix_register_recompiled_stack(&g_rt);
    irix_register_recompiled_stack_imports(&g_rt, nm_resolver);
    qsort(g_ent, g_n, sizeof *g_ent, nm_cmp);
    stub_bytes = g_n * 32u;
    stubs = mmap(NULL, stub_bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    s = stubs;
    for (i = 0; i < g_n; i++) {
        uint8_t *at;
        int32_t rel;
        if (!g_ent[i].fn) continue;
        /* stub: lea 4(%esp),%edx; push %edx; push $i; call nm_enter; add $8,%esp; ret */
        at = s;
        *s++ = 0x8d; *s++ = 0x54; *s++ = 0x24; *s++ = 0x04;
        *s++ = 0x52;
        *s++ = 0x68; memcpy(s, &(uint32_t){ (uint32_t)i }, 4); s += 4;
        *s++ = 0xe8; rel = (int32_t)((uint8_t *)nm_enter - (s + 4)); memcpy(s, &rel, 4); s += 4;
        *s++ = 0x83; *s++ = 0xc4; *s++ = 0x08;
        *s++ = 0xc3;
        s = (uint8_t *)(((uintptr_t)s + 15u) & ~15u);
        /* patch jmp rel32 at the guest function address */
        {
            uint8_t *fa = NM_PTR(g_ent[i].addr);
            rel = (int32_t)(at - (fa + 5));
            fa[0] = 0xe9; memcpy(fa + 1, &rel, 4);
        }
    }
}
