#include "IrixRuntimeXtBridge.h"

#include <X11/Intrinsic.h>
#include <stdio.h>
#include <string.h>

static IrixGuestRuntime *g_rt;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    const char *s;
    size_t i;

    if (!addr)
        return NULL;
    s = (const char *)irix_guest_ptr(rt, addr, 1u);
    if (!s)
        return NULL;

    /*
     * Do not hand an unterminated guest mapping to host Xt.
     * A 64K ceiling is far beyond any sane Xt diagnostic string.
     */
    for (i = 0; i < 65536u; ++i) {
        const char *p = (const char *)irix_guest_ptr(rt, addr + (IrixGuestAddr)i, 1u);
        if (!p)
            return NULL;
        if (*p == '\0')
            return s;
    }
    return NULL;
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
    MAP(XtWarning);
    MAP(XtError);
    MAP(XtAppWarning);
    MAP(XtAppError);
#undef MAP

    return 0;
}
