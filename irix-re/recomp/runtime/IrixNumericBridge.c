#include "IrixNumericBridge.h"

#include <stdlib.h>
#include <string.h>

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return addr ? (const char *)irix_guest_ptr(rt, addr, 1u) : 0;
}

static void bridge_atof(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    float v = s ? (float)atof(s) : 0.0f;
    irix_guest_fpr_set_s(cpu, 0u, v);
}

IrixGuestFunction irix_numeric_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "atof")) return bridge_atof;
    return 0;
}
