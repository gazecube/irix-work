#include "IrixGuest.h"
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
#include "IrixX11WindowBridge.h"
#include "IrixX11WmBridge.h"
#include "IrixX11ContextBridge.h"
#include "IrixXrmCompat.h"
#include "IrixXrmQuarkBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

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
void register_irix_prebound_imports(IrixGuestRuntime *rt, IrixGuestAddr (*data_resolver)(const char *));
void irix_x11_extra_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_extra_lookup(const char *name);
void irix_x11_connection_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_connection_bridge_lookup(const char *name);
void irix_x11_bulk_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_bulk_lookup(const char *name);
void irix_x11_image_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_image_bridge_lookup(const char *name);

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
    return irix_x11_bulk_lookup(name);
}

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
    return 0;
}

static int load_elf(IrixGuestRuntime *rt, const char *root, const char *rel,
                    IrixGuestElfInfo *info_out)
{
    char path[4096];
    IrixGuestElfInfo info;
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    if (irix_guest_load_elf32be_info(rt, path, &info) != 0) {
        fprintf(stderr, "failed to load guest ELF: %s\n", path);
        return -1;
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
            "  regs: v0=%08x sp=%08x gp=%08x ra=%08x a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
            (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[29],
            (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31],
            (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
            (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
            (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
            (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
    report_guest_words(rt, "v0", (IrixGuestAddr)(uint32_t)cpu->gpr[2]);
    report_guest_words(rt, "a0", (IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    report_guest_words(rt, "a1", (IrixGuestAddr)(uint32_t)cpu->gpr[5]);
    report_guest_words(rt, "a3", (IrixGuestAddr)(uint32_t)cpu->gpr[7]);
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
        AT_ENTRY = 9,
        AT_UID = 11,
        AT_EUID = 12,
        AT_GID = 13,
        AT_EGID = 14
    };
    const IrixGuestAddr base = 0x7fd00000u;
    const uint32_t size = 0x00100000u;
    IrixGuestAddr top = base + size - 0x100u;
    IrixGuestAddr sp, argv_addr, envp_addr;
    IrixGuestAddr *argv_guest = 0, *env_guest = 0;
    size_t envc = 0, i;
    const uint32_t auxv[][2] = {
        {AT_PHDR, app ? app->phdr : 0u},
        {AT_PHENT, app ? app->phent : 0u},
        {AT_PHNUM, app ? app->phnum : 0u},
        {AT_PAGESZ, 16384u},
        {AT_BASE, interp ? interp->load_base : 0u},
        {AT_ENTRY, app ? app->entry : 0u},
        {AT_UID, (uint32_t)getuid()},
        {AT_EUID, (uint32_t)geteuid()},
        {AT_GID, (uint32_t)getgid()},
        {AT_EGID, (uint32_t)getegid()},
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

    /* N32 process entry uses a 16-byte aligned stack. Build the canonical
       argc/argv/envp/auxv vectors below the copied strings. */
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

    memset(&app_elf, 0, sizeof(app_elf));
    memset(&interp_elf, 0, sizeof(interp_elf));
    memset(&rld_elf, 0, sizeof(rld_elf));
    irix_guest_runtime_init(&rt);
    irix_guest_set_rootfs(&rt, IRIX_RUNTIME_TREE);
    memset(&cpu, 0, sizeof(cpu));

    /* rld's save_pixie_regs/restore_pixie_regs use fixed scratch storage at
       0x00200780.  Map the containing IRIX 16 KiB page so the original rld
       can save and restore its callee-saved registers normally. */
    if (irix_guest_map(&rt, 0x00200000u, 0x00004000u, 3u) != 0) {
        fprintf(stderr, "failed to map rld pixie register scratch page\n");
        return 1;
    }

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
                if (i != 0u && component->role == 0)
                    continue;

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
                } else if (component->role == 2) {
                    rld_elf = info;
                    have_rld_elf = 1;
                }
            }
        }
    }

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
    irix_register_recompiled_stack_imports(&rt, resolve_native);
    register_irix_bridge_imports(&rt, resolve_native);
    register_irix_prebound_imports(&rt, irix_libc_bridge_data_lookup);

    irix_register_recompiled_stack(&rt);
    irix_register_app_bundle(&rt);

    if (setup_process_stack(&rt, &cpu, argc, argv,
                            have_app_elf ? &app_elf : 0,
                            have_interp_elf ? &interp_elf : 0) != 0) {
        fprintf(stderr, "failed to construct IRIX N32 process stack\n");
        return 1;
    }

    /*
     * For an ELF executable with PT_INTERP, the kernel transfers control to
     * the interpreter's entry point, while AT_ENTRY carries the executable's
     * own e_entry.  IRIX N32 desktop binaries use /usr/lib32/libc.so.1 as that
     * interpreter.  Entering the executable directly still skips the libc/rld
     * bootstrap, which is exactly what the previous run demonstrated.
     */
    entry = have_interp_elf ? interp_elf.entry :
            (have_app_elf ? app_elf.entry : 0u);
    if (have_interp_elf)
        cpu.gpr[28] = interp_elf.gp_value;
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
        return 2;
    }

    fprintf(stderr, "IRIX %s returned: v0=0x%llx\n",
            IRIX_APP_NAME, (unsigned long long)cpu.gpr[2]);
    irix_guest_runtime_destroy(&rt);
    return (int)(uint32_t)cpu.gpr[2];
}
