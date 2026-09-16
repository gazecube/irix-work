#include "IrixX11Bridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_XRM_MAX_DATABASES 256u
#define IRIX_XRM_HANDLE_BASE 0x72000000u
#define IRIX_XRM_SEARCH_HANDLE_BASE 0x72100000u
#define IRIX_REGION_HANDLE_BASE 0x72200000u
#define IRIX_REGION_MAX_HANDLES 1024u
#define IRIX_XRM_MAX_SEARCH_ENTRIES 4096u
#define IRIX_XRM_VALUE_BASE 0x73000000u
#define IRIX_XRM_VALUE_SIZE 0x01000000u

typedef struct IrixX11BridgeState {
    IrixGuestRuntime *rt;
    const char *const *fast_quark_names;
    size_t fast_quark_count;
    XrmDatabase databases[IRIX_XRM_MAX_DATABASES];
    size_t database_count;
    XrmHashTable search_entries[IRIX_XRM_MAX_SEARCH_ENTRIES];
    size_t search_entry_count;
    Region regions[IRIX_REGION_MAX_HANDLES];
    size_t region_count;
    IrixGuestAddr value_cur;
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

static int read_quark_list(IrixGuestRuntime *rt, IrixGuestAddr addr, XrmQuark *out, int cap)
{
    int i;
    if (!addr || cap < 1) return 0;
    for (i = 0; i < cap - 1; ++i) {
        uint32_t q = read32(rt, addr + (IrixGuestAddr)(i * 4));
        out[i] = (XrmQuark)q;
        if (!q) return i;
    }
    out[cap - 1] = 0;
    return cap - 1;
}

static IrixGuestAddr search_entry_to_guest(XrmHashTable entry)
{
    size_t i;
    if (!entry) return 0;
    for (i = 0; i < g_bridge.search_entry_count; ++i)
        if (g_bridge.search_entries[i] == entry)
            return IRIX_XRM_SEARCH_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
    if (g_bridge.search_entry_count >= IRIX_XRM_MAX_SEARCH_ENTRIES)
        return 0;
    i = g_bridge.search_entry_count++;
    g_bridge.search_entries[i] = entry;
    return IRIX_XRM_SEARCH_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
}

static void bridge_XrmQGetSearchList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = database_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr names_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr classes_addr = (uint32_t)cpu->gpr[6];
    IrixGuestAddr list_addr = (uint32_t)cpu->gpr[7];
    int list_length = (int32_t)cpu->gpr[8];
    XrmQuark names[128], classes[128];
    XrmHashTable *host_list;
    Bool ok;
    int i;

    if (!list_addr || list_length <= 0 || list_length > 4096) {
        cpu->gpr[2] = 0;
        return;
    }
    if (!db) {
        write32(rt, list_addr, 0);
        cpu->gpr[2] = 1;
        return;
    }
    read_quark_list(rt, names_addr, names, 128);
    read_quark_list(rt, classes_addr, classes, 128);
    host_list = (XrmHashTable *)calloc((size_t)list_length, sizeof(*host_list));
    if (!host_list) { cpu->gpr[2] = 0; return; }
    ok = XrmQGetSearchList(db, names, classes, host_list, list_length);
    if (ok) {
        for (i = 0; i < list_length; ++i) {
            IrixGuestAddr h = search_entry_to_guest(host_list[i]);
            write32(rt, list_addr + (IrixGuestAddr)(i * 4), h);
            if (!host_list[i]) break;
        }
    }
    free(host_list);
    cpu->gpr[2] = ok ? 1 : 0;
}

static XrmHashTable search_entry_from_guest(IrixGuestAddr addr)
{
    uint32_t index;
    if (!addr || addr < IRIX_XRM_SEARCH_HANDLE_BASE) return 0;
    index = (addr - IRIX_XRM_SEARCH_HANDLE_BASE) / 4u;
    if (index >= g_bridge.search_entry_count) return 0;
    return g_bridge.search_entries[index];
}

static IrixGuestAddr region_to_guest(Region region)
{
    size_t i;
    if (!region) return 0;
    for (i = 0; i < g_bridge.region_count; ++i)
        if (g_bridge.regions[i] == region)
            return IRIX_REGION_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
    if (g_bridge.region_count >= IRIX_REGION_MAX_HANDLES) return 0;
    i = g_bridge.region_count++;
    g_bridge.regions[i] = region;
    return IRIX_REGION_HANDLE_BASE + (IrixGuestAddr)(i * 4u);
}

static Region region_from_guest(IrixGuestAddr addr)
{
    uint32_t index;
    if (!addr || addr < IRIX_REGION_HANDLE_BASE) return 0;
    index = (addr - IRIX_REGION_HANDLE_BASE) / 4u;
    if (index >= g_bridge.region_count) return 0;
    return g_bridge.regions[index];
}

static IrixGuestAddr value_alloc(IrixGuestRuntime *rt, uint32_t size)
{
    IrixGuestAddr a;
    uint32_t n = (size + 7u) & ~7u;
    if (!n || g_bridge.value_cur + n < g_bridge.value_cur ||
        g_bridge.value_cur + n > IRIX_XRM_VALUE_BASE + IRIX_XRM_VALUE_SIZE)
        return 0;
    a = g_bridge.value_cur;
    g_bridge.value_cur += n;
    if (!irix_guest_ptr(rt, a, n)) return 0;
    return a;
}

static void write_xrm_value(IrixGuestRuntime *rt, IrixGuestAddr out, const XrmValue *value)
{
    IrixGuestAddr data = 0;
    if (!out) return;
    if (value && value->addr && value->size) {
        data = value_alloc(rt, (uint32_t)value->size);
        if (data) {
            void *dst = irix_guest_ptr(rt, data, value->size);
            if (dst) memcpy(dst, value->addr, value->size);
        }
    }
    write32(rt, out, value ? (uint32_t)value->size : 0);
    write32(rt, out + 4, data);
}

static void bridge_XrmQGetSearchResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr list_addr = (uint32_t)cpu->gpr[4];
    XrmHashTable host_list[256];
    XrmRepresentation type = 0;
    XrmValue value;
    Bool ok;
    int i;
    memset(&value, 0, sizeof(value));
    for (i = 0; i < 255; ++i) {
        IrixGuestAddr handle = read32(rt, list_addr + (IrixGuestAddr)(i * 4));
        host_list[i] = search_entry_from_guest(handle);
        if (!handle || !host_list[i]) break;
    }
    host_list[i] = 0;
    ok = XrmQGetSearchResource(host_list, (XrmName)cpu->gpr[5],
                               (XrmClass)cpu->gpr[6], &type, &value);
    if ((uint32_t)cpu->gpr[7]) write32(rt, (uint32_t)cpu->gpr[7], (uint32_t)type);
    if ((uint32_t)cpu->gpr[8]) write_xrm_value(rt, (uint32_t)cpu->gpr[8], ok ? &value : 0);
    cpu->gpr[2] = ok ? 1 : 0;
}

static void bridge_XAllocSizeHints(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = value_alloc(rt, 72u);
    if (addr) {
        void *p = irix_guest_ptr(rt, addr, 72u);
        if (p) memset(p, 0, 72u);
    }
    cpu->gpr[2] = addr;
}

static void bridge_XCreateRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = region_to_guest(XCreateRegion());
}

static void bridge_XDestroyRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    if (r) XDestroyRegion(r);
    cpu->gpr[2] = 0;
}

static void bridge_XUnionRectWithRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr rect_addr = (uint32_t)cpu->gpr[4];
    Region src = region_from_guest((uint32_t)cpu->gpr[5]);
    Region dst = region_from_guest((uint32_t)cpu->gpr[6]);
    XRectangle rect;
    rect.x = (short)irix_guest_read16be(rt, rect_addr + 0);
    rect.y = (short)irix_guest_read16be(rt, rect_addr + 2);
    rect.width = irix_guest_read16be(rt, rect_addr + 4);
    rect.height = irix_guest_read16be(rt, rect_addr + 6);
    cpu->gpr[2] = (src && dst) ? (uint64_t)XUnionRectWithRegion(&rect, src, dst) : 0;
}

static void bridge_XUnionRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region a = region_from_guest((uint32_t)cpu->gpr[4]);
    Region b = region_from_guest((uint32_t)cpu->gpr[5]);
    Region dst = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (a && b && dst) ? (uint64_t)XUnionRegion(a, b, dst) : 0;
}

static void bridge_XIntersectRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region a = region_from_guest((uint32_t)cpu->gpr[4]);
    Region b = region_from_guest((uint32_t)cpu->gpr[5]);
    Region dst = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (a && b && dst) ? (uint64_t)XIntersectRegion(a, b, dst) : 0;
}

static void bridge_XSubtractRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region a = region_from_guest((uint32_t)cpu->gpr[4]);
    Region b = region_from_guest((uint32_t)cpu->gpr[5]);
    Region dst = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (a && b && dst) ? (uint64_t)XSubtractRegion(a, b, dst) : 0;
}

static void bridge_XXorRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region a = region_from_guest((uint32_t)cpu->gpr[4]);
    Region b = region_from_guest((uint32_t)cpu->gpr[5]);
    Region dst = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (a && b && dst) ? (uint64_t)XXorRegion(a, b, dst) : 0;
}

static void bridge_XOffsetRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = r ? (uint64_t)XOffsetRegion(r, (int)cpu->gpr[5], (int)cpu->gpr[6]) : 0;
}

static void bridge_XShrinkRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = r ? (uint64_t)XShrinkRegion(r, (int)cpu->gpr[5], (int)cpu->gpr[6]) : 0;
}

static void bridge_XEmptyRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = r ? (uint64_t)XEmptyRegion(r) : 1;
}

static void bridge_XEqualRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region a = region_from_guest((uint32_t)cpu->gpr[4]);
    Region b = region_from_guest((uint32_t)cpu->gpr[5]);
    (void)rt;
    cpu->gpr[2] = (a && b) ? (uint64_t)XEqualRegion(a, b) : 0;
}

static void bridge_XPointInRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = r ? (uint64_t)XPointInRegion(r, (int)cpu->gpr[5], (int)cpu->gpr[6]) : 0;
}

static void bridge_XRectInRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    (void)rt;
    cpu->gpr[2] = r ? (uint64_t)XRectInRegion(r, (int)cpu->gpr[5], (int)cpu->gpr[6],
                                               (unsigned)cpu->gpr[7], (unsigned)cpu->gpr[8]) : RectangleOut;
}

static void bridge_XClipBox(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Region r = region_from_guest((uint32_t)cpu->gpr[4]);
    IrixGuestAddr rect_addr = (uint32_t)cpu->gpr[5];
    XRectangle rect;
    int rc = r ? XClipBox(r, &rect) : 0;
    if (r && rect_addr) {
        irix_guest_write16be(rt, rect_addr + 0, (uint16_t)rect.x);
        irix_guest_write16be(rt, rect_addr + 2, (uint16_t)rect.y);
        irix_guest_write16be(rt, rect_addr + 4, rect.width);
        irix_guest_write16be(rt, rect_addr + 6, rect.height);
    }
    cpu->gpr[2] = (uint64_t)rc;
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
    if (irix_guest_map(rt, IRIX_XRM_VALUE_BASE, IRIX_XRM_VALUE_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_XRM_VALUE_BASE, 1))
        g_bridge.value_cur = IRIX_XRM_VALUE_BASE;
}

IrixGuestFunction irix_x11_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XAllocSizeHints")) return bridge_XAllocSizeHints;
    if (!strcmp(name, "XCreateRegion")) return bridge_XCreateRegion;
    if (!strcmp(name, "XDestroyRegion")) return bridge_XDestroyRegion;
    if (!strcmp(name, "XUnionRectWithRegion")) return bridge_XUnionRectWithRegion;
    if (!strcmp(name, "XUnionRegion")) return bridge_XUnionRegion;
    if (!strcmp(name, "XIntersectRegion")) return bridge_XIntersectRegion;
    if (!strcmp(name, "XSubtractRegion")) return bridge_XSubtractRegion;
    if (!strcmp(name, "XXorRegion")) return bridge_XXorRegion;
    if (!strcmp(name, "XOffsetRegion")) return bridge_XOffsetRegion;
    if (!strcmp(name, "XShrinkRegion")) return bridge_XShrinkRegion;
    if (!strcmp(name, "XEmptyRegion")) return bridge_XEmptyRegion;
    if (!strcmp(name, "XEqualRegion")) return bridge_XEqualRegion;
    if (!strcmp(name, "XPointInRegion")) return bridge_XPointInRegion;
    if (!strcmp(name, "XRectInRegion")) return bridge_XRectInRegion;
    if (!strcmp(name, "XClipBox")) return bridge_XClipBox;
    if (!strcmp(name, "XrmInitialize")) return bridge_XrmInitialize;
    if (!strcmp(name, "XrmPermStringToQuark")) return bridge_XrmPermStringToQuark;
    if (!strcmp(name, "XrmStringToQuark")) return bridge_XrmStringToQuark;
    if (!strcmp(name, "XrmQGetSearchList")) return bridge_XrmQGetSearchList;
    if (!strcmp(name, "XrmQGetSearchResource")) return bridge_XrmQGetSearchResource;
    if (!strcmp(name, "XrmGetFileDatabase")) return bridge_XrmGetFileDatabase;
    if (!strcmp(name, "XrmGetStringDatabase")) return bridge_XrmGetStringDatabase;
    if (!strcmp(name, "XrmMergeDatabases")) return bridge_XrmMergeDatabases;
    if (!strcmp(name, "XrmCombineDatabase")) return bridge_XrmCombineDatabase;
    if (!strcmp(name, "XrmCombineFileDatabase")) return bridge_XrmCombineFileDatabase;
    return 0;
}
