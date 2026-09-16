#include "IrixX11ConnectionBridge.h"

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
    if (!strcmp(name, "XAddConnectionWatch")) return bridge_XAddConnectionWatch;
    if (!strcmp(name, "XRemoveConnectionWatch")) return bridge_XRemoveConnectionWatch;
    if (!strcmp(name, "XInternalConnectionNumbers")) return bridge_XInternalConnectionNumbers;
    if (!strcmp(name, "XProcessInternalConnection")) return bridge_XProcessInternalConnection;
    return 0;
}
