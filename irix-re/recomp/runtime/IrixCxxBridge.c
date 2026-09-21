#include "IrixCxxBridge.h"
#include "IrixLibcBridge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static IrixGuestRuntime *g_rt;

#define IRIX_OSTRSTREAM_SLOTS 32u
#define IRIX_OSTRSTREAM_TEXT 256u

typedef struct IrixOstrstreamState {
    IrixGuestAddr object;
    IrixGuestAddr ostream_object;
    char text[IRIX_OSTRSTREAM_TEXT];
    size_t len;
    int used;
} IrixOstrstreamState;

static IrixOstrstreamState g_ostrstreams[IRIX_OSTRSTREAM_SLOTS];

static IrixOstrstreamState *ostrstream_find(IrixGuestAddr object)
{
    unsigned i;
    for (i = 0; i < IRIX_OSTRSTREAM_SLOTS; ++i)
        if (g_ostrstreams[i].used &&
            (g_ostrstreams[i].object == object ||
             g_ostrstreams[i].ostream_object == object))
            return &g_ostrstreams[i];
    return 0;
}

static IrixOstrstreamState *ostrstream_create(IrixGuestAddr object)
{
    IrixOstrstreamState *st = ostrstream_find(object);
    unsigned i;

    if (st) {
        st->len = 0;
        st->text[0] = '\0';
        return st;
    }

    for (i = 0; i < IRIX_OSTRSTREAM_SLOTS; ++i) {
        if (!g_ostrstreams[i].used) {
            st = &g_ostrstreams[i];
            memset(st, 0, sizeof(*st));
            st->used = 1;
            st->object = object;
            /*
             * Observed IRIX libCio layout: the ostream base subobject used
             * by ostrstream::operator<< is this+0x44.
             */
            st->ostream_object = object + 0x44u;
            return st;
        }
    }
    return 0;
}

static void ostrstream_destroy(IrixGuestAddr object)
{
    IrixOstrstreamState *st = ostrstream_find(object);
    if (st)
        memset(st, 0, sizeof(*st));
}

static IrixGuestAddr guest_malloc(IrixGuestRuntime *rt, IrixGuestCpu *caller,
                                  uint32_t size)
{
    IrixGuestFunction malloc_fn = irix_libc_bridge_lookup("malloc");
    IrixGuestCpu call;

    if (!malloc_fn || !size)
        return 0;
    call = *caller;
    call.gpr[4] = size;
    malloc_fn(rt, &call);
    return (IrixGuestAddr)(uint32_t)call.gpr[2];
}

static void bridge_ostrstream_ctor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    (void)rt;

    if (!object || !ostrstream_create(object)) {
        irix_guest_trap(rt, cpu, 0xeeee2201u);
        return;
    }
    cpu->gpr[2] = object;
}

static void bridge_ostream_insert_long(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    int32_t value = (int32_t)(uint32_t)cpu->gpr[5];
    IrixOstrstreamState *st = ostrstream_find(object);
    int n;

    if (!st) {
        irix_guest_trap(rt, cpu, 0xeeee2202u);
        return;
    }

    n = snprintf(st->text + st->len, sizeof(st->text) - st->len,
                 "%ld", (long)value);
    if (n < 0 || (size_t)n >= sizeof(st->text) - st->len) {
        irix_guest_trap(rt, cpu, 0xeeee2203u);
        return;
    }
    st->len += (size_t)n;
    cpu->gpr[2] = object;
}

static void bridge_ostream_insert_cstr(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixGuestAddr string_addr = (IrixGuestAddr)(uint32_t)cpu->gpr[5];
    IrixOstrstreamState *st = ostrstream_find(object);
    const char *s = (const char *)irix_guest_ptr(rt, string_addr, 1);
    size_t n;

    if (!s) {
        irix_guest_trap(rt, cpu, 0xeeee2206u);
        return;
    }

    /*
     * Only ostrstream instances are emulated here.  This symbol is also the
     * ordinary ostream::operator<<(const char *) entry point, so callers such
     * as cerr/cout must be allowed to use the real translated libCio body.
     * Returning without claiming the call lets the guest implementation run.
     */
    if (!st) {
        return;
    }

    n = strlen(s);
    if (n >= sizeof(st->text) - st->len) {
        irix_guest_trap(rt, cpu, 0xeeee2207u);
        return;
    }

    memcpy(st->text + st->len, s, n + 1u);
    st->len += n;
    cpu->gpr[2] = object;
}

static void bridge_ostrstream_str(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    IrixOstrstreamState *st = ostrstream_find(object);
    IrixGuestAddr out;
    char *dst;

    if (!st) {
        irix_guest_trap(rt, cpu, 0xeeee2204u);
        return;
    }

    out = guest_malloc(rt, cpu, (uint32_t)st->len + 1u);
    if (!out) {
        cpu->gpr[2] = 0;
        return;
    }
    dst = (char *)irix_guest_ptr(rt, out, st->len + 1u);
    if (!dst) {
        irix_guest_trap(rt, cpu, 0xeeee2205u);
        return;
    }
    memcpy(dst, st->text, st->len + 1u);
    cpu->gpr[2] = out;
}

static void bridge_ostrstream_dtor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (IrixGuestAddr)(uint32_t)cpu->gpr[4];
    (void)rt;
    ostrstream_destroy(object);
    cpu->gpr[2] = object;
}

static int matches_imd_dictionary_slot_dtor(IrixGuestRuntime *rt,
                                            IrixGuestAddr dtor)
{
    static const uint32_t words[] = {
        0x27bdffd0u, 0xffb00008u, 0x00808025u, 0xffa50000u,
        0xffbc0010u, 0x3c010002u, 0x2421e4b0u, 0x10800014u,
        0x0321e021u, 0x8e070000u, 0x10e0000au, 0xdfa60000u,
        0x8ce40000u, 0x24050003u, 0x8c990014u, 0x8c84000cu,
        0xffbf0018u, 0x0320f809u, 0x00872021u, 0xdfbf0018u,
        0xdfa60000u, 0x30c60001u, 0x10c00005u, 0x8f998070u,
        0xffbf0018u, 0x0320f809u, 0x02002025u, 0xdfbf0018u,
        0xdfbc0010u, 0xdfb00008u, 0x03e00008u, 0x27bd0030u
    };
    size_t i;

    if (!dtor || !irix_guest_ptr(rt, dtor, sizeof(words)))
        return 0;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (irix_guest_read32be(rt, dtor + (IrixGuestAddr)(i * 4u)) != words[i])
            return 0;
    return 1;
}

static int call_imd_dictionary_slot_dtor(IrixGuestRuntime *rt,
                                         const IrixGuestCpu *caller,
                                         IrixGuestAddr object,
                                         uint32_t dtor_flag)
{
    IrixGuestAddr entry;
    IrixGuestAddr table;
    IrixGuestAddr method;
    IrixGuestAddr adjusted;
    IrixGuestCpu call;
    IrixGuestAddr saved_pc;

    if (!object)
        return 0;
    if (!irix_guest_ptr(rt, object, 4u))
        return -1;

    entry = irix_guest_read32be(rt, object);
    if (entry) {
        if (!irix_guest_ptr(rt, entry, 4u))
            return -1;
        table = irix_guest_read32be(rt, entry);
        if (!table || !irix_guest_ptr(rt, table + 20u, 4u))
            return -1;

        method = irix_guest_read32be(rt, table + 20u);
        adjusted = entry + irix_guest_read32be(rt, table + 12u);
        if (!method)
            return -1;

        call = *caller;
        call.gpr[4] = (uint64_t)(int64_t)(int32_t)adjusted;
        call.gpr[5] = 3u;
        call.gpr[6] = (uint64_t)(int64_t)(int32_t)dtor_flag;
        call.gpr[25] = (uint64_t)(int64_t)(int32_t)method;
        saved_pc = rt->current_pc;
        if (irix_guest_call(rt, &call, method) != 0) {
            rt->current_pc = saved_pc;
            return -1;
        }
        rt->current_pc = saved_pc;
    }

    if (dtor_flag & 1u) {
        IrixGuestFunction free_fn = irix_libc_bridge_lookup("free");
        if (free_fn) {
            call = *caller;
            call.gpr[4] = (uint64_t)(int64_t)(int32_t)object;
            free_fn(rt, &call);
        }
    }
    return 0;
}

static int call_guest_dtor(IrixGuestRuntime *rt, const IrixGuestCpu *caller,
                           IrixGuestAddr dtor, IrixGuestAddr object,
                           uint32_t dtor_flag)
{
    IrixGuestCpu call = *caller;
    IrixGuestAddr saved_pc = rt->current_pc;

    if (matches_imd_dictionary_slot_dtor(rt, dtor))
        return call_imd_dictionary_slot_dtor(rt, caller, object, dtor_flag);

    call.gpr[4] = (uint64_t)(int64_t)(int32_t)object;
    call.gpr[5] = (uint64_t)(int64_t)(int32_t)dtor_flag;
    call.gpr[25] = (uint64_t)(int64_t)(int32_t)dtor;
    if (irix_guest_call(rt, &call, dtor) != 0) {
        rt->current_pc = saved_pc;
        return -1;
    }
    rt->current_pc = saved_pc;
    return 0;
}

static int validate_vec_count(IrixGuestRuntime *rt, IrixGuestAddr array,
                              uint32_t element_size, uint32_t count)
{
    uint64_t total;

    if (!array || !element_size || !count || count > 0x00100000u)
        return -1;
    total = (uint64_t)count * (uint64_t)element_size;
    if (total > UINT32_MAX)
        return -1;
    if (!irix_guest_ptr(rt, array, (size_t)total))
        return -1;
    return 0;
}

static int recover_vec_count(IrixGuestRuntime *rt, IrixGuestAddr array,
                             uint32_t element_size, uint32_t *count_out,
                             uint32_t *cookie_back_out)
{
    static const uint32_t backs[] = {4u, 8u, 12u, 16u};
    size_t i;

    if (!array || !element_size || !count_out || !cookie_back_out)
        return -1;

    for (i = 0; i < sizeof(backs) / sizeof(backs[0]); ++i) {
        uint32_t back = backs[i];
        IrixGuestAddr cookie;
        uint32_t count;

        if (array < back)
            continue;
        cookie = array - back;
        if (!irix_guest_ptr(rt, cookie, 4u))
            continue;

        count = irix_guest_read32be(rt, cookie);
        if (validate_vec_count(rt, array, element_size, count) != 0)
            continue;

        *count_out = count;
        *cookie_back_out = back;
        return 0;
    }
    return -1;
}

static void bridge___vec_delete2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr array = (uint32_t)cpu->gpr[4];
    uint32_t count = (uint32_t)cpu->gpr[5];
    uint32_t element_size = (uint32_t)cpu->gpr[6];
    IrixGuestAddr dtor = (uint32_t)cpu->gpr[7];
    uint32_t delete_flag = (uint32_t)cpu->gpr[8];
    uint32_t dtor_flag = (uint32_t)cpu->gpr[9];
    uint32_t extended_count = (uint32_t)cpu->gpr[10];
    uint32_t cookie_back = 0;
    uint64_t total;
    uint32_t i;

    cpu->gpr[2] = 0;
    if (!array)
        return;

    if (count == UINT32_MAX) {
        if (recover_vec_count(rt, array, element_size, &count, &cookie_back) == 0) {
            /* recovered from the compiler's vector cookie */
        } else if (validate_vec_count(rt, array, element_size, extended_count) == 0) {
            count = extended_count;
        } else {
            irix_guest_trap(rt, cpu, 0xeeee2103u);
            return;
        }
    }

    total = (uint64_t)count * (uint64_t)element_size;
    if (total > UINT32_MAX || (total && !irix_guest_ptr(rt, array, (size_t)total))) {
        irix_guest_trap(rt, cpu, 0xeeee2104u);
        return;
    }

    if (dtor && element_size) {
        for (i = count; i > 0u && !rt->trapped; --i) {
            IrixGuestAddr object = array + (i - 1u) * element_size;
            if (call_guest_dtor(rt, cpu, dtor, object, dtor_flag) != 0)
                return;
        }
    }

    if (delete_flag) {
        IrixGuestFunction free_fn = irix_libc_bridge_lookup("free");
        if (free_fn) {
            IrixGuestCpu call = *cpu;
            IrixGuestAddr base = cookie_back ? array - cookie_back : array;
            call.gpr[4] = (uint64_t)(int64_t)(int32_t)base;
            free_fn(rt, &call);
        }
    }
}

void irix_cxx_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    memset(g_ostrstreams, 0, sizeof(g_ostrstreams));
    (void)g_rt;
}

IrixGuestFunction irix_cxx_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "__vec_delete2")) return bridge___vec_delete2;
    if (!strcmp(name, "__ct__10ostrstreamGv")) return bridge_ostrstream_ctor;
    if (!strcmp(name, "__dt__10ostrstreamGv")) return bridge_ostrstream_dtor;
    if (!strcmp(name, "str__10ostrstreamGv")) return bridge_ostrstream_str;
    if (!strcmp(name, "__ls__7ostreamGl")) return bridge_ostream_insert_long;
    if (!strcmp(name, "__ls__7ostreamGi")) return bridge_ostream_insert_long;
    if (!strcmp(name, "__ls__7ostreamGPCc")) return bridge_ostream_insert_cstr;
    return 0;
}
