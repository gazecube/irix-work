#include "IrixCxxBridge.h"
#include "IrixLibcBridge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static IrixGuestRuntime *g_rt;

static int call_guest_dtor(IrixGuestRuntime *rt, const IrixGuestCpu *caller,
                           IrixGuestAddr dtor, IrixGuestAddr object,
                           uint32_t dtor_flag)
{
    IrixGuestCpu call = *caller;
    IrixGuestAddr saved_pc = rt->current_pc;

    call.gpr[4] = (uint64_t)(int64_t)(int32_t)object;
    call.gpr[5] = (uint64_t)(int64_t)(int32_t)dtor_flag;
    if (irix_guest_call(rt, &call, dtor) != 0) {
        rt->current_pc = saved_pc;
        return -1;
    }
    rt->current_pc = saved_pc;
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
        uint64_t total;

        if (array < back)
            continue;
        cookie = array - back;
        if (!irix_guest_ptr(rt, cookie, 4u))
            continue;

        count = irix_guest_read32be(rt, cookie);
        if (!count || count > 0x00100000u)
            continue;

        total = (uint64_t)count * (uint64_t)element_size;
        if (total > UINT32_MAX)
            continue;
        if (!irix_guest_ptr(rt, array, (size_t)total))
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
    uint32_t cookie_back = 0;
    uint64_t total;
    uint32_t i;

    cpu->gpr[2] = 0;
    if (!array)
        return;

    if (count == UINT32_MAX) {
        if (recover_vec_count(rt, array, element_size, &count, &cookie_back) != 0) {
            fprintf(stderr,
                    "VEC_DELETE2 COOKIE UNKNOWN array=%08x elem=%u pre=%08x,%08x,%08x,%08x\n",
                    array, element_size,
                    array >= 16u && irix_guest_ptr(rt, array - 16u, 4u) ? irix_guest_read32be(rt, array - 16u) : 0u,
                    array >= 12u && irix_guest_ptr(rt, array - 12u, 4u) ? irix_guest_read32be(rt, array - 12u) : 0u,
                    array >= 8u && irix_guest_ptr(rt, array - 8u, 4u) ? irix_guest_read32be(rt, array - 8u) : 0u,
                    array >= 4u && irix_guest_ptr(rt, array - 4u, 4u) ? irix_guest_read32be(rt, array - 4u) : 0u);
            irix_guest_trap(rt, cpu, 0xeeee2103u);
            return;
        }
        fprintf(stderr,
                "VEC_DELETE2 recovered count=%u cookie_back=%u array=%08x elem=%u\n",
                count, cookie_back, array, element_size);
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
    (void)g_rt;
}

IrixGuestFunction irix_cxx_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "__vec_delete2")) return bridge___vec_delete2;
    return 0;
}
