#include "IrixX11ConnectionBridge.h"
#include "IrixX11BridgeExtraAccess.h"
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

    if (!s)
        return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX)
        return 0;

    malloc_fn = irix_libc_bridge_lookup("malloc");
    if (!malloc_fn)
        return 0;
    call = *cpu;
    call.gpr[4] = (uint32_t)n;
    malloc_fn(rt, &call);
    out = (IrixGuestAddr)(uint32_t)call.gpr[2];
    if (!out)
        return 0;
    dst = irix_guest_ptr(rt, out, n);
    if (!dst)
        return 0;
    memcpy(dst, s, n);
    return out;
}

static void bridge_XResourceManagerString(IrixGuestRuntime *rt,
                                          IrixGuestCpu *cpu)
{
    Display *dpy;
    const char *resource_string;
    IrixGuestAddr out = 0;

    /*
     * Stay on the same host Xlib connection that backs the guest Display.
     * Reopening DISPLAY here used to create yet another connection during Xt
     * resource initialization.  The resource-manager property is server-wide,
     * but keeping every bridge on the canonical Display avoids split request
     * serials and extension/visual state.
     */
    dpy = irix_x11_primary_display_from_guest((IrixGuestAddr)(uint32_t)cpu->gpr[4]);
    if (dpy) {
        resource_string = XResourceManagerString(dpy);
        out = copy_host_string_to_guest(rt, cpu, resource_string);
    } else {
        dpy = XOpenDisplay(0);
        if (dpy) {
            fprintf(stderr,
                    "IRIX XResourceManagerString: primary Display unavailable; temporary fallback\n");
            resource_string = XResourceManagerString(dpy);
            out = copy_host_string_to_guest(rt, cpu, resource_string);
            XCloseDisplay(dpy);
        }
    }
    cpu->gpr[2] = out;
}

static void bridge_XAddConnectionWatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = cpu->gpr[5] ? 1u : 0u;
}

static void bridge_XRemoveConnectionWatch(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
}

static void bridge_XInternalConnectionNumbers(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr fd_array_out = (uint32_t)cpu->gpr[5];
    IrixGuestAddr count_out = (uint32_t)cpu->gpr[6];

    if (fd_array_out)
        irix_guest_write32be(rt, fd_array_out, 0);
    if (count_out)
        irix_guest_write32be(rt, count_out, 0);
    cpu->gpr[2] = 1u;
}

static void bridge_XProcessInternalConnection(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
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
