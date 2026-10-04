#include "IrixRuntimeXtBridge.h"
#include "IrixXrmCompat.h"

#include <X11/Intrinsic.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IrixGuestRuntime *g_rt;
static XtAppContext g_xt_path_app;
static Display *g_xt_path_display;
static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr);

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestCpu caller;
    IrixGuestAddr predicate;
} IrixXtPredicateContext;

typedef struct {
    char match;
    String substitution;
} IrixXtHostSubstitution;

static IrixXtPredicateContext *g_xt_predicate_context;

static IrixGuestFunctionEntry *guest_xt_function(IrixGuestRuntime *rt,
                                                  const char *name)
{
    size_t i;
    IrixGuestFunctionEntry *fallback = 0;
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *entry = &rt->functions[i];
        if (!entry->name || strcmp(entry->name, name) != 0 ||
            (entry->flags & IRIX_GUEST_FUNCTION_NATIVE))
            continue;
        if (entry->address >= 0x0f600000u && entry->address < 0x0f6a0000u)
            return entry;
        if (!fallback) fallback = entry;
    }
    return fallback;
}

static IrixGuestAddr guest_xt_alloc(IrixGuestRuntime *rt,
                                    const IrixGuestCpu *caller,
                                    uint32_t size)
{
    IrixGuestFunctionEntry *entry = guest_xt_function(rt, "XtMalloc");
    IrixGuestCpu call;
    if (!entry || !size) return 0;
    call = *caller;
    call.gpr[4] = size;
    call.gpr[25] = entry->address;
    if (irix_guest_call(rt, &call, entry->address) != 0 || rt->trapped)
        return 0;
    return (IrixGuestAddr)(uint32_t)call.gpr[2];
}

static void guest_xt_release(IrixGuestRuntime *rt,
                             const IrixGuestCpu *caller,
                             IrixGuestAddr address)
{
    IrixGuestFunctionEntry *entry = guest_xt_function(rt, "XtFree");
    IrixGuestCpu call;
    if (!entry || !address) return;
    call = *caller;
    call.gpr[4] = address;
    call.gpr[25] = entry->address;
    (void)irix_guest_call(rt, &call, entry->address);
}

static IrixGuestAddr guest_copy_string(IrixGuestRuntime *rt,
                                       const IrixGuestCpu *caller,
                                       const char *text)
{
    size_t size;
    IrixGuestAddr address;
    void *dst;
    if (!text) return 0;
    size = strlen(text) + 1u;
    if (size > UINT32_MAX) return 0;
    address = guest_xt_alloc(rt, caller, (uint32_t)size);
    if (!address) return 0;
    dst = irix_guest_ptr(rt, address, size);
    if (!dst) {
        guest_xt_release(rt, caller, address);
        return 0;
    }
    memcpy(dst, text, size);
    return address;
}

static Boolean guest_file_predicate(String filename)
{
    IrixXtPredicateContext *context = g_xt_predicate_context;
    IrixGuestAddr guest_name;
    IrixGuestCpu call;
    IrixXtPredicateContext *previous;
    int result;
    if (!context || !context->predicate) return True;
    guest_name = guest_copy_string(context->rt, &context->caller, filename);
    if (!guest_name) return False;
    call = context->caller;
    call.gpr[4] = guest_name;
    call.gpr[25] = context->predicate;
    previous = g_xt_predicate_context;
    result = irix_guest_call(context->rt, &call, context->predicate);
    g_xt_predicate_context = previous;
    guest_xt_release(context->rt, &context->caller, guest_name);
    return result == 0 && !context->rt->trapped ? (Boolean)(int32_t)call.gpr[2] : False;
}

static char *root_search_path(IrixGuestRuntime *rt, const char *path)
{
    size_t cap, used = 0;
    const char *cursor;
    char *out;
    if (!path || !rt || !rt->rootfs) return 0;
    cap = strlen(path) + strlen(rt->rootfs) * 16u + 32u;
    out = (char *)malloc(cap);
    if (!out) return 0;
    cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ':');
        size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
        if (n && cursor[0] == '/') {
            char guest_part[PATH_MAX];
            char resolved[PATH_MAX];
            const char *host_part;
            if (n >= sizeof(guest_part)) { free(out); return 0; }
            memcpy(guest_part, cursor, n);
            guest_part[n] = '\0';
            host_part = irix_guest_resolve_path(rt, guest_part, resolved,
                                                sizeof(resolved),
                                                "Xt search path", 1);
            if (!host_part) { free(out); return 0; }
            n = strlen(host_part);
            if (used + n + 2u > cap) { free(out); return 0; }
            memcpy(out + used, host_part, n);
            used += n;
        } else {
            if (used + n + 2u > cap) { free(out); return 0; }
            memcpy(out + used, cursor, n);
            used += n;
        }
        if (!end) break;
        out[used++] = ':';
        cursor = end + 1;
    }
    out[used] = '\0';
    return out;
}

static char *xt_default_search_path(IrixGuestRuntime *rt)
{
    static const char *const suffixes[] = {
        "%L/%T/%N%C%S", "%l/%T/%N%C%S", "%T/%N%C%S",
        "%L/%T/%N%S", "%l/%T/%N%S", "%T/%N%S"
    };
    size_t i, root_size, total;
    char *path, *p;
    if (!rt || !rt->rootfs) return 0;
    root_size = strlen(rt->rootfs);
    total = 6u * (root_size + sizeof("/usr/lib/X11/") - 1u + 32u) + 1u;
    path = (char *)malloc(total);
    if (!path) return 0;
    p = path;
    for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        int n = snprintf(p, total - (size_t)(p - path), "%s/usr/lib/X11/%s%s",
                         rt->rootfs, suffixes[i], i + 1u < 6u ? ":" : "");
        if (n < 0 || (size_t)n >= total - (size_t)(p - path)) {
            free(path);
            return 0;
        }
        p += n;
    }
    return path;
}

static int guest_xt_app_identity(IrixGuestRuntime *rt, const IrixGuestCpu *caller,
                                 char **app_name, char **app_class)
{
    IrixGuestFunctionEntry *entry = guest_xt_function(rt, "XtGetApplicationNameAndClass");
    IrixGuestAddr out;
    IrixGuestCpu call;
    const char *name, *class_name;
    if (!entry) return 0;
    out = guest_xt_alloc(rt, caller, 8u);
    if (!out) return 0;
    irix_guest_write32be(rt, out, 0);
    irix_guest_write32be(rt, out + 4u, 0);
    call = *caller;
    call.gpr[4] = caller->gpr[4];
    call.gpr[5] = out;
    call.gpr[6] = out + 4u;
    call.gpr[25] = entry->address;
    if (irix_guest_call(rt, &call, entry->address) != 0 || rt->trapped) {
        guest_xt_release(rt, caller, out);
        return 0;
    }
    name = guest_cstr(rt, irix_guest_read32be(rt, out));
    class_name = guest_cstr(rt, irix_guest_read32be(rt, out + 4u));
    if (name && class_name) {
        *app_name = strdup(name);
        *app_class = strdup(class_name);
    }
    guest_xt_release(rt, caller, out);
    return *app_name && *app_class;
}

static Display *xt_path_display(int *argc, char **argv,
                                const char *app_name, const char *app_class)
{
    Display *display = irix_xrm_compat_display();
    XrmDatabase database;
    if (!display) return 0;
    if (g_xt_path_display == display) return display;
    if (!g_xt_path_app) g_xt_path_app = XtCreateApplicationContext();
    if (!g_xt_path_app) return 0;
    database = XrmGetDatabase(display);
    XtDisplayInitialize(g_xt_path_app, display, app_name, app_class, 0, 0,
                        argc, argv);
    XrmSetDatabase(display, database);
    g_xt_path_display = display;
    return display;
}

static void bridge_XtResolvePathname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr path_addr = (uint32_t)cpu->gpr[8];
    IrixGuestAddr subs_addr = (uint32_t)cpu->gpr[9];
    IrixGuestAddr predicate = (uint32_t)cpu->gpr[11];
    const char *type = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *filename = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    const char *suffix = guest_cstr(rt, (uint32_t)cpu->gpr[7]);
    const char *guest_path = path_addr ? guest_cstr(rt, path_addr) : 0;
    const char *env_path = getenv("XFILESEARCHPATH");
    char *owned_path = 0;
    const char *search_path = guest_path;
    IrixXtHostSubstitution *substitutions = 0;
    Substitution host_substitutions = 0;
    Cardinal substitution_count = (Cardinal)(uint32_t)cpu->gpr[10];
    Display *display;
    String resolved;
    IrixXtPredicateContext predicate_context;
    IrixXtPredicateContext *previous_predicate;
    IrixGuestAddr result;
    size_t i;
    int argc = 0;
    char *argv[] = { (char *)"irix", 0 };
    char *app_name = 0, *app_class = 0;

    if (getenv("IRIX_RESOURCE_TRACE"))
        fprintf(stderr,
                "IRIX XtResolvePathname request: caller=%08x type=%s filename=%s suffix=%s path=%s substitutions=%08x count=%u predicate=%08x XFILESEARCHPATH=%s XAPPLRESDIR=%s\n",
                (uint32_t)cpu->gpr[31], type ? type : "<null>",
                filename ? filename : "<null>", suffix ? suffix : "<null>",
                guest_path ? guest_path : (path_addr ? "<invalid>" : "<null>"),
                subs_addr, substitution_count, predicate,
                env_path ? env_path : "<null>",
                getenv("XAPPLRESDIR") ? getenv("XAPPLRESDIR") : "<null>");

    if (rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }
    if (path_addr && !guest_path &&
        irix_xrm_compat_is_database_handle(rt, path_addr)) {
        /*
         * SGI's optional UserDefined palette probe occasionally carries its
         * XrmDatabase token in XtResolvePathname's path slot.  That token is
         * not a string.  Xt treats a null path as the normal default search
         * path, which lets the built-in Base palette fallback resolve while
         * avoiding a guest-side invalid memory read.
         */
        if (getenv("IRIX_RESOURCE_TRACE"))
            fprintf(stderr,
                    "IRIX XtResolvePathname: Xrm database handle %08x in path slot; using default search for %s\n",
                    path_addr, filename ? filename : "<null>");
        path_addr = 0;
        guest_path = 0;
        search_path = 0;
    }
    if (path_addr && !guest_path) {
        fprintf(stderr,
                "IRIX XtResolvePathname: invalid path=%08x caller=%08x "
                "type=%08x filename=%08x suffix=%08x substitutions=%08x "
                "count=%08x predicate=%08x display=%08x\n",
                path_addr, (uint32_t)cpu->gpr[31],
                (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6],
                (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[9],
                (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11],
                (uint32_t)cpu->gpr[4]);
        cpu->gpr[2] = 0;
        return;
    }
    if (!search_path) search_path = env_path;
    if (!search_path) {
        owned_path = xt_default_search_path(rt);
        search_path = owned_path;
    } else {
        owned_path = root_search_path(rt, search_path);
        search_path = owned_path;
    }
    if (!search_path) {
        free(owned_path);
        cpu->gpr[2] = 0;
        return;
    }

    if (substitution_count > 0u) {
        if (!subs_addr || substitution_count > 1024u) {
            free(owned_path);
            cpu->gpr[2] = 0;
            return;
        }
        substitutions = (IrixXtHostSubstitution *)calloc(
            substitution_count, sizeof(*substitutions));
        if (!substitutions) { free(owned_path); cpu->gpr[2] = 0; return; }
        for (i = 0; i < substitution_count; ++i) {
            IrixGuestAddr rec = subs_addr + (IrixGuestAddr)(i * 8u);
            IrixGuestAddr text_addr = irix_guest_read32be(rt, rec + 4u);
            const char *text = guest_cstr(rt, text_addr);
            if (rt->trapped || !text) {
                free(substitutions); free(owned_path); cpu->gpr[2] = 0; return;
            }
            substitutions[i].match = (char)irix_guest_read8(rt, rec);
            substitutions[i].substitution = (String)text;
        }
        host_substitutions = (Substitution)substitutions;
    }

    if (!guest_xt_app_identity(rt, cpu, &app_name, &app_class)) {
        free(app_name);
        free(app_class);
        app_name = strdup("irix");
        app_class = strdup("IRIX");
    }
    if (getenv("IRIX_RESOURCE_TRACE"))
        fprintf(stderr, "IRIX XtResolvePathname identity: name=%s class=%s\n",
                app_name ? app_name : "<null>", app_class ? app_class : "<null>");
    display = xt_path_display(&argc, argv, app_name, app_class);
    if (!display) {
        free(app_name); free(app_class);
        free(substitutions); free(owned_path); cpu->gpr[2] = 0; return;
    }

    predicate_context.rt = rt;
    predicate_context.caller = *cpu;
    predicate_context.predicate = predicate;
    previous_predicate = g_xt_predicate_context;
    g_xt_predicate_context = &predicate_context;
    /* SGI's NULL filename selects the application class for %N. */
    resolved = XtResolvePathname(display, type,
                                 filename ? filename : app_class, suffix,
                                 (String)search_path, host_substitutions,
                                 substitution_count,
                                 predicate ? guest_file_predicate : 0);
    g_xt_predicate_context = previous_predicate;
    free(app_name);
    free(app_class);
    free(substitutions);
    if (!resolved) {
        if (getenv("IRIX_RESOURCE_TRACE"))
            fprintf(stderr, "IRIX XtResolvePathname miss: caller=%08x search=%s\n",
                    (uint32_t)cpu->gpr[31], search_path ? search_path : "<null>");
        free(owned_path);
        cpu->gpr[2] = 0;
        return;
    }

    free(owned_path);

    result = guest_copy_string(rt, cpu, resolved);
    XtFree(resolved);
    if (result && !rt->trapped) {
        char resolved_path[PATH_MAX];
        const char *final_path = irix_guest_resolve_path(
            rt, irix_guest_cstr(rt, result, PATH_MAX), resolved_path,
            sizeof(resolved_path), "resource:XtResolvePathname", 1);
        if (final_path && final_path != irix_guest_cstr(rt, result, PATH_MAX)) {
            size_t n = strlen(final_path) + 1u;
            IrixGuestAddr replacement = guest_xt_alloc(rt, cpu, (uint32_t)n);
            void *dst = replacement ? irix_guest_ptr(rt, replacement, n) : 0;
            if (dst) { memcpy(dst, final_path, n); guest_xt_release(rt, cpu, result); result = replacement; }
            else { if (replacement) guest_xt_release(rt, cpu, replacement); guest_xt_release(rt, cpu, result); result = 0; }
        }
    }
    cpu->gpr[2] = result;
}

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static void bridge_XtToolkitInitialize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    XtToolkitInitialize();
    cpu->gpr[2] = 0;
}

static void bridge_XtWarning(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *msg = guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    if (msg)
        XtWarning(msg);
    else
        fprintf(stderr, "IRIX XtWarning: <invalid guest string 0x%08x>\n",
                (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = 0;
}

static void bridge_XtError(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *msg = guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[4]);

    /*
     * Deadline bring-up: SGI Xt can reach the fatal windowed-ancestor path
     * for an absent optional widget while 4Dwm is constructing screen
     * resources.  We already treat the corresponding NULL realization paths
     * as empty/optional.  Keep this one exact diagnostic nonfatal so the WM
     * can continue startup; all other XtError calls still trap normally.
     */
    if (msg &&
        (!strcmp(msg, "Object \"(null)\" does not have windowed ancestor") ||
         !strcmp(msg, "Couldn't find per display information"))) {
        fprintf(stderr,
                "IRIX Xt bring-up compatibility: suppressed fatal: %s\n",
                msg);
        cpu->gpr[2] = 0u;
        cpu->gpr[0] = 0u;
        return;
    }

    /*
     * Host XtError normally terminates the process.  Keep the translated
     * runtime in control instead: report the same diagnostic and trap at the
     * guest boundary so we still get useful IRIX state.
     */
    fprintf(stderr, "IRIX XtError via host bridge: %s\n", msg ? msg : "<invalid guest string>");
    irix_guest_trap(rt, cpu, 0xeeee3101u);
}

static void bridge_XtAppWarning(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *msg = guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5]);

    /*
     * The guest XtAppContext is deliberately ignored here.  Passing it to
     * host libXt would reinterpret a guest pointer as a native object.
     */
    if (msg)
        XtWarning(msg);
    else
        fprintf(stderr, "IRIX XtAppWarning: <invalid guest string 0x%08x>\n",
                (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = 0;
}

static void bridge_XtAppError(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *msg = guest_cstr(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[5]);
    fprintf(stderr, "IRIX XtAppError via host bridge: %s\n", msg ? msg : "<invalid guest string>");
    irix_guest_trap(rt, cpu, 0xeeee3102u);
}

void irix_runtime_xt_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    (void)g_rt;
}

IrixGuestFunction irix_runtime_xt_bridge_lookup(const char *name)
{
    if (!name)
        return 0;

#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(XtResolvePathname);
    MAP(XtWarning);
    MAP(XtError);
    MAP(XtAppWarning);
    MAP(XtAppError);
#undef MAP

    return 0;
}
