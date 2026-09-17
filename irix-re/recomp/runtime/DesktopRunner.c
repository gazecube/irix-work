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

#ifdef IRIX_HAVE_FAST_QUARKS
#include "irix_fast_quarks.inc"
#endif

#ifndef IRIX_RUNTIME_TREE
#define IRIX_RUNTIME_TREE ""
#endif
#ifndef IRIX_INTERNAL_TREE
#define IRIX_INTERNAL_TREE ""
#endif
#ifndef IRIX_CORE_IMAGE_DIR
#define IRIX_CORE_IMAGE_DIR ""
#endif

void irix_register_recompiled_stack(IrixGuestRuntime *rt);
void irix_register_recompiled_stack_imports(IrixGuestRuntime *rt, IrixGuestFunction (*resolver)(const char *));
void register_desktop(IrixGuestRuntime *rt);
void register_imdApp(IrixGuestRuntime *rt);
void register_imdUtil(IrixGuestRuntime *rt);
void register_imdXutil(IrixGuestRuntime *rt);
void register_imdModel(IrixGuestRuntime *rt);
void register_imdIconPanel(IrixGuestRuntime *rt);
void register_imdFtrDB(IrixGuestRuntime *rt);
void register_imdFtrGallery(IrixGuestRuntime *rt);
void register_imdFtrIcon(IrixGuestRuntime *rt);
void register_irix_bridge_imports(IrixGuestRuntime *rt, IrixGuestFunction (*resolver)(const char *));
void register_irix_prebound_imports(IrixGuestRuntime *rt);
void irix_x11_extra_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_extra_lookup(const char *name);
void irix_x11_connection_bridge_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_connection_bridge_lookup(const char *name);
void irix_x11_bulk_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_bulk_lookup(const char *name);

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

static int load_elf(IrixGuestRuntime *rt, const char *root, const char *rel)
{
    char path[4096]; IrixGuestAddr entry = 0;
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    if (irix_guest_load_elf32be(rt, path, &entry) != 0) {
        fprintf(stderr, "failed to load guest ELF: %s\n", path);
        return -1;
    }
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

static int setup_argv(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int argc, char **argv)
{
    const IrixGuestAddr base = 0x7fd00000u;
    const uint32_t size = 0x00100000u;
    IrixGuestAddr cur = base + 0x1000u;
    IrixGuestAddr argv_addr = base + 0x100u;
    int i;
    if (irix_guest_map(rt, base, size, 3u) != 0) return -1;
    for (i = 0; i < argc; ++i) {
        size_t n = strlen(argv[i]) + 1u;
        void *p;
        if ((uint64_t)cur + n >= (uint64_t)base + size) return -1;
        p = irix_guest_ptr(rt, cur, n);
        if (!p) return -1;
        memcpy(p, argv[i], n);
        irix_guest_write32be(rt, argv_addr + (IrixGuestAddr)(i * 4), cur);
        cur += (IrixGuestAddr)n;
    }
    irix_guest_write32be(rt, argv_addr + (IrixGuestAddr)(argc * 4), 0);
    cpu->gpr[4] = (uint64_t)(uint32_t)argc;
    cpu->gpr[5] = argv_addr;
    cpu->gpr[29] = base + size - 0x100u;
    return 0;
}

int main(int argc, char **argv)
{
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestAddr entry;

    irix_guest_runtime_init(&rt);
    memset(&cpu, 0, sizeof(cpu));

    if (load_core_image(&rt) != 0) {
        fprintf(stderr, "failed to load synthetic core image\n");
        return 1;
    }

    if (load_elf(&rt, IRIX_RUNTIME_TREE, "usr/sbin/desktop") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdApp.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdUtil.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdXutil.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdModel.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdIconPanel.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdFtrDB.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdFtrGallery.so") != 0 ||
        load_elf(&rt, IRIX_INTERNAL_TREE, "usr/lib32/internal/libimdFtrIcon.so") != 0)
        return 1;

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
    irix_x11_bridge_init(&rt, irix_fast_quark_names, irix_fast_quark_count);
#else
    irix_x11_bridge_init(&rt, 0, 0);
#endif
    irix_x11_extra_init(&rt);
    irix_x11_keyboard_bridge_init(&rt);
    irix_x11_connection_bridge_init(&rt);
    irix_x11_context_bridge_init(&rt);
    irix_x11_window_bridge_init(&rt);
    irix_x11_wm_bridge_init(&rt);
    irix_runtime_x11_bridge_init(&rt);
    irix_x11_bulk_init(&rt);

    /* Guest lookup is first-match. Register native import bridges before any
       translated definition at the same prebound address so deliberate ABI
       overrides (for example host-backed Xlib objects) actually take effect. */
    irix_register_recompiled_stack_imports(&rt, resolve_native);
    register_irix_bridge_imports(&rt, resolve_native);

    irix_register_recompiled_stack(&rt);
    register_desktop(&rt);
    register_imdApp(&rt);
    register_imdUtil(&rt);
    register_imdXutil(&rt);
    register_imdModel(&rt);
    register_imdIconPanel(&rt);
    register_imdFtrDB(&rt);
    register_imdFtrGallery(&rt);
    register_imdFtrIcon(&rt);

    /* Prebound and missing-import forwarders remain fallback entries. */
    register_irix_prebound_imports(&rt);

    if (setup_argv(&rt, &cpu, argc, argv) != 0) {
        fprintf(stderr, "failed to construct guest argv/stack\n");
        return 1;
    }

    entry = find_named(&rt, "main");
    if (!entry) {
        fprintf(stderr, "translated desktop main not found\n");
        return 1;
    }

    fprintf(stderr, "IRIX desktop bootstrap: functions=%zu main=0x%08x DISPLAY=%s\n",
            rt.function_count, entry, getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
    if (irix_guest_call(&rt, &cpu, entry) != 0 || rt.trapped) {
        IrixGuestFunctionEntry *e = irix_guest_find_function(&rt, rt.trap_pc);
        IrixGuestFunctionEntry *fault_e = irix_guest_find_function(&rt, rt.fault_addr);
        IrixGuestFunctionEntry *caller = irix_guest_find_function(&rt, (IrixGuestAddr)cpu.pc);
        fprintf(stderr,
                "IRIX desktop trapped: pc=0x%08x fault=0x%08x code=0x%08x symbol=%s fault_symbol=%s caller_pc=0x%08x caller=%s\n",
                rt.trap_pc, rt.fault_addr, rt.trap_code,
                e && e->name ? e->name : "?",
                fault_e && fault_e->name ? fault_e->name : "?",
                (uint32_t)cpu.pc,
                caller && caller->name ? caller->name : "?");
        return 2;
    }

    fprintf(stderr, "IRIX desktop returned: v0=0x%llx\n",
            (unsigned long long)cpu.gpr[2]);
    irix_guest_runtime_destroy(&rt);
    return (int)(uint32_t)cpu.gpr[2];
}
