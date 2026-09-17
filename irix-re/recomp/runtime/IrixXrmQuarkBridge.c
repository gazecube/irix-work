#include "IrixXrmQuarkBridge.h"

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_XRM_QUARK_VALUE_BASE 0x74300000u
#define IRIX_XRM_QUARK_VALUE_SIZE 0x00100000u
#define IRIX_XRM_QUARK_CACHE_MAX 4096u

typedef struct {
    XrmQuark host_quark;
    uint32_t guest_quark;
    IrixGuestAddr guest_string;
} IrixXrmQuarkCacheEntry;

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr value_cur;
    const char *const *fast_quark_names;
    size_t fast_quark_count;
    IrixXrmQuarkCacheEntry cache[IRIX_XRM_QUARK_CACHE_MAX];
    size_t cache_count;
} IrixXrmQuarkBridgeState;

static IrixXrmQuarkBridgeState g_quark;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1) : 0;
}

static IrixGuestAddr alloc_guest(uint32_t size)
{
    IrixGuestAddr out;
    uint32_t n = (size + 7u) & ~7u;
    if (!n || !g_quark.value_cur ||
        g_quark.value_cur + n < g_quark.value_cur ||
        g_quark.value_cur + n > IRIX_XRM_QUARK_VALUE_BASE + IRIX_XRM_QUARK_VALUE_SIZE)
        return 0;
    out = g_quark.value_cur;
    g_quark.value_cur += n;
    return out;
}

static IrixGuestAddr copy_string(const char *s)
{
    IrixGuestAddr out;
    size_t n;
    void *dst;
    if (!s) return 0;
    n = strlen(s) + 1u;
    if (n > UINT32_MAX) return 0;
    out = alloc_guest((uint32_t)n);
    if (!out) return 0;
    dst = irix_guest_ptr(g_quark.rt, out, n);
    if (!dst) return 0;
    memcpy(dst, s, n);
    return out;
}

static int fast_quark_index(const char *s, size_t *index_out)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < g_quark.fast_quark_count; ++i) {
        const char *q = g_quark.fast_quark_names[i];
        if (q && strcmp(q, s) == 0) {
            if (index_out) *index_out = i;
            return 1;
        }
    }
    return 0;
}

static IrixGuestAddr guest_quark_to_string(uint32_t q)
{
    size_t i;
    if (!q) return 0;

    if ((int32_t)q >= 0) {
        return guest_cstr(g_quark.rt, (IrixGuestAddr)q) ? (IrixGuestAddr)q : 0;
    }

    for (i = 0; i < g_quark.cache_count; ++i)
        if (g_quark.cache[i].guest_quark == q && g_quark.cache[i].guest_string)
            return g_quark.cache[i].guest_string;

    {
        uint32_t index = ~q;
        IrixGuestAddr saddr;
        if (index >= g_quark.fast_quark_count || !g_quark.fast_quark_names[index])
            return 0;
        saddr = copy_string(g_quark.fast_quark_names[index]);
        if (!saddr) return 0;
        if (g_quark.cache_count < IRIX_XRM_QUARK_CACHE_MAX) {
            g_quark.cache[g_quark.cache_count].host_quark = 0;
            g_quark.cache[g_quark.cache_count].guest_quark = q;
            g_quark.cache[g_quark.cache_count].guest_string = saddr;
            ++g_quark.cache_count;
        }
        return saddr;
    }
}

static uint32_t intern_guest_quark(const char *s)
{
    XrmQuark host_q;
    size_t i, index;
    IrixGuestAddr guest_s;

    if (!s) return 0;
    if (fast_quark_index(s, &index))
        return ~((uint32_t)index);

    host_q = XrmStringToQuark(s);
    for (i = 0; i < g_quark.cache_count; ++i)
        if (g_quark.cache[i].host_quark == host_q && g_quark.cache[i].guest_quark)
            return g_quark.cache[i].guest_quark;

    guest_s = copy_string(s);
    if (!guest_s) return 0;
    if (g_quark.cache_count < IRIX_XRM_QUARK_CACHE_MAX) {
        g_quark.cache[g_quark.cache_count].host_quark = host_q;
        g_quark.cache[g_quark.cache_count].guest_quark = guest_s;
        g_quark.cache[g_quark.cache_count].guest_string = guest_s;
        ++g_quark.cache_count;
    }
    return guest_s;
}

static uint32_t host_quark_to_guest(XrmQuark q)
{
    const char *s;
    if (!q) return 0;
    s = XrmQuarkToString(q);
    return s ? intern_guest_quark(s) : 0;
}

static void bridge_XrmQuarkToString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = guest_quark_to_string((uint32_t)cpu->gpr[4]);
}

static void bridge_XrmUniqueQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)XrmUniqueQuark();
}

static void bridge__XrmBufferToQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    const char *src;
    char *tmp;

    if (!addr || len > 0x01000000u) {
        cpu->gpr[2] = 0;
        return;
    }
    src = (const char *)irix_guest_ptr(rt, addr, len ? len : 1u);
    if (!src || rt->trapped) {
        cpu->gpr[2] = 0;
        return;
    }
    tmp = (char *)malloc((size_t)len + 1u);
    if (!tmp) {
        cpu->gpr[2] = 0;
        return;
    }
    if (len) memcpy(tmp, src, len);
    tmp[len] = '\0';
    cpu->gpr[2] = intern_guest_quark(tmp);
    free(tmp);
}

static void bridge_XrmStringToQuarkList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    XrmQuark *list;
    size_t cap, i;
    if (!s || !out) { cpu->gpr[2] = 0; return; }
    cap = strlen(s) + 2u;
    list = (XrmQuark *)calloc(cap, sizeof(*list));
    if (!list) { cpu->gpr[2] = 0; return; }
    XrmStringToQuarkList(s, list);
    for (i = 0; i < cap; ++i) {
        uint32_t q = host_quark_to_guest(list[i]);
        irix_guest_write32be(rt, out + (IrixGuestAddr)(i * 4u), q);
        if (!list[i]) break;
    }
    free(list);
    cpu->gpr[2] = 0;
}

static void bridge_XrmStringToBindingQuarkList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr bindings_out = (uint32_t)cpu->gpr[5];
    IrixGuestAddr quarks_out = (uint32_t)cpu->gpr[6];
    XrmBinding *bindings;
    XrmQuark *quarks;
    size_t cap, i;
    if (!s || !bindings_out || !quarks_out) { cpu->gpr[2] = 0; return; }
    cap = strlen(s) + 2u;
    bindings = (XrmBinding *)calloc(cap, sizeof(*bindings));
    quarks = (XrmQuark *)calloc(cap, sizeof(*quarks));
    if (!bindings || !quarks) {
        free(bindings);
        free(quarks);
        cpu->gpr[2] = 0;
        return;
    }
    XrmStringToBindingQuarkList(s, bindings, quarks);
    for (i = 0; i < cap; ++i) {
        irix_guest_write32be(rt, bindings_out + (IrixGuestAddr)(i * 4u), (uint32_t)bindings[i]);
        irix_guest_write32be(rt, quarks_out + (IrixGuestAddr)(i * 4u), host_quark_to_guest(quarks[i]));
        if (!quarks[i]) break;
    }
    free(bindings);
    free(quarks);
    cpu->gpr[2] = 0;
}

static void bridge_XrmRepresentationToString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = guest_quark_to_string((uint32_t)cpu->gpr[4]);
}

static void bridge_XrmStringToRepresentation(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = s ? intern_guest_quark(s) : 0;
}

void irix_xrm_quark_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_quark, 0, sizeof(g_quark));
    g_quark.rt = rt;
    if (irix_guest_map(rt, IRIX_XRM_QUARK_VALUE_BASE, IRIX_XRM_QUARK_VALUE_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_XRM_QUARK_VALUE_BASE, 1))
        g_quark.value_cur = IRIX_XRM_QUARK_VALUE_BASE;
}

IrixGuestFunction irix_xrm_quark_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "_XQstring")) return bridge_XrmQuarkToString;
    if (!strcmp(name, "_XrmBufferToQuark")) return bridge__XrmBufferToQuark;

#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(XrmQuarkToString);
    MAP(XrmUniqueQuark);
    MAP(XrmStringToQuarkList);
    MAP(XrmStringToBindingQuarkList);
    MAP(XrmRepresentationToString);
    MAP(XrmStringToRepresentation);
#undef MAP
    return 0;
}
