#include "IrixXrmCompat.h"

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_XRM_COMPAT_DB_BASE 0x72300000u
#define IRIX_XRM_COMPAT_SEARCH_BASE 0x72310000u
#define IRIX_XRM_COMPAT_MAX_DBS 256u
#define IRIX_XRM_COMPAT_MAX_SEARCH 4096u
#define IRIX_XRM_COMPAT_VALUE_BASE 0x74100000u
#define IRIX_XRM_COMPAT_VALUE_SIZE 0x00200000u

typedef struct {
    IrixGuestRuntime *rt;
    Display *display;
    XrmDatabase dbs[IRIX_XRM_COMPAT_MAX_DBS];
    size_t db_count;
    XrmHashTable searches[IRIX_XRM_COMPAT_MAX_SEARCH];
    size_t search_count;
    IrixGuestAddr value_cur;
} IrixXrmCompatState;

static IrixXrmCompatState g_xrm;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static uint32_t rd32(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_read32be(rt, addr);
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr addr, uint32_t value)
{
    if (addr) irix_guest_write32be(rt, addr, value);
}

static IrixGuestAddr value_alloc(uint32_t size)
{
    IrixGuestAddr a;
    uint32_t n = (size + 7u) & ~7u;
    if (!n || !g_xrm.value_cur || g_xrm.value_cur + n < g_xrm.value_cur ||
        g_xrm.value_cur + n > IRIX_XRM_COMPAT_VALUE_BASE + IRIX_XRM_COMPAT_VALUE_SIZE)
        return 0;
    a = g_xrm.value_cur;
    g_xrm.value_cur += n;
    return a;
}

static IrixGuestAddr copy_bytes(const void *src, uint32_t size)
{
    IrixGuestAddr a;
    void *dst;
    if (!src || !size) return 0;
    a = value_alloc(size);
    if (!a) return 0;
    dst = irix_guest_ptr(g_xrm.rt, a, size);
    if (!dst) return 0;
    memcpy(dst, src, size);
    return a;
}

static IrixGuestAddr copy_string(const char *s)
{
    size_t n;
    if (!s) return 0;
    n = strlen(s) + 1u;
    return n <= UINT32_MAX ? copy_bytes(s, (uint32_t)n) : 0;
}

static IrixGuestAddr db_to_guest(XrmDatabase db)
{
    size_t i;
    if (!db) return 0;
    for (i = 0; i < g_xrm.db_count; ++i)
        if (g_xrm.dbs[i] == db)
            return IRIX_XRM_COMPAT_DB_BASE + (IrixGuestAddr)(i * 4u);
    if (g_xrm.db_count >= IRIX_XRM_COMPAT_MAX_DBS) return 0;
    i = g_xrm.db_count++;
    g_xrm.dbs[i] = db;
    return IRIX_XRM_COMPAT_DB_BASE + (IrixGuestAddr)(i * 4u);
}

static XrmDatabase db_from_guest(IrixGuestAddr addr)
{
    uint32_t i;
    if (addr < IRIX_XRM_COMPAT_DB_BASE || addr >= IRIX_XRM_COMPAT_SEARCH_BASE) return 0;
    i = (addr - IRIX_XRM_COMPAT_DB_BASE) / 4u;
    return i < g_xrm.db_count ? g_xrm.dbs[i] : 0;
}

static IrixGuestAddr search_to_guest(XrmHashTable table)
{
    size_t i;
    if (!table) return 0;
    for (i = 0; i < g_xrm.search_count; ++i)
        if (g_xrm.searches[i] == table)
            return IRIX_XRM_COMPAT_SEARCH_BASE + (IrixGuestAddr)(i * 4u);
    if (g_xrm.search_count >= IRIX_XRM_COMPAT_MAX_SEARCH) return 0;
    i = g_xrm.search_count++;
    g_xrm.searches[i] = table;
    return IRIX_XRM_COMPAT_SEARCH_BASE + (IrixGuestAddr)(i * 4u);
}

static XrmHashTable search_from_guest(IrixGuestAddr addr)
{
    uint32_t i;
    if (addr < IRIX_XRM_COMPAT_SEARCH_BASE) return 0;
    i = (addr - IRIX_XRM_COMPAT_SEARCH_BASE) / 4u;
    return i < g_xrm.search_count ? g_xrm.searches[i] : 0;
}

static void write_value(IrixGuestRuntime *rt, IrixGuestAddr out, const XrmValue *value)
{
    IrixGuestAddr data = 0;
    if (!out) return;
    if (value && value->addr && value->size)
        data = copy_bytes(value->addr, (uint32_t)value->size);
    wr32(rt, out + 0u, value ? (uint32_t)value->size : 0u);
    wr32(rt, out + 4u, data);
}

static int read_quarks(IrixGuestRuntime *rt, IrixGuestAddr addr, XrmQuark *out, int cap)
{
    int i;
    if (!addr || cap < 2) return 0;
    for (i = 0; i < cap - 1; ++i) {
        uint32_t q = rd32(rt, addr + (IrixGuestAddr)(i * 4u));
        out[i] = (XrmQuark)q;
        if (!q) return i;
    }
    out[cap - 1] = 0;
    return cap - 1;
}

static Display *compat_display(void)
{
    if (!g_xrm.display) g_xrm.display = XOpenDisplay(NULL);
    return g_xrm.display;
}

Display *irix_xrm_compat_display(void)
{
    return compat_display();
}

int irix_xrm_compat_is_database_handle(IrixGuestRuntime *rt,
                                       IrixGuestAddr address)
{
    uint32_t index;
    if (g_xrm.rt != rt || address < IRIX_XRM_COMPAT_DB_BASE ||
        address >= IRIX_XRM_COMPAT_SEARCH_BASE ||
        ((address - IRIX_XRM_COMPAT_DB_BASE) & 3u) != 0u)
        return 0;
    index = (address - IRIX_XRM_COMPAT_DB_BASE) / 4u;
    return index < g_xrm.db_count && g_xrm.dbs[index] != 0;
}

static void bridge_XrmGetDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = compat_display();
    (void)rt;
    (void)cpu->gpr[4];
    cpu->gpr[2] = d ? db_to_guest(XrmGetDatabase(d)) : 0;
}

static void bridge_XrmSetDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = compat_display();
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[5]);
    (void)rt;
    (void)cpu->gpr[4];
    if (d) XrmSetDatabase(d, db);
    cpu->gpr[2] = 0;
}

static void bridge_XrmGetFileDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest_path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char host_path[4096];
    const char *path = irix_guest_resolve_path(rt, guest_path, host_path,
                                               sizeof(host_path),
                                               "resource:XrmGetFileDatabase", 1);
    XrmDatabase db = path ? XrmGetFileDatabase(path) : 0;
    cpu->gpr[2] = db_to_guest(db);
}

static void bridge_XrmGetStringDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *text = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = db_to_guest(text ? XrmGetStringDatabase(text) : 0);
}

static void bridge_XrmDestroyDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[4]);
    size_t i;
    (void)rt;
    if (db) XrmDestroyDatabase(db);
    for (i = 0; i < g_xrm.db_count; ++i)
        if (g_xrm.dbs[i] == db) g_xrm.dbs[i] = 0;
    cpu->gpr[2] = 0;
}

static void bridge_XrmMergeDatabases(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dstp = (uint32_t)cpu->gpr[5];
    XrmDatabase src = db_from_guest((uint32_t)cpu->gpr[4]);
    XrmDatabase dst = db_from_guest(rd32(rt, dstp));
    XrmMergeDatabases(src, &dst);
    wr32(rt, dstp, db_to_guest(dst));
    cpu->gpr[2] = 0;
}

static void bridge_XrmCombineDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dstp = (uint32_t)cpu->gpr[5];
    XrmDatabase src = db_from_guest((uint32_t)cpu->gpr[4]);
    XrmDatabase dst = db_from_guest(rd32(rt, dstp));
    XrmCombineDatabase(src, &dst, (Bool)cpu->gpr[6]);
    wr32(rt, dstp, db_to_guest(dst));
    cpu->gpr[2] = 0;
}

static void bridge_XrmCombineFileDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *guest_path = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    char host_path[4096];
    const char *path = irix_guest_resolve_path(rt, guest_path, host_path,
                                               sizeof(host_path),
                                               "resource:XrmCombineFileDatabase", 1);
    IrixGuestAddr dstp = (uint32_t)cpu->gpr[5];
    XrmDatabase dst = db_from_guest(rd32(rt, dstp));
    Bool ok = path ? XrmCombineFileDatabase(path, &dst, (Bool)cpu->gpr[6]) : False;
    wr32(rt, dstp, db_to_guest(dst));
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XrmQGetResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[4]);
    XrmQuark names[128], classes[128];
    XrmRepresentation type = 0;
    XrmValue value;
    Bool ok = False;
    memset(&value, 0, sizeof(value));
    if (db) {
        read_quarks(rt, (uint32_t)cpu->gpr[5], names, 128);
        read_quarks(rt, (uint32_t)cpu->gpr[6], classes, 128);
        ok = XrmQGetResource(db, names, classes, &type, &value);
    }
    wr32(rt, (uint32_t)cpu->gpr[7], ok ? (uint32_t)type : 0u);
    write_value(rt, (uint32_t)cpu->gpr[8], ok ? &value : 0);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XrmGetResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[4]);
    const char *name = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *class_name = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    char *type = 0;
    XrmValue value;
    Bool ok = False;
    memset(&value, 0, sizeof(value));
    if (db && name && class_name)
        ok = XrmGetResource(db, name, class_name, &type, &value);
    wr32(rt, (uint32_t)cpu->gpr[7], ok && type ? copy_string(type) : 0u);
    write_value(rt, (uint32_t)cpu->gpr[8], ok ? &value : 0);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XrmQGetSearchList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[4]);
    XrmQuark names[128], classes[128];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    int cap = (int32_t)cpu->gpr[8];
    XrmHashTable *list;
    Bool ok;
    int i;
    if (!db || !out || cap <= 0 || cap > 4096) { cpu->gpr[2] = 0; return; }
    read_quarks(rt, (uint32_t)cpu->gpr[5], names, 128);
    read_quarks(rt, (uint32_t)cpu->gpr[6], classes, 128);
    list = (XrmHashTable *)calloc((size_t)cap, sizeof(*list));
    if (!list) { cpu->gpr[2] = 0; return; }
    ok = XrmQGetSearchList(db, names, classes, list, cap);
    if (ok) {
        for (i = 0; i < cap; ++i) {
            wr32(rt, out + (IrixGuestAddr)(i * 4u), search_to_guest(list[i]));
            if (!list[i]) break;
        }
    }
    free(list);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XrmQGetSearchResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmHashTable list[256];
    XrmRepresentation type = 0;
    XrmValue value;
    IrixGuestAddr p = (uint32_t)cpu->gpr[4];
    Bool ok;
    int i;
    memset(&value, 0, sizeof(value));
    for (i = 0; i < 255; ++i) {
        IrixGuestAddr h = rd32(rt, p + (IrixGuestAddr)(i * 4u));
        list[i] = search_from_guest(h);
        if (!h || !list[i]) break;
    }
    list[i] = 0;
    ok = XrmQGetSearchResource(list, (XrmName)cpu->gpr[5], (XrmClass)cpu->gpr[6], &type, &value);
    wr32(rt, (uint32_t)cpu->gpr[7], ok ? (uint32_t)type : 0u);
    write_value(rt, (uint32_t)cpu->gpr[8], ok ? &value : 0);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XrmPutStringResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dbp = (uint32_t)cpu->gpr[4];
    XrmDatabase db = db_from_guest(rd32(rt, dbp));
    const char *spec = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *value = guest_cstr(rt, (uint32_t)cpu->gpr[6]);
    if (spec && value) XrmPutStringResource(&db, spec, value);
    wr32(rt, dbp, db_to_guest(db));
    cpu->gpr[2] = 0;
}

static void bridge_XrmPutLineResource(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dbp = (uint32_t)cpu->gpr[4];
    XrmDatabase db = db_from_guest(rd32(rt, dbp));
    const char *line = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    if (line) XrmPutLineResource(&db, line);
    wr32(rt, dbp, db_to_guest(db));
    cpu->gpr[2] = 0;
}

static void bridge_XrmPutFileDatabase(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    XrmDatabase db = db_from_guest((uint32_t)cpu->gpr[4]);
    const char *path = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    if (db && path) XrmPutFileDatabase(db, path);
    cpu->gpr[2] = 0;
}

void irix_xrm_compat_init(IrixGuestRuntime *rt)
{
    memset(&g_xrm, 0, sizeof(g_xrm));
    g_xrm.rt = rt;
    if (irix_guest_map(rt, IRIX_XRM_COMPAT_VALUE_BASE, IRIX_XRM_COMPAT_VALUE_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_XRM_COMPAT_VALUE_BASE, 1))
        g_xrm.value_cur = IRIX_XRM_COMPAT_VALUE_BASE;
}

IrixGuestFunction irix_xrm_compat_lookup(const char *name)
{
    if (!name) return 0;
#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(XrmGetDatabase);
    MAP(XrmSetDatabase);
    MAP(XrmGetFileDatabase);
    MAP(XrmGetStringDatabase);
    MAP(XrmDestroyDatabase);
    MAP(XrmMergeDatabases);
    MAP(XrmCombineDatabase);
    MAP(XrmCombineFileDatabase);
    MAP(XrmQGetResource);
    MAP(XrmGetResource);
    MAP(XrmQGetSearchList);
    MAP(XrmQGetSearchResource);
    MAP(XrmPutStringResource);
    MAP(XrmPutLineResource);
    MAP(XrmPutFileDatabase);
#undef MAP
    return 0;
}
