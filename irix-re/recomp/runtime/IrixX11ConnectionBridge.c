#include "IrixX11ConnectionBridge.h"
#include "IrixLibcBridge.h"

#include <X11/Xlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * XAddConnectionWatch/XInternalConnectionNumbers exist so Xlib extensions
 * can expose auxiliary transport file descriptors to Xt's event loop.
 *
 * The current translated desktop stack has no bridged Xlib extension that
 * opens such auxiliary connections. Represent that state explicitly: watches
 * can be installed/removed successfully, but there are zero internal FDs to
 * enumerate or process. This avoids leaking host callbacks or host pointers
 * into the N32 guest while keeping Xt's connection-watch bootstrap intact.
 */

typedef struct {
    IrixGuestRuntime *rt;
} IrixX11ConnectionState;

static IrixX11ConnectionState g_conn;

static IrixGuestAddr copy_host_string_to_guest(IrixGuestRuntime *rt,
                                                IrixGuestCpu *cpu,
                                                const char *s)
{
    IrixGuestFunction malloc_fn;
    IrixGuestCpu call;
    IrixGuestAddr out;
    size_t n;
    void *dst;

    fprintf(stderr, "XRMSTR copy enter s=%p trapped=%d fault=0x%08x\n",
            (const void *)s, rt->trapped, rt->fault_addr);
    if (!s)
        return 0;
    n = strlen(s) + 1u;
    fprintf(stderr, "XRMSTR copy strlen n=%zu trapped=%d fault=0x%08x\n",
            n, rt->trapped, rt->fault_addr);
    if (n > UINT32_MAX)
        return 0;

    malloc_fn = irix_libc_bridge_lookup("malloc");
    fprintf(stderr, "XRMSTR copy malloc_fn=%p\n", (void *)malloc_fn);
    if (!malloc_fn)
        return 0;
    call = *cpu;
    call.gpr[4] = (uint32_t)n;
    malloc_fn(rt, &call);
    out = (IrixGuestAddr)(uint32_t)call.gpr[2];
    fprintf(stderr,
            "XRMSTR copy malloc out=0x%08x trapped=%d fault=0x%08x\n",
            out, rt->trapped, rt->fault_addr);
    if (!out)
        return 0;
    dst = irix_guest_ptr(rt, out, n);
    fprintf(stderr, "XRMSTR copy guest_ptr=%p\n", dst);
    if (!dst)
        return 0;
    memcpy(dst, s, n);
    fprintf(stderr,
            "XRMSTR copy done out=0x%08x trapped=%d fault=0x%08x\n",
            out, rt->trapped, rt->fault_addr);
    return out;
}

static void bridge_XResourceManagerString(IrixGuestRuntime *rt,
                                          IrixGuestCpu *cpu)
{
    Display *dpy;
    const char *resource_string;
    IrixGuestAddr out = 0;

    fprintf(stderr,
            "XRMSTR bridge enter a0=0x%08x pc=0x%08x trapped=%d fault=0x%08x\n",
            (uint32_t)cpu->gpr[4], (uint32_t)cpu->pc,
            rt->trapped, rt->fault_addr);

    /* The second call currently occurs while __ct__23ImdClassResourceManager
       is nested inside __ct__26ImdDefaultsResourceManager.  If its Display *
       argument is NULL, use the known N32 frame sizes to recover the outer
       constructor's saved caller RA and original this pointer. */
    if ((uint32_t)cpu->gpr[4] == 0u) {
        IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
        IrixGuestAddr outer_ra_addr = sp + 2128u;
        IrixGuestAddr outer_this_addr = sp + 2164u;
        uint64_t outer_ra_slot = 0;
        uint32_t outer_ra = 0;
        uint32_t outer_this = 0;
        if (irix_guest_ptr(rt, outer_ra_addr, 8u)) {
            outer_ra_slot = irix_guest_read64be(rt, outer_ra_addr);
            outer_ra = (uint32_t)outer_ra_slot;
        }
        if (irix_guest_ptr(rt, outer_this_addr, 4u))
            outer_this = irix_guest_read32be(rt, outer_this_addr);
        fprintf(stderr,
                "XRMSTR null-chain sp=0x%08x outer_ra_slot=0x%016llx outer_ra=0x%08x outer_call=0x%08x outer_this=0x%08x\n",
                sp, (unsigned long long)outer_ra_slot, outer_ra,
                outer_ra ? outer_ra - 8u : 0u, outer_this);
    }

    /* Use the process DISPLAY directly.  The guest Display is a synthetic
       shadow rather than a real SGI Xlib Display, so dereferencing fields from
       it here is both unnecessary and brittle.  XResourceManagerString is a
       server property query; opening the same DISPLAY gives the same value
       without exposing host pointers or depending on shadow layout. */
    dpy = XOpenDisplay(0);
    fprintf(stderr,
            "XRMSTR bridge XOpenDisplay=%p trapped=%d fault=0x%08x\n",
            (void *)dpy, rt->trapped, rt->fault_addr);
    if (dpy) {
        resource_string = XResourceManagerString(dpy);
        fprintf(stderr,
                "XRMSTR bridge resource_string=%p trapped=%d fault=0x%08x\n",
                (const void *)resource_string, rt->trapped, rt->fault_addr);
        out = copy_host_string_to_guest(rt, cpu, resource_string);
        fprintf(stderr,
                "XRMSTR bridge copied out=0x%08x trapped=%d fault=0x%08x\n",
                out, rt->trapped, rt->fault_addr);
        XCloseDisplay(dpy);
        fprintf(stderr,
                "XRMSTR bridge closed trapped=%d fault=0x%08x\n",
                rt->trapped, rt->fault_addr);
    }
    cpu->gpr[2] = out;
    fprintf(stderr,
            "XRMSTR bridge exit v0=0x%08x trapped=%d fault=0x%08x\n",
            out, rt->trapped, rt->fault_addr);
}

static void bridge_XAddConnectionWatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* Display*, callback, client_data. The callback is intentionally retained
       only semantically for now: no auxiliary host Xlib connections exist. */
    cpu->gpr[2] = cpu->gpr[5] ? 1u : 0u;
}

static void bridge_XRemoveConnectionWatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* XRemoveConnectionWatch returns void in Xlib. */
    cpu->gpr[2] = 0;
}

static void bridge_XInternalConnectionNumbers(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr fd_array_out = (uint32_t)cpu->gpr[5];
    IrixGuestAddr count_out = (uint32_t)cpu->gpr[6];

    /* No bridged extension currently owns an auxiliary Xlib transport FD. */
    if (fd_array_out)
        irix_guest_write32be(rt, fd_array_out, 0);
    if (count_out)
        irix_guest_write32be(rt, count_out, 0);
    cpu->gpr[2] = 1u;
}

static void bridge_XProcessInternalConnection(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    /* With zero internal connections there is nothing to dispatch. */
    cpu->gpr[2] = 0;
}

void irix_x11_connection_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_conn, 0, sizeof(g_conn));
    g_conn.rt = rt;
}

IrixGuestFunction irix_x11_connection_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XResourceManagerString")) return bridge_XResourceManagerString;
    if (!strcmp(name, "XAddConnectionWatch")) return bridge_XAddConnectionWatch;
    if (!strcmp(name, "XRemoveConnectionWatch")) return bridge_XRemoveConnectionWatch;
    if (!strcmp(name, "XInternalConnectionNumbers")) return bridge_XInternalConnectionNumbers;
    if (!strcmp(name, "XProcessInternalConnection")) return bridge_XProcessInternalConnection;
    return 0;
}
