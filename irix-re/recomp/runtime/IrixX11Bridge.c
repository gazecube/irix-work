#include "IrixX11Bridge.h"

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_XRM_MAX_DATABASES 256u
#define IRIX_XRM_HANDLE_BASE 0x72000000u

typedef struct IrixX11BridgeState {
    IrixGuestRuntime *rt;
    const char *const *fast_quark_names;
    size_t fast_quark_count;
    XrmDatabase databases[IRIX_XRM_MAX_DATABASES];
    size_t database_count;
} IrixX11BridgeState;

static IrixX11BridgeState g_bridge;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static const char *guest_quark_string(IrixGuestRuntime *rt, uint32_t encoded)
{
    if ((int32_t)encoded < 0) {
        uint32_t index = ~encoded;
        if (index >= g_bridge.fast_quark_count)
            return 0;
        return g_bridge.fast_quark_names[index];
    }
    return guest_cstr(rt, encoded);
}

static uint32_t read32(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_read32be(rt, addr);
}

static void write32(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    irix_guest_write32be(rt, addr, value);
}

static IrixGuestAddr database_to_guest(XrmDatabase db)
{
    size_t i;
    if (!db)
        return 0;
    for (i = 0; i < g_bridge.database_count; ++i)
        if (g_bridge.databases[i] == db)
            return IRIX_XRM_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
    if (g_bridge.database_count >= IRIX_XRM_MAX_DATABASES)
        return 0;
    i = g_bridge.database_count++;
    g_bridge.databases[i] = db;
    return IRIX_XRM_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
}

static XrmDatabase database_from_guest(IrixGuestAddr addr)
{
    uint32_t index;
    if (!addr || addr < IRIX_XRM_HANDLE_BASE)
        return 0;
    index = (addr - IRIX_XRM_HANDLE_BASE) / 4u;
    if (index >= g_bridge.database_count)
        return 0;
    return g_bridge.databases[index];
}

static void bridge_XrmInitialize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    XrmInitialize();
    cpu->gpr[2] = 0;
}

static void bridge_XrmPermStringToQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_quark_string(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = s ? (uint32_t)XrmPermStringToQuark(s) : 0;
}

static void bridge_XrmStringToQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_quark_string(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = s ? (uint32_t)XrmStringToQuark(s) : 0;
}

static void bridge_XrmGetFileDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = database_to_guest(path ? XrmGetFileDatabase(path) : 0);
}

static void bridge_XrmGetStringDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *text = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = database_to_guest(text ? XrmGetStringDatabase(text) : 0);
}

static void bridge_XrmMergeDatabases(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_ptr = (uint32_t)cpu->gpr[5];
    XrmDatabase src = database_from_guest((uint32_t)cpu->gpr[4]);
    XrmDatabase dst = database_from_guest(read32(rt, dst_ptr));
    XrmMergeDatabases(src, &dst);
    write32(rt, dst_ptr, database_to_guest(dst));
    cpu->gpr[2] = 0;
}

static void bridge_XrmCombineDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_ptr = (uint32_t)cpu->gpr[5];
    XrmDatabase src = database_from_guest((uint32_t)cpu->gpr[4]);
    XrmDatabase dst = database_from_guest(read32(rt, dst_ptr));
    XrmCombineDatabase(src, &dst, (Bool)cpu->gpr[6]);
    write32(rt, dst_ptr, database_to_guest(dst));
    cpu->gpr[2] = 0;
}

static void bridge_XrmCombineFileDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr dst_ptr = (uint32_t)cpu->gpr[5];
    XrmDatabase dst = database_from_guest(read32(rt, dst_ptr));
    Bool ok = path ? XrmCombineFileDatabase(path, &dst, (Bool)cpu->gpr[6]) : False;
    write32(rt, dst_ptr, database_to_guest(dst));
    cpu->gpr[2] = ok ? 1 : 0;
}

void irix_x11_bridge_init(IrixGuestRuntime *rt,
                          const char *const *fast_quark_names,
                          size_t fast_quark_count)
{
    memset(&g_bridge, 0, sizeof(g_bridge));
    g_bridge.rt = rt;
    g_bridge.fast_quark_names = fast_quark_names;
    g_bridge.fast_quark_count = fast_quark_count;
}

IrixGuestFunction irix_x11_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XrmInitialize")) return bridge_XrmInitialize;
    if (!strcmp(name, "XrmPermStringToQuark")) return bridge_XrmPermStringToQuark;
    if (!strcmp(name, "XrmStringToQuark")) return bridge_XrmStringToQuark;
    if (!strcmp(name, "XrmGetFileDatabase")) return bridge_XrmGetFileDatabase;
    if (!strcmp(name, "XrmGetStringDatabase")) return bridge_XrmGetStringDatabase;
    if (!strcmp(name, "XrmMergeDatabases")) return bridge_XrmMergeDatabases;
    if (!strcmp(name, "XrmCombineDatabase")) return bridge_XrmCombineDatabase;
    if (!strcmp(name, "XrmCombineFileDatabase")) return bridge_XrmCombineFileDatabase;
    return 0;
}
