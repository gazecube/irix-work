#include "IrixGuest.h"
#include "IrixMipsJit.h"
#include "IrixLibcBridge.h"
#include "IrixPosixBridge.h"
#include "IrixCLibExtraBridge.h"
#include "IrixCxxBridge.h"
#include "IrixRuntimeLibcBridge.h"
#include "IrixFsBridge.h"
#include "IrixStdioBridge.h"
#include "IrixX11Bridge.h"
#include "IrixX11KeyboardBridge.h"
#include "IrixRuntimeX11Bridge.h"
#include "IrixRuntimeXtBridge.h"
#include "IrixX11WindowBridge.h"
#include "IrixX11WmBridge.h"
#include "IrixX11ContextBridge.h"
#include "IrixXrmCompat.h"
#include "IrixXrmQuarkBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

extern char **environ;

/* Runtime-private GP ownership registration. Kept out of IrixGuest.h so
   generated translation objects do not rebuild when this loader plumbing
   changes. */
void irix_guest_register_gp_range(IrixGuestAddr base, uint32_t size,
                                  IrixGuestAddr gp);

#ifdef IRIX_HAVE_FAST_QUARKS
#include "irix_fast_quarks.inc"
#endif

#ifndef IRIX_RUNTIME_TREE
#define IRIX_RUNTIME_TREE ""
#endif
#ifndef IRIX_DEV_TREE
#define IRIX_DEV_TREE ""
#endif
#ifndef IRIX_INTERNAL_TREE
#define IRIX_INTERNAL_TREE ""
#endif
#ifndef IRIX_CORE_IMAGE_DIR
#define IRIX_CORE_IMAGE_DIR ""
#endif
#ifndef IRIX_APP_NAME
#define IRIX_APP_NAME "desktop"
#endif
#ifndef IRIX_APP_PATH
#define IRIX_APP_PATH "usr/sbin/desktop"
#endif
#ifndef IRIX_RECOMP_ROOT
#define IRIX_RECOMP_ROOT ""
#endif
void irix_register_recompiled_stack(IrixGuestRuntime *rt);
void irix_register_recompiled_stack_imports(IrixGuestRuntime *rt, IrixGuestFunction (*resolver)(const char *));
typedef struct IrixAppGuestComponent {
    int kind; /* 0 = runtime tree, 1 = internal tree, 2 = development tree */
    int role; /* 0 = ordinary, 1 = PT_INTERP, 2 = rld */
    const char *path;
} IrixAppGuestComponent;

extern const IrixAppGuestComponent irix_app_guest_components[];
extern const size_t irix_app_guest_component_count;
void irix_register_app_bundle(IrixGuestRuntime *rt);
void register_irix_bridge_imports(IrixGuestRuntime *rt, IrixGuestFunction (*resolver)(const char *));
void register_irix_prebound_imports(IrixGuestRuntime *rt, IrixGuestAddr (*data_resolver)(const char *), int prefer_dso_data);
void irix_x11_extra_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_extra_lookup(const char *name);
void irix_x11_connection_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_connection_bridge_lookup(const char *name);
void irix_x11_bulk_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_bulk_lookup(const char *name);
void irix_x11_image_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_image_bridge_lookup(const char *name);

#ifndef IRIX_JIT_ONLY
static IrixGuestFunction resolve_native(const char *name)
{
    IrixGuestFunction fn = irix_libc_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_posix_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_fs_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_stdio_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_clib_extra_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_cxx_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_runtime_libc_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_xrm_quark_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_xrm_compat_lookup(name);
    if (fn) return fn;
    fn = irix_x11_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_extra_lookup(name);
    if (fn) return fn;
    fn = irix_x11_keyboard_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_connection_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_context_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_window_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_image_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_x11_wm_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_runtime_x11_bridge_lookup(name);
    if (fn) return fn;
    fn = irix_runtime_xt_bridge_lookup(name);
    if (fn) return fn;
    return irix_x11_bulk_lookup(name);
}

static size_t rebind_named_native_functions(IrixGuestRuntime *rt)
{
    size_t i, rebound = 0;

    /*
     * A translated DSO can contain the real guest implementation of a symbol
     * that the compatibility runtime deliberately implements natively.  rld
     * is then free to bind an application veneer directly to that guest
     * address.  Since dispatch is address-based, merely registering the
     * name-based bridge import does not help: execution enters the translated
     * provider instead.
     *
     * Once every translated provider has registered, replace each named
     * function for which resolve_native() has an implementation. Generated
     * import registrations are owner-qualified as "component:symbol", so
     * strip that diagnostic owner prefix before asking the native resolver.
     * This makes
     * native ABI bridges authoritative regardless of whether a caller reaches
     * them through a generated import forwarder or through rld's resolved
     * guest address.
     */
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        IrixGuestFunction native;

        const char *name;

        /*
         * Some SGI DSOs have stripped/private function records even when the
         * routine is a public Xlib entry point.  The IRIX libX11 used by xwsh
         * has XkbSelectEventDetails at 0x0f715fb4 without a surviving name,
         * so ordinary name-based rebinding cannot make the host-Xlib bridge
         * authoritative.  This address was identified statically from the
         * exact SGI libX11 instruction sequence around the faulting request
         * builder.  Keep this as an exact-address alias, not a range heuristic.
         */
        if ((!entry->name || !strcmp(entry->name, "?")) &&
            entry->address == 0x0f715fb4u) {
            native = resolve_native("XkbSelectEventDetails");
            if (native && entry->function != native) {
                entry->function = native;
                entry->name = "libX11:XkbSelectEventDetails";
                ++rebound;
            }
            continue;
        }

        /*
         * Exact SGI libX11 OpenDis.c internal XKB initializer.  Static
         * matching against the 6.5.5 debug libX11 identifies the faulting
         * sequence as the helper which writes Display->xkb_info.  The runtime
         * copy is recovered anonymously at 0x0f706bdc; replace the whole
         * helper because the guest Display shadow deliberately has no private
         * Xlib connection/XKB pointers.
         */
        /*
         * This recovered helper does have a synthetic recovery name in some
         * generated bundles, so do not gate an exact-address identity on the
         * name being NULL/"?".  The address match is the authority here.
         */
        if (entry->address == 0x0f706bdcu) {
            native = resolve_native("_IrixXkbDisplayInit");
            if (native && entry->function != native) {
                entry->function = native;
                entry->name = "libX11:_IrixXkbDisplayInit";
                ++rebound;
            }
            continue;
        }

        /*
         * xwsh's stripped executable retains XConfigureWindow as an import
         * veneer at 0x10020290.  Bind that exact veneer to the host-Xlib
         * bridge so it cannot fall back into SGI Xlib request construction.
         */
        if (entry->address == 0x10020290u) {
            native = resolve_native("XConfigureWindow");
            if (native && entry->function != native) {
                entry->function = native;
                entry->name = "xwsh:XConfigureWindow";
                ++rebound;
            }
            continue;
        }

        /*
         * libXt's own XConfigureWindow import veneer is a separate recovered
         * guest function.  Xt geometry management calls this provider
         * directly, so pin it to the same host-Xlib bridge as the executable
         * veneer.  Address is from the exact IRIX libXt symbol table.
         */
        if (entry->address == 0x0f60e38cu) {
            native = resolve_native("XConfigureWindow");
            fprintf(stderr,
                    "IRIX exact XConfigureWindow rebind: addr=0x%08x name=%s native=%p old=%p\\n",
                    entry->address, entry->name ? entry->name : "<null>",
                    (void *)native, (void *)entry->function);
            if (native && entry->function != native) {
                entry->function = native;
                entry->name = "libXt:XConfigureWindow";
                ++rebound;
            }
            continue;
        }

        if (!entry->name)
            continue;
        name = strrchr(entry->name, ':');
        name = name ? name + 1 : entry->name;
        native = resolve_native(name);
        if (!native || entry->function == native)
            continue;
        entry->function = native;
        ++rebound;
    }
    return rebound;
}

#endif

#ifndef IRIX_JIT_ONLY
typedef void (*IrixDsoRegister)(IrixGuestRuntime *);

static void register_native_dsos(IrixGuestRuntime *rt)
{
    size_t i;
    for (i = 0; i < irix_app_guest_component_count; ++i) {
        const IrixAppGuestComponent *component = &irix_app_guest_components[i];
        char path[4096];
        char symbol[512];
        const char *base, *p;
        void *handle;
        IrixDsoRegister reg;

        if (component->role != 0 || i == 0u)
            continue;
        base = strrchr(component->path, '/');
        base = base ? base + 1 : component->path;
        /* Native translated DSOs live at their original IRIX paths inside
           the shared reconstructed root, not in an application-private lib
           directory. */
        snprintf(path, sizeof(path), "%s/%s", IRIX_RECOMP_ROOT, component->path);
        handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            fprintf(stderr, "failed to load translated DSO %s: %s\n",
                    path, dlerror());
            exit(1);
        }

        /* Auto dependency rows use dso_<sanitized SONAME>; manifest shared
           components retain their explicit prefixes. Prefer a registry symbol
           exported by this DSO rather than guessing from the filename. */
        reg = 0;
        for (p = base; *p; ++p) (void)p;
        snprintf(symbol, sizeof(symbol), "register_dso_%s", base);
        for (p = symbol; *p; ++p)
            if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                  (*p >= '0' && *p <= '9') || *p == '_'))
                *(char *)p = '_';
        reg = (IrixDsoRegister)dlsym(handle, symbol);
        if (!reg) {
            /* Explicit IID components use the basename without lib/.so. */
            size_t n = strlen(base);
            const char *name = base;
            if (strncmp(name, "lib", 3) == 0) name += 3;
            snprintf(symbol, sizeof(symbol), "register_%s", name);
            {
                char *dot = strstr(symbol, ".so");
                if (dot) *dot = '\0';
            }
            reg = (IrixDsoRegister)dlsym(handle, symbol);
        }
        if (!reg) {
            fprintf(stderr, "translated DSO %s has no registry entry\n", path);
            exit(1);
        }
        reg(rt);
    }
}

#endif

#ifndef IRIX_JIT_ONLY
static int load_blob(IrixGuestRuntime *rt, const char *path, IrixGuestAddr base)
{
    FILE *f = fopen(path, "rb");
    long n;
    void *dst;
    if (!f) { perror(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f); return -1;
    }
    if ((unsigned long)n > 0xfffffffful || irix_guest_map(rt, base, (uint32_t)n, 7u) != 0) {
        fclose(f); return -1;
    }
    dst = irix_guest_ptr(rt, base, (size_t)n);
    if (!dst || fread(dst, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int load_core_image(IrixGuestRuntime *rt)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/main.bin", IRIX_CORE_IMAGE_DIR);
    if (load_blob(rt, path, 0x50000000u) != 0) return -1;
    snprintf(path, sizeof(path), "%s/small.bin", IRIX_CORE_IMAGE_DIR);
    if (load_blob(rt, path, 0x60000000u) != 0) return -1;

    /*
     * link_stack_n32 relocates the complete archive core against one shared
     * N32 small-data/GOT window:
     *
     *   main image  = 0x50000000...
     *   small image = 0x60000000...
     *   $gp         = 0x60008000
     *
     * Give synthetic-core callable addresses explicit GP ownership just like
     * mapped DSOs. These intentionally broad reservations match the linker
     * layout windows and cannot overlap the original IRIX DSO address space.
     */
    irix_guest_register_gp_range(0x50000000u, 0x10000000u, 0x60008000u);
    irix_guest_register_gp_range(0x60000000u, 0x00010000u, 0x60008000u);
    return 0;
}

#endif

static int load_elf(IrixGuestRuntime *rt, const char *root, const char *rel,
                    IrixGuestElfInfo *info_out)
{
    char path[4096];
    char alias[4096];
    const char *selected = path;
    IrixGuestElfInfo info;
    size_t segment_count_before;

    snprintf(path, sizeof(path), "%s/%s", root, rel);

    /*
     * IRIX media/runtime trees are seen in both layouts:
     *
     *   /usr/lib32/libc.so.1
     *   /lib32/libc.so.1
     *
     * PT_INTERP records the guest pathname, but extracted trees may flatten
     * the /usr/lib32 compatibility location into /lib32 (or vice versa).
     * Resolve that filesystem alias before loading so the guest-visible ELF
     * ABI/path remains untouched.
     */
    if (access(path, R_OK) != 0) {
        if (!strncmp(rel, "usr/lib32/", 10)) {
            snprintf(alias, sizeof(alias), "%s/lib32/%s", root, rel + 10);
            if (access(alias, R_OK) == 0)
                selected = alias;
        } else if (!strncmp(rel, "lib32/", 6)) {
            snprintf(alias, sizeof(alias), "%s/usr/lib32/%s", root, rel + 6);
            if (access(alias, R_OK) == 0)
                selected = alias;
        }
    }

    if (selected != path)
        fprintf(stderr, "IRIX ELF path alias: /%s -> %s\n", rel, selected);

    segment_count_before = rt->memory.count;
    if (irix_guest_load_elf32be_info(rt, selected, &info) != 0) {
        fprintf(stderr, "failed to load guest ELF: %s\n", selected);
        return -1;
    }
    if (info.gp_value) {
        size_t i;
        for (i = segment_count_before; i < rt->memory.count; ++i) {
            IrixGuestSegment *seg = &rt->memory.segments[i];
            irix_guest_register_gp_range(seg->guest_base, seg->size,
                                         info.gp_value);
        }
    }
    if (info_out)
        *info_out = info;
    return 0;
}

static IrixGuestAddr find_named(IrixGuestRuntime *rt, const char *name)
{
    size_t i;
    for (i = 0; i < rt->function_count; ++i)
        if (rt->functions[i].name && strcmp(rt->functions[i].name, name) == 0)
            return rt->functions[i].address;
    return 0;
}

static void report_guest_words(IrixGuestRuntime *rt, const char *label, IrixGuestAddr addr)
{
    unsigned i;
    if (!addr || !irix_guest_ptr(rt, addr, 4u))
        return;
    fprintf(stderr, "  mem %s@0x%08x:", label, addr);
    for (i = 0; i < 6u; ++i) {
        IrixGuestAddr p = addr + i * 4u;
        if (!irix_guest_ptr(rt, p, 4u))
            break;
        fprintf(stderr, " +%02x=%08x", i * 4u, irix_guest_read32be(rt, p));
    }
    fputc('\n', stderr);
}

static void report_guest_bytes_ascii(IrixGuestRuntime *rt, const char *label,
                                     IrixGuestAddr addr, unsigned before,
                                     unsigned after)
{
    IrixGuestAddr start;
    unsigned total, off;

    if (!addr)
        return;
    start = addr >= before ? addr - before : 0u;
    total = before + after;

    fprintf(stderr, "  bytes %s around 0x%08x (-0x%x..+0x%x):\n",
            label, addr, before, after);
    for (off = 0; off < total; off += 16u) {
        unsigned i, n = total - off;
        if (n > 16u) n = 16u;
        fprintf(stderr, "    %08x  ", start + off);
        for (i = 0; i < 16u; ++i) {
            IrixGuestAddr p = start + off + i;
            if (i < n && irix_guest_ptr(rt, p, 1u)) {
                unsigned char ch = *(const unsigned char *)irix_guest_ptr(rt, p, 1u);
                fprintf(stderr, "%02x ", (unsigned)ch);
            } else {
                fputs("?? ", stderr);
            }
        }
        fputs(" |", stderr);
        for (i = 0; i < n; ++i) {
            IrixGuestAddr p = start + off + i;
            if (irix_guest_ptr(rt, p, 1u)) {
                unsigned char ch = *(const unsigned char *)irix_guest_ptr(rt, p, 1u);
                fputc(ch >= 0x20u && ch <= 0x7eu ? (int)ch : '.', stderr);
            } else {
                fputc('?', stderr);
            }
        }
        fputs("|\n", stderr);
    }
}

static void report_trap_context(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestFunctionEntry *below = 0, *above = 0, *ra_fn = 0;
    IrixGuestAddr pc = rt->trap_pc;
    IrixGuestAddr ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
    size_t i;

    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *e = &rt->functions[i];
        if (e->address <= pc && (!below || e->address > below->address)) below = e;
        if (e->address > pc && (!above || e->address < above->address)) above = e;
        if (e->address <= ra && (!ra_fn || e->address > ra_fn->address)) ra_fn = e;
    }

    fprintf(stderr,
            "  near: below=0x%08x:%s(+0x%x) above=0x%08x:%s(-0x%x) ra=0x%08x:%s(+0x%x)\n",
            below ? below->address : 0u, below && below->name ? below->name : "?",
            below ? (uint32_t)(pc - below->address) : 0u,
            above ? above->address : 0u, above && above->name ? above->name : "?",
            above ? (uint32_t)(above->address - pc) : 0u,
            ra, ra_fn && ra_fn->name ? ra_fn->name : "?",
            ra_fn ? (uint32_t)(ra - ra_fn->address) : 0u);

    if (pc >= 4u && irix_guest_ptr(rt, pc - 4u, 16u)) {
        fprintf(stderr,
                "  insn: [%08x]=%08x [%08x]=%08x [%08x]=%08x [%08x]=%08x\n",
                pc - 4u, irix_guest_read32be(rt, pc - 4u),
                pc, irix_guest_read32be(rt, pc),
                pc + 4u, irix_guest_read32be(rt, pc + 4u),
                pc + 8u, irix_guest_read32be(rt, pc + 8u));
    }
    fprintf(stderr,
            "  regs: v0=%08x sp=%08x gp=%08x ra=%08x t9=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
            (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[29],
            (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31],
            (uint32_t)cpu->gpr[25],
            (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
            (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
            (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
            (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
    if (ra >= 40u && irix_guest_ptr(rt, ra - 40u, 52u)) {
        IrixGuestAddr q;
        fprintf(stderr, "  caller-insn:");
        for (q = ra - 40u; q <= ra + 8u; q += 4u)
            fprintf(stderr, " [%08x]=%08x", q, irix_guest_read32be(rt, q));
        fputc('\n', stderr);
        fprintf(stderr,
                "  caller-regs: at=%08x t9=%08x s0=%08x s1=%08x s2=%08x s3=%08x gp=%08x\n",
                (uint32_t)cpu->gpr[1], (uint32_t)cpu->gpr[25],
                (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                (uint32_t)cpu->gpr[28]);
    }
    report_guest_words(rt, "v0", (IrixGuestAddr)(uint32_t)cpu->gpr[2]);
    report_guest_words(rt, "a0", (IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    report_guest_words(rt, "a1", (IrixGuestAddr)(uint32_t)cpu->gpr[5]);
    report_guest_words(rt, "a3", (IrixGuestAddr)(uint32_t)cpu->gpr[7]);

    /*
     * rld run_init_code() dependency walk diagnostic.
     *
     * The debug rld identifies 0x0fb69b84..0x0fb69c5c as run_init_code().
     * At the dependency loop, s0 is the current rld OBJECT.  Its observed
     * fields are:
     *   +0x58  dynamic string table
     *   +0x6c  Elf32_Lib (.liblist) array
     *   +0xa8  .liblist entry count
     * and the loop keeps i and i*20 in the current stack frame.
     *
     * Keep this deliberately scoped to traps inside run_init_code rather than
     * turning on broad rld tracing.
     */
    if (pc >= 0x0fb69b84u && pc <= 0x0fb69c5cu) {
        IrixGuestAddr obj = (IrixGuestAddr)(uint32_t)cpu->gpr[16]; /* s0 */
        IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
        IrixGuestAddr dynstr = 0u, liblist = 0u;
        uint32_t libcount = 0u, loop_i = 0u, loop_off = 0u;

        if (obj && irix_guest_ptr(rt, obj + 0xa8u, 4u)) {
            dynstr = irix_guest_read32be(rt, obj + 0x58u);
            liblist = irix_guest_read32be(rt, obj + 0x6cu);
            libcount = irix_guest_read32be(rt, obj + 0xa8u);
        }
        if (sp && irix_guest_ptr(rt, sp + 12u, 4u)) {
            loop_i = irix_guest_read32be(rt, sp + 0u);
            loop_off = irix_guest_read32be(rt, sp + 8u);
        }

        fprintf(stderr,
                "  rld run_init_code: s0=0x%08x dynstr=0x%08x "
                "liblist=0x%08x libcount=%u sp_i=%u sp_i20=0x%08x\\n",
                obj, dynstr, liblist, libcount, loop_i, loop_off);

        report_guest_words(rt, "rld-object", obj);
        if (obj)
            report_guest_bytes_ascii(rt, "rld-object", obj, 0u, 0xe0u);
        if (dynstr)
            report_guest_bytes_ascii(rt, "rld-dynstr", dynstr, 0u, 0x100u);
        if (liblist)
            report_guest_words(rt, "rld-liblist", liblist);
    }

}

static int push_guest_string(IrixGuestRuntime *rt, IrixGuestAddr base,
                             IrixGuestAddr *top, const char *s,
                             IrixGuestAddr *addr_out)
{
    size_t n = strlen(s) + 1u;
    void *p;
    if ((uint64_t)*top < (uint64_t)base + n)
        return -1;
    *top -= (IrixGuestAddr)n;
    p = irix_guest_ptr(rt, *top, n);
    if (!p)
        return -1;
    memcpy(p, s, n);
    *addr_out = *top;
    return 0;
}

static int guest_env_is_loader_control(const char *s)
{
    static const char *const exact[] = {
        "LD_LIBRARY_PATH=",
        "LD_LIBRARYN32_PATH=",
        "LD_LIBRARY64_PATH=",
        "LD_PRELOAD=",
        "_RLD_PATH=",
        "_RLD_ROOT=",
        "_RLD_LIST=",
        "_RLDN32_LIST=",
        "_RLD64_LIST=",
        "_RLD_ARGS="
    };
    size_t i;

    if (!s)
        return 0;
    for (i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i) {
        size_t n = strlen(exact[i]);
        if (strncmp(s, exact[i], n) == 0)
            return 1;
    }
    return 0;
}

static int setup_process_stack(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                               int argc, char **argv,
                               const IrixGuestElfInfo *app,
                               const IrixGuestElfInfo *interp)
{
    enum {
        AT_NULL = 0,
        AT_PHDR = 3,
        AT_PHENT = 4,
        AT_PHNUM = 5,
        AT_PAGESZ = 6,
        AT_BASE = 7,
        AT_ENTRY = 9
    };
    /*
     * Keep the historical initial stack top at 0x7fe00000, but give the
     * original IRIX desktop stack substantially more room to grow downward.
     * Xt/Motif shell construction can consume more than the old 1 MiB mapping.
     */
    const IrixGuestAddr base = 0x7f600000u;
    const uint32_t size = 0x00800000u;
    IrixGuestAddr top = base + size - 0x100u;
    IrixGuestAddr sp, argv_addr, envp_addr;
    IrixGuestAddr *argv_guest = 0, *env_guest = 0;
    size_t envc = 0, i;
    /*
     * Match IRIX elf.c, not the Linux/SVR4 superset.  For a dynamic ELF
     * with a usable PT_PHDR, IRIX appends exactly:
     *
     *   AT_PHDR, AT_PHENT, AT_PHNUM, AT_ENTRY,
     *   AT_BASE, AT_PAGESZ, AT_NULL
     *
     * (AT_PFETCHFD is an R8K prefetch workaround and is not applicable here.)
     * AT_BASE is the runtime-loader mapping base rounded down to
     * ELF_MIPS_MAXPGSZ (64 KiB), while AT_PAGESZ is NBPP (16 KiB).
     *
     * elf.c uses AT_EXECFD instead of the first four entries when it cannot
     * provide the executable program headers.  Our loader always has the
     * program-header metadata, so this is the hasu path.
     */
    const uint32_t auxv[][2] = {
        {AT_PHDR, app ? app->phdr : 0u},
        {AT_PHENT, app ? app->phent : 0u},
        {AT_PHNUM, app ? app->phnum : 0u},
        {AT_ENTRY, app ? app->entry : 0u},
        {AT_BASE, interp ? (interp->load_base & ~0xffffu) : 0u},
        {AT_PAGESZ, 16384u},
        {AT_NULL, 0u}
    };

    /*
     * Do not blindly leak the host dynamic-linker environment into the IRIX
     * process.  In a MaXX session, variables such as LD_LIBRARY_PATH point at
     * /opt/MaXX/lib64 and cause the real IRIX rld to search Linux/x86 host
     * paths before its own N32 defaults.  Preserve ordinary environment state
     * (DISPLAY, HOME, LANG, etc.) but strip controls understood by IRIX rld.
     */
    if (environ) {
        for (i = 0; environ[i]; ++i)
            if (!guest_env_is_loader_control(environ[i]))
                ++envc;
    }

    if (irix_guest_map(rt, base, size, 3u) != 0)
        return -1;

    argv_guest = (IrixGuestAddr *)calloc((size_t)argc + 1u, sizeof(*argv_guest));
    env_guest = (IrixGuestAddr *)calloc(envc + 1u, sizeof(*env_guest));
    if (!argv_guest || !env_guest)
        goto fail;

    if (environ) {
        size_t dst = envc;
        size_t src = 0;
        while (environ[src])
            ++src;
        while (src > 0) {
            const char *e = environ[--src];
            if (guest_env_is_loader_control(e)) {
                fprintf(stderr, "IRIX env: suppress host loader variable %.*s\n",
                        (int)(strchr(e, '=') ? strchr(e, '=') - e : strlen(e)), e);
                continue;
            }
            --dst;
            if (push_guest_string(rt, base, &top, e, &env_guest[dst]) != 0)
                goto fail;
        }
    }
    for (i = (size_t)argc; i > 0; --i)
        if (push_guest_string(rt, base, &top, argv[i - 1u], &argv_guest[i - 1u]) != 0)
            goto fail;

    /*
     * fuexarg() lays the initial IRIX stack out as:
     *   argc, argv[], NULL, envp[], NULL, reserved auxv, strings.
     * elf.c fills that reserved auxv area after mapping PT_INTERP.
     *
     * Preserve the existing N32 alignment while reproducing that ordering.
     */
    top &= ~15u;
    {
        size_t words = 1u + ((size_t)argc + 1u) + (envc + 1u) +
                       (sizeof(auxv) / sizeof(auxv[0])) * 2u;
        uint64_t bytes = (uint64_t)words * 4u;
        IrixGuestAddr p;
        if ((uint64_t)top < (uint64_t)base + bytes)
            goto fail;
        sp = (IrixGuestAddr)((top - (IrixGuestAddr)bytes) & ~15u);
        p = sp;
        irix_guest_write32be(rt, p, (uint32_t)argc); p += 4u;
        argv_addr = p;
        for (i = 0; i < (size_t)argc; ++i) {
            irix_guest_write32be(rt, p, argv_guest[i]); p += 4u;
        }
        irix_guest_write32be(rt, p, 0u); p += 4u;
        envp_addr = p;
        for (i = 0; i < envc; ++i) {
            irix_guest_write32be(rt, p, env_guest[i]); p += 4u;
        }
        irix_guest_write32be(rt, p, 0u); p += 4u;
        for (i = 0; i < sizeof(auxv) / sizeof(auxv[0]); ++i) {
            irix_guest_write32be(rt, p, auxv[i][0]); p += 4u;
            irix_guest_write32be(rt, p, auxv[i][1]); p += 4u;
        }
    }

    cpu->gpr[4] = (uint64_t)(uint32_t)argc;
    cpu->gpr[5] = argv_addr;
    cpu->gpr[6] = envp_addr;
    cpu->gpr[28] = app ? app->gp_value : 0u;
    cpu->gpr[29] = sp;

    fprintf(stderr,
            "IRIX process stack: sp=0x%08x argv=0x%08x envp=0x%08x envc=%zu "
            "phdr=0x%08x phent=%u phnum=%u entry=0x%08x gp=0x%08x "
            "interp=%s interp_base=0x%08x\n",
            sp, argv_addr, envp_addr, envc,
            app ? app->phdr : 0u, app ? (unsigned)app->phent : 0u,
            app ? (unsigned)app->phnum : 0u, app ? app->entry : 0u,
            app ? app->gp_value : 0u,
            app && app->interp[0] ? app->interp : "(none)",
            interp ? interp->load_base : 0u);

    free(argv_guest);
    free(env_guest);
    return rt->trapped ? -1 : 0;

fail:
    free(argv_guest);
    free(env_guest);
    return -1;
}

int main(int argc, char **argv)
{
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestAddr entry;
    IrixGuestElfInfo app_elf;
    IrixGuestElfInfo interp_elf;
    IrixGuestElfInfo rld_elf;
    int have_app_elf = 0;
    int have_interp_elf = 0;
    int have_rld_elf = 0;
    char **guest_argv = argv;
#ifdef IRIX_DIRECT_MAIN
    char **direct_guest_argv = 0;
#endif

    memset(&app_elf, 0, sizeof(app_elf));
    memset(&interp_elf, 0, sizeof(interp_elf));
    memset(&rld_elf, 0, sizeof(rld_elf));
    /*
     * When subsystem profiling is enabled, identify this process by its
     * ecosystem manifest name. The runtime writes one callgraph per process.
     */
    if (getenv("IRIX_PROFILE_DIR") && !getenv("IRIX_PROFILE_NAME"))
        setenv("IRIX_PROFILE_NAME", IRIX_APP_NAME, 0);
#ifdef IRIX_DIRECT_MAIN
    /* Direct translated bring-up deliberately bypasses PT_INTERP/rld. */
    setenv("IRIX_EXEC_BACKEND", "static", 1);
#endif
    irix_guest_runtime_init(&rt);
    irix_guest_set_rootfs(&rt, IRIX_RUNTIME_TREE);
    memset(&cpu, 0, sizeof(cpu));
#ifdef IRIX_JIT_ONLY
    if (!irix_mips_jit_available()) {
        fprintf(stderr,
                "IRIX JIT-only runner requires Unicorn; rebuild after installing unicorn\n");
        return 1;
    }
    if (!irix_mips_jit_requested()) {
        fprintf(stderr,
                "IRIX JIT-only runner has JIT disabled by IRIX_EXEC_BACKEND\n");
        return 1;
    }
#endif

    /* rld's save_pixie_regs/restore_pixie_regs use fixed scratch storage at
       0x00200780.  Map the containing IRIX 16 KiB page so the original rld
       can save and restore its callee-saved registers normally. */
    if (irix_guest_map(&rt, 0x00200000u, 0x00004000u, 3u) != 0) {
        fprintf(stderr, "failed to map rld pixie register scratch page\n");
        return 1;
    }

#ifdef IRIX_JIT_ONLY
    if (load_elf(&rt, IRIX_RUNTIME_TREE, IRIX_APP_PATH, &app_elf) != 0) {
        fprintf(stderr, "failed to load original IRIX app %s\n", IRIX_APP_PATH);
        return 1;
    }
    have_app_elf = 1;
    if (app_elf.interp[0]) {
        const char *interp_rel = app_elf.interp[0] == '/' ?
                                 app_elf.interp + 1 : app_elf.interp;
        if (load_elf(&rt, IRIX_RUNTIME_TREE, interp_rel, &interp_elf) != 0) {
            fprintf(stderr, "failed to load PT_INTERP %s\n", app_elf.interp);
            return 1;
        }
        have_interp_elf = 1;
        fprintf(stderr,
                "IRIX JIT PT_INTERP: %s entry=0x%08x gp=0x%08x\n",
                app_elf.interp, interp_elf.entry, interp_elf.gp_value);
    }
    if (load_elf(&rt, IRIX_RUNTIME_TREE, "lib32/rld", &rld_elf) == 0)
        have_rld_elf = 1;
#else
    if (load_core_image(&rt) != 0) {
        fprintf(stderr, "failed to load synthetic core image\n");
        return 1;
    }

    {
        size_t i;
        for (i = 0; i < irix_app_guest_component_count; ++i) {
            const IrixAppGuestComponent *component = &irix_app_guest_components[i];
            const char *root;
            if (component->kind == 0)
                root = IRIX_RUNTIME_TREE;
            else if (component->kind == 1)
                root = IRIX_INTERNAL_TREE;
            else
                root = IRIX_DEV_TREE;
            {
                IrixGuestElfInfo info;

                /*
                 * Kernel/exec bootstrap owns the executable and PT_INTERP.
                 * libc then maps /lib32/rld itself.  Ordinary dependency DSOs
                 * must remain unmapped here so the original IRIX rld can own
                 * their PT_LOAD mappings and relocation lifecycle.
                 *
                 * The translated functions for those DSOs are still
                 * registered below; only their guest memory image is deferred
                 * to rld.
                 */
#ifdef IRIX_DIRECT_MAIN
                /*
                 * Direct translated execution bypasses the IRIX loader's
                 * control flow, not its memory images. Every component whose
                 * translated code can execute still needs its original guest
                 * PT_LOAD image for .data/.bss/.got and GP-relative globals.
                 *
                 * This includes PT_INTERP: on IRIX N32 /usr/lib32/libc.so.1
                 * is both the ELF interpreter and libc, so ordinary translated
                 * libc routines such as _stat/_cerror access writable state in
                 * that image even though direct-main never jumps to its entry.
                 *
                 * Map all resolved component images here. We still bypass
                 * PT_INTERP/rld execution by selecting main explicitly below.
                 */
#else
                if (i != 0u && component->role == 0)
                    continue;
#endif

                if (load_elf(&rt, root, component->path, &info) != 0)
                    return 1;
                if (i == 0u) {
                    app_elf = info;
                    have_app_elf = 1;
                }
                if (component->role == 1) {
                    interp_elf = info;
                    have_interp_elf = 1;
                    fprintf(stderr,
                            "IRIX PT_INTERP component: %s entry=0x%08x gp=0x%08x\n",
                            component->path, info.entry, info.gp_value);
                    if (irix_guest_ptr(&rt, 0x0fb5291cu, 4u))
                        fprintf(stderr,
                                "IRIX libc GOT startup watch: after PT_INTERP load "
                                "[0x0fb5291c]=%08x\n",
                                irix_guest_read32be(&rt, 0x0fb5291cu));
                } else if (component->role == 2) {
                    rld_elf = info;
                    have_rld_elf = 1;
                }
            }
        }
    }

#endif
    (void)rld_elf;
    (void)have_rld_elf;
    if (irix_libc_bridge_init(&rt, 0x30000000u, 0x04000000u) != 0) {
        fprintf(stderr, "failed to initialize guest heap\n");
        return 1;
    }
    irix_posix_bridge_init(&rt);
    irix_fs_bridge_init(&rt);
    if (irix_stdio_bridge_init(&rt) != 0) {
        fprintf(stderr, "failed to initialize guest stdio bridge\n");
        return 1;
    }
    irix_clib_extra_bridge_init(&rt);
    irix_cxx_bridge_init(&rt);
    irix_runtime_libc_bridge_init(&rt);
    irix_xrm_quark_bridge_init(&rt);
    irix_xrm_compat_init(&rt);
#ifdef IRIX_HAVE_FAST_QUARKS
    irix_xrm_quark_bridge_set_fast_table(irix_fast_quark_names, irix_fast_quark_count);
    irix_x11_bridge_init(&rt, irix_fast_quark_names, irix_fast_quark_count);
#else
    irix_xrm_quark_bridge_set_fast_table(0, 0);
    irix_x11_bridge_init(&rt, 0, 0);
#endif
    irix_x11_extra_init(&rt);
    irix_x11_keyboard_bridge_init(&rt);
    irix_x11_connection_bridge_init(&rt);
    irix_x11_context_bridge_init(&rt);
    irix_x11_window_bridge_init(&rt);
    irix_x11_image_bridge_init(&rt);
    irix_x11_wm_bridge_init(&rt);
    irix_runtime_x11_bridge_init(&rt);
    irix_runtime_xt_bridge_init(&rt);
    irix_x11_bulk_init(&rt);

    /*
     * Registration priority is first-match in irix_guest_find_function().
     *
     * Keep native overrides first.  Then install prebound/missing import
     * handlers before translated functions: SGI executables and DSOs may
     * contain executable import stubs which metadata recovery also emits as
     * ordinary translated functions.  If those translated stubs are
     * registered first, they shadow the name-based forwarder and private
     * runtime imports such as libc->_rld_new_interface execute the stub
     * itself instead of the real /usr/lib32/rld provider.
     *
     * The forwarder target does not need to be registered yet; lookup happens
     * when the import is actually called, after the complete bundle below has
     * been registered.
     */
#ifndef IRIX_JIT_ONLY
    irix_register_recompiled_stack_imports(&rt, resolve_native);
    register_irix_bridge_imports(&rt, resolve_native);
#ifdef IRIX_DIRECT_MAIN
    register_irix_prebound_imports(&rt, irix_libc_bridge_data_lookup, 1);
#else
    register_irix_prebound_imports(&rt, irix_libc_bridge_data_lookup, 0);
#endif
    if (irix_guest_ptr(&rt, 0x0fb5291cu, 4u))
        fprintf(stderr,
                "IRIX libc GOT startup watch: after import patching "
                "[0x0fb5291c]=%08x\n",
                irix_guest_read32be(&rt, 0x0fb5291cu));

    irix_register_recompiled_stack(&rt);
    register_native_dsos(&rt);

    /*
     * xwsh's stripped import veneers for getpwnam/getpwuid were recovered as
     * anonymous executable functions.  The guest rld leaves the application
     * GOT pointing at those veneers, so name-based bridge registration never
     * sees the calls.  Pin the two verified veneers before the app bundle is
     * registered; first-match lookup then sends them through the ABI bridge.
     *
     * authenticateUser__6wshAppGv proves the mapping:
     *   0x1001f41c takes a guest name pointer and stores struct passwd *
     *   0x1001f430 takes uid 1000 and stores struct passwd *
     */
    /*
     * Executable import veneers are now recovered generically from undefined
     * STT_FUNC dynsym entries with nonzero st_value by gen_prebound_bindings.
     * Keep AppRunner application-agnostic: bridge registration happens before
     * translated app functions, so exact-address first-match dispatch gives
     * those named veneers native authority without per-program pin tables.
     */

    irix_register_app_bundle(&rt);

    /*
     * The SGI libX11 XKB display initializer is reached through an interior
     * recovered entry at 0x0f706c48, not the nominal 0x0f706bdc boundary.
     * The crash consistently reports that recovered entry as the nearest
     * function below PC 0x0f706cb4.  Rebind the actual dispatched entry.
     */
    {
        IrixGuestFunction native = resolve_native("_IrixXkbDisplayInit");
        size_t i;
        if (native) {
            for (i = 0; i < rt.function_count; ++i) {
                IrixGuestFunctionEntry *e = &rt.functions[i];
                if (e->address == 0x0f706c48u) {
                    e->function = native;
                    e->name = "libX11:_IrixXkbDisplayInit";
                }
            }
        }
    }

    {
        size_t rebound = rebind_named_native_functions(&rt);
        fprintf(stderr,
                "IRIX native bridge authority: rebound=%zu named translated functions\\n",
                rebound);
    }
#else
    fprintf(stderr,
            "IRIX JIT-only mode: original MIPS execution; translated "
            "registries and native DSO overrides disabled\n");
#endif

#ifdef IRIX_DIRECT_MAIN
    /*
     * Keep the host harness name out of the guest process. The real IRIX
     * session bootstrap is /usr/sbin/desktop and Xt/resource/session logic
     * is allowed to derive application identity from argv[0].
     *
     * Preserve every caller-supplied argument after argv[0].
     */
    if (argc > 0) {
        const char *guest_name = strrchr(IRIX_APP_PATH, '/');
        size_t i;
        guest_name = guest_name ? guest_name + 1 : IRIX_APP_PATH;
        direct_guest_argv =
            (char **)calloc((size_t)argc + 1u, sizeof(*direct_guest_argv));
        if (!direct_guest_argv) {
            fprintf(stderr, "failed to allocate direct guest argv\n");
            return 1;
        }
        direct_guest_argv[0] = (char *)guest_name;
        for (i = 1u; i < (size_t)argc; ++i)
            direct_guest_argv[i] = argv[i];
        guest_argv = direct_guest_argv;
        fprintf(stderr,
                "IRIX direct guest identity: host argv0=%s guest argv0=%s\n",
                argv[0] ? argv[0] : "(null)", guest_name);
    }
#endif

    if (setup_process_stack(&rt, &cpu, argc, guest_argv,
                            have_app_elf ? &app_elf : 0,
                            have_interp_elf ? &interp_elf : 0) != 0) {
#ifdef IRIX_DIRECT_MAIN
        free(direct_guest_argv);
#endif
        fprintf(stderr, "failed to construct IRIX N32 process stack\n");
        return 1;
    }
#ifdef IRIX_DIRECT_MAIN
    free(direct_guest_argv);
    direct_guest_argv = 0;
#endif

    /*
     * For an ELF executable with PT_INTERP, the kernel transfers control to
     * the interpreter's entry point, while AT_ENTRY carries the executable's
     * own e_entry.  IRIX N32 desktop binaries use /usr/lib32/libc.so.1 as that
     * interpreter.  Entering the executable directly still skips the libc/rld
     * bootstrap, which is exactly what the previous run demonstrated.
     */
#ifdef IRIX_DIRECT_MAIN
    entry = find_named(&rt, "main");
    if (have_app_elf)
        cpu.gpr[28] = app_elf.gp_value;
    fprintf(stderr,
            "IRIX %s DIRECT translated main: main=0x%08x gp=0x%08x\n",
            IRIX_APP_NAME, entry, (uint32_t)cpu.gpr[28]);
    if (!entry) {
        fprintf(stderr, "translated %s main not found\n", IRIX_APP_NAME);
        return 1;
    }
#else
    entry = have_interp_elf ? interp_elf.entry :
            (have_app_elf ? app_elf.entry : 0u);
    if (have_interp_elf)
        cpu.gpr[28] = interp_elf.gp_value;
#ifdef IRIX_JIT_ONLY
    if (!entry) {
        fprintf(stderr, "IRIX JIT-only process has no entry point\n");
        return 1;
    }
#else
    if (!entry || !irix_guest_find_function(&rt, entry)) {
        IrixGuestAddr app_entry = have_app_elf ? app_elf.entry : 0u;
        fprintf(stderr,
                "warning: translated process entry 0x%08x unavailable; "
                "falling back to executable entry=0x%08x\n",
                entry, app_entry);
        entry = app_entry;
        if (have_app_elf)
            cpu.gpr[28] = app_elf.gp_value;
    }
    if (!entry || !irix_guest_find_function(&rt, entry)) {
        IrixGuestAddr main_entry = find_named(&rt, "main");
        fprintf(stderr,
                "warning: translated executable entry 0x%08x unavailable; "
                "falling back to main=0x%08x\n",
                entry, main_entry);
        entry = main_entry;
    }
    if (!entry) {
        fprintf(stderr, "translated %s process entry not found\n", IRIX_APP_NAME);
        return 1;
    }
#endif
#endif /* IRIX_DIRECT_MAIN */
    fprintf(stderr,
            "IRIX %s bootstrap: functions=%zu process_entry=0x%08x "
            "interp_entry=0x%08x app_entry=0x%08x gp=0x%08x "
            "main=0x%08x DISPLAY=%s\n",
            IRIX_APP_NAME, rt.function_count, entry,
            have_interp_elf ? interp_elf.entry : 0u,
            have_app_elf ? app_elf.entry : 0u,
            (uint32_t)cpu.gpr[28], find_named(&rt, "main"),
            getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
    if (irix_guest_call(&rt, &cpu, entry) != 0 || rt.trapped) {
        if (rt.trapped && rt.trap_code == 0xeeee23ffu) {
            int status = (int)(rt.fault_addr & 0xffu);
            fprintf(stderr, "IRIX %s exited: status=%d\n",
                    IRIX_APP_NAME, status);
            irix_guest_runtime_destroy(&rt);
            return status;
        }
        IrixGuestFunctionEntry *e = irix_guest_find_function(&rt, rt.trap_pc);
        IrixGuestFunctionEntry *fault_e = irix_guest_find_function(&rt, rt.fault_addr);
        IrixGuestFunctionEntry *caller = irix_guest_find_function(&rt, (IrixGuestAddr)cpu.pc);
        fprintf(stderr,
                "IRIX %s trapped: pc=0x%08x fault=0x%08x code=0x%08x symbol=%s fault_symbol=%s caller_pc=0x%08x caller=%s\n",
                IRIX_APP_NAME, rt.trap_pc, rt.fault_addr, rt.trap_code,
                e && e->name ? e->name : "?",
                fault_e && fault_e->name ? fault_e->name : "?",
                (uint32_t)cpu.pc,
                caller && caller->name ? caller->name : "?");
        report_trap_context(&rt, &cpu);
        irix_guest_runtime_destroy(&rt);
        return 2;
    }

    fprintf(stderr, "IRIX %s returned: v0=0x%llx\n",
            IRIX_APP_NAME, (unsigned long long)cpu.gpr[2]);
    irix_guest_runtime_destroy(&rt);
    return (int)(uint32_t)cpu.gpr[2];
}
