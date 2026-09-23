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

/* SGI FAST_QUARKS encodes selected string pointers as negative 32-bit values.
   _XQstring simply returns ordinary pointers unchanged, or indexes the SGI
   name table with ~encoded for a fast string.  XrmQuark itself remains an int
   and is a completely separate namespace. */
static const char *decode_qstring(IrixGuestRuntime *rt, uint32_t encoded)
{
    if ((int32_t)encoded >= 0)
        return guest_cstr(rt, (IrixGuestAddr)encoded);
    {
        uint32_t index = ~encoded;
        if (index >= g_quark.fast_quark_count || !g_quark.fast_quark_names)
            return 0;
        return g_quark.fast_quark_names[index];
    }
}

static IrixGuestAddr host_quark_string_to_guest(XrmQuark q)
{
    const char *s;
    size_t i;
    if (!q) return 0;
    for (i = 0; i < g_quark.cache_count; ++i)
        if (g_quark.cache[i].host_quark == q)
            return g_quark.cache[i].guest_string;
    s = XrmQuarkToString(q);
    if (!s) return 0;
    {
        IrixGuestAddr out = copy_string(s);
        if (!out) return 0;
        if (g_quark.cache_count < IRIX_XRM_QUARK_CACHE_MAX) {
            g_quark.cache[g_quark.cache_count].host_quark = q;
            g_quark.cache[g_quark.cache_count].guest_string = out;
            ++g_quark.cache_count;
        }
        return out;
    }
}

static void bridge_XQstring(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t encoded = (uint32_t)cpu->gpr[4];
    if ((int32_t)encoded >= 0) {
        cpu->gpr[2] = encoded;
        return;
    }
    cpu->gpr[2] = copy_string(decode_qstring(rt, encoded));
}

static void bridge_XQstrcmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t a = (uint32_t)cpu->gpr[4];
    uint32_t b = (uint32_t)cpu->gpr[5];
    const char *sa;
    const char *sb;

    if ((int32_t)a < 0 && (int32_t)b < 0 && a == b) {
        cpu->gpr[2] = 0;
        return;
    }
    sa = decode_qstring(rt, a);
    sb = decode_qstring(rt, b);
    if (!sa || !sb) {
        int rc = sa ? 1 : (sb ? -1 : 0);
        cpu->gpr[2] = (uint64_t)(int64_t)rc;
        return;
    }
    cpu->gpr[2] = (uint64_t)(int64_t)strcmp(sa, sb);
}

static uint32_t quarkify_encoded(IrixGuestRuntime *rt, uint32_t encoded)
{
    const char *s = decode_qstring(rt, encoded);
    return s ? (uint32_t)XrmStringToQuark(s) : 0u;
}

static void bridge_XrmStringToQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    cpu->gpr[2] = quarkify_encoded(rt, (uint32_t)cpu->gpr[4]);
}

static void bridge_XrmPermStringToQuark(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = decode_qstring(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = s ? (uint32_t)XrmPermStringToQuark(s) : 0;
}

static void bridge_XrmQuarkToString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = host_quark_string_to_guest((XrmQuark)(int32_t)cpu->gpr[4]);
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
    cpu->gpr[2] = (uint32_t)XrmStringToQuark(tmp);
    free(tmp);
}

static void bridge_XrmStringToQuarkList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = decode_qstring(rt, (uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    XrmQuark *list;
    size_t cap, i;
    if (!s || !out) { cpu->gpr[2] = 0; return; }
    cap = strlen(s) + 2u;
    list = (XrmQuark *)calloc(cap, sizeof(*list));
    if (!list) { cpu->gpr[2] = 0; return; }
    XrmStringToQuarkList(s, list);
    for (i = 0; i < cap; ++i) {
        irix_guest_write32be(rt, out + (IrixGuestAddr)(i * 4u), (uint32_t)list[i]);
        if (!list[i]) break;
    }
    free(list);
    cpu->gpr[2] = 0;
}

static void bridge_XrmStringToBindingQuarkList(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = decode_qstring(rt, (uint32_t)cpu->gpr[4]);
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
        irix_guest_write32be(rt, quarks_out + (IrixGuestAddr)(i * 4u), (uint32_t)quarks[i]);
        if (!quarks[i]) break;
    }
    free(bindings);
    free(quarks);
    cpu->gpr[2] = 0;
}

static void bridge_XrmRepresentationToString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_XrmQuarkToString(rt, cpu);
}

static void bridge_XrmStringToRepresentation(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    bridge_XrmStringToQuark(rt, cpu);
}

void irix_xrm_quark_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_quark, 0, sizeof(g_quark));
    g_quark.rt = rt;
    rt->xrm_quarkify_encoded = quarkify_encoded;
    if (irix_guest_map(rt, IRIX_XRM_QUARK_VALUE_BASE, IRIX_XRM_QUARK_VALUE_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_XRM_QUARK_VALUE_BASE, 1))
        g_quark.value_cur = IRIX_XRM_QUARK_VALUE_BASE;
}

void irix_xrm_quark_bridge_set_fast_table(const char *const *names, size_t count)
{
    g_quark.fast_quark_names = names;
    g_quark.fast_quark_count = count;
}

IrixGuestFunction irix_xrm_quark_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "_XQstring")) return bridge_XQstring;
    if (!strcmp(name, "_XQstrcmp")) return bridge_XQstrcmp;
    if (!strcmp(name, "_XrmBufferToQuark")) return bridge__XrmBufferToQuark;

#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(XrmStringToQuark);
    MAP(XrmPermStringToQuark);
    MAP(XrmQuarkToString);
    MAP(XrmUniqueQuark);
    MAP(XrmStringToQuarkList);
    MAP(XrmStringToBindingQuarkList);
    MAP(XrmRepresentationToString);
    MAP(XrmStringToRepresentation);
#undef MAP
    return 0;
}
