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
#include <signal.h>
#include "IrixGuest.h"
#include "nm_rt.h"

typedef struct { IrixGuestAddr addr; IrixGuestFunction fn; void *native; const char *name; } NmEntry;
static NmEntry *g_ent; static size_t g_n, g_cap;
static IrixGuestRuntime g_rt;
static uint32_t g_gp;
static uint32_t g_sp;
static void *g_pending_native;
typedef struct { uint32_t from, to; } NmPatch;
static NmPatch *g_patch; static size_t g_np, g_npcap;
static void nm_patch_add(uint32_t from, uint32_t to)
{
    if (g_np == g_npcap) { g_npcap = g_npcap ? g_npcap * 2 : 512; g_patch = realloc(g_patch, g_npcap * sizeof *g_patch); }
    g_patch[g_np++] = (NmPatch){ from, to };
}
static int nm_pcmp(const void *x, const void *y)
{ uint32_t a = ((const NmPatch *)x)->from, b = ((const NmPatch *)y)->from; return a < b ? -1 : a > b; }
static void nm_patch_region(uint32_t base, size_t size)
{
    uint32_t *w = NM_PTR(base);
    size_t i, n = size / 4;
    for (i = 0; i < n; i++) {
        size_t lo = 0, hi = g_np;
        uint32_t v = w[i];
        if (v < g_patch[0].from || v > g_patch[g_np - 1].from) continue;
        while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (g_patch[mid].from < v) lo = mid + 1; else hi = mid; }
        if (lo < g_np && g_patch[lo].from == v) w[i] = g_patch[lo].to;
    }
}
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
    if (fn == nm_import_marker) { nm_add(a, NULL, g_pending_native, name); nm_patch_add(a, (uint32_t)(uintptr_t)g_pending_native); }
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

typedef uint32_t (*nm_fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

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
        /* N32 stack arguments: 8-byte slots from sp+0, an int in the low half (+4) */
        const uint32_t sp = (uint32_t)cpu->gpr[29];
#define NM_SARG(k) NM_LD(uint32_t, sp + 8u * (k) + 4u)
        uint32_t lo = ((nm_fn8)native)((uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                                       (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
                                       (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                                       NM_SARG(0), NM_SARG(1), NM_SARG(2), NM_SARG(3),
                                       NM_SARG(4), NM_SARG(5), NM_SARG(6), NM_SARG(7));
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
    for (i = 8; i < 16; i++) NM_ST(uint32_t, g_sp + 8u * (i - 8) + 4u, args[i]);  /* stack args */
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

static void nm_segv(int sig, siginfo_t *si, void *uc)
{
    IrixGuestFunctionEntry *fe = irix_guest_find_function(&g_rt, g_rt.current_pc);
    (void)uc;
    fprintf(stderr, "nativemem: signal %d fault address %p, guest pc 0x%08x%s%s\n", sig, si->si_addr,
            g_rt.current_pc, fe && fe->name ? " in " : "", fe && fe->name ? fe->name : "");
    signal(sig, SIG_DFL);
    raise(sig);
}

void nm_init(const NmImage *img)
{
    size_t i, stub_bytes;
    uint8_t *stubs, *s;
    {   struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = nm_segv; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    }
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
    {   /* Xt class records and other native data: rewrite pointer slots */
        static const char *const keep_guest[] = { "errno", "__iob", "__libc_attr", "__us_rsthread_stdio", NULL };
        size_t k;
        for (k = 0; k < img->ndimps; k++) {
            int skip = 0, j;
            void *p;
            for (j = 0; keep_guest[j]; j++) if (!strcmp(keep_guest[j], img->dimps[k].name)) skip = 1;
            if (skip) continue;
            p = dlsym(RTLD_DEFAULT, img->dimps[k].name);
            if (p) nm_patch_add(img->dimps[k].addr, (uint32_t)(uintptr_t)p);
            else fprintf(stderr, "nativemem: unresolved data import %s\n", img->dimps[k].name);
        }
        if (g_np) {
            qsort(g_patch, g_np, sizeof *g_patch, nm_pcmp);
            nm_patch_region(img->main_base, img->main_size);
            nm_patch_region(img->small_base, img->small_size & ~3u);
        }
        for (k = 0; k < img->nislots; k++) {
            void *p = dlsym(RTLD_DEFAULT, img->islots[k].name);
            if (p) NM_ST(uint32_t, img->islots[k].slot, (uint32_t)(uintptr_t)p + (uint32_t)img->islots[k].addend);
        }
    }
    qsort(g_ent, g_n, sizeof *g_ent, nm_cmp);
    stub_bytes = g_n * 32u;
    stubs = mmap(NULL, stub_bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    s = stubs;
    for (i = 0; i < g_n; i++) {
        uint8_t *at;
        int32_t rel;
        if (!g_ent[i].fn || g_ent[i].addr < img->main_base || g_ent[i].addr >= img->main_base + img->main_size) continue;
        /* stub (keeps the i386 ABI 16-byte stack alignment for everything called below):
           lea 4(%esp),%edx; push %ebp; mov %esp,%ebp; and $-16,%esp; sub $8,%esp;
           push %edx; push $i; call nm_enter; mov %ebp,%esp; pop %ebp; ret */
        at = s;
        *s++ = 0x8d; *s++ = 0x54; *s++ = 0x24; *s++ = 0x04;
        *s++ = 0x55;
        *s++ = 0x89; *s++ = 0xe5;
        *s++ = 0x83; *s++ = 0xe4; *s++ = 0xf0;
        *s++ = 0x83; *s++ = 0xec; *s++ = 0x08;
        *s++ = 0x52;
        *s++ = 0x68; memcpy(s, &(uint32_t){ (uint32_t)i }, 4); s += 4;
        *s++ = 0xe8; rel = (int32_t)((uint8_t *)nm_enter - (s + 4)); memcpy(s, &rel, 4); s += 4;
        *s++ = 0x89; *s++ = 0xec;
        *s++ = 0x5d;
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

#ifdef NM_SHADOW
/* Mixed-width access detector: remembers the width of the last guest-code write to
   each byte and reports reads at a different width (endianness hazards). */
static uint8_t *g_shadow[1u << 20];
typedef struct { uint32_t pc, addr; uint8_t rw, ww; } NmHaz;
static NmHaz g_haz[4096]; static unsigned g_nhaz;
static void nm_shadow_report(void)
{
    unsigned i;
    fprintf(stderr, "nativemem: %u distinct mixed-width read sites\n", g_nhaz);
    for (i = 0; i < g_nhaz; i++) {
        IrixGuestFunctionEntry *fe = irix_guest_find_function(&g_rt, g_haz[i].pc);
        fprintf(stderr, "  pc %08x addr %08x read %u last-write %u %s\n", g_haz[i].pc, g_haz[i].addr,
                g_haz[i].rw, g_haz[i].ww, fe && fe->name ? fe->name : "");
    }
}
void nm_shadow(IrixGuestRuntime *rt, IrixGuestAddr a, unsigned w, int is_write)
{
    static int reg;
    unsigned i;
    if (!reg) { reg = 1; atexit(nm_shadow_report); }
    if (a >= 0xf0000000u) return;
    for (i = 0; i < w; i++) {
        uint32_t ad = a + i;
        uint8_t *pg = g_shadow[ad >> 12];
        if (!pg) { if (!is_write) continue; pg = g_shadow[ad >> 12] = calloc(4096, 1); }
        if (is_write) pg[ad & 0xfff] = (uint8_t)w;
        else if (pg[ad & 0xfff] && pg[ad & 0xfff] != w && g_nhaz < 4096) {
            unsigned j;
            for (j = 0; j < g_nhaz; j++) if (g_haz[j].pc == rt->current_pc) break;
            if (j == g_nhaz) g_haz[g_nhaz++] = (NmHaz){ rt->current_pc, a, (uint8_t)w, pg[ad & 0xfff] };
            return;
        }
    }
}
#endif
