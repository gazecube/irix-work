#define irix_x11_bridge_lookup irix_x11_bridge_lookup_base
#include "IrixX11Bridge.c"
#undef irix_x11_bridge_lookup

#include "IrixX11BridgeExtraAccess.h"

static XPoint *unit_read_points(IrixGuestRuntime *rt, IrixGuestAddr addr, int n)
{
    XPoint *p;
    int i;
    if (n <= 0) return 0;
    if (!addr || !irix_guest_ptr(rt, addr, (size_t)n * 4u)) return 0;
    p = (XPoint *)calloc((size_t)n, sizeof(*p));
    if (!p) return 0;
    for (i = 0; i < n; ++i) {
        p[i].x = (short)irix_guest_read16be(rt, addr + (IrixGuestAddr)i * 4u + 0u);
        p[i].y = (short)irix_guest_read16be(rt, addr + (IrixGuestAddr)i * 4u + 2u);
    }
    return p;
}

static void bridge_unit_XPolygonRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int n = (int32_t)cpu->gpr[5];
    XPoint *points = unit_read_points(rt, (uint32_t)cpu->gpr[4], n);
    Region r = (n == 0 || points) ? XPolygonRegion(points, n, (int)cpu->gpr[6]) : 0;
    free(points);
    cpu->gpr[2] = region_to_guest(r);
}

static void bridge_unit_XSetRegion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
    GC gc = irix_x11_primary_gc_from_guest((uint32_t)cpu->gpr[5]);
    Region r = region_from_guest((uint32_t)cpu->gpr[6]);
    (void)rt;
    cpu->gpr[2] = (d && gc && r) ? (uint32_t)XSetRegion(d, gc, r) : 0;
}

IrixGuestFunction irix_x11_bridge_lookup(const char *name)
{
    IrixGuestFunction fn = irix_x11_bridge_lookup_base(name);
    if (fn || !name) return fn;
    if (!strcmp(name, "XPolygonRegion")) return bridge_unit_XPolygonRegion;
    if (!strcmp(name, "XSetRegion")) return bridge_unit_XSetRegion;
    return 0;
}
