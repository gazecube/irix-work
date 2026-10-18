#include "IrixGuest.h"
#include "IrixX11Bridge.h"
#include "IrixX11BridgeExtraAccess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void irix_x11_extra_init(IrixGuestRuntime *rt);
IrixGuestFunction irix_x11_extra_lookup(const char *name);

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void unexpected_original_path(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)cpu;
    rt->trapped = 1;
}

int main(void)
{
    static IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestFunction open_display;
    IrixGuestAddr display, first, other, mutable_gc, guest;
    Display *host;
    uint32_t root;
    unsigned i;
    const IrixGuestAddr values = 0x10000u;
    if (!getenv("DISPLAY")) return 77;
    irix_guest_runtime_init(&rt);
    CHECK(irix_guest_map(&rt, values, 4096u, 3u) == 0);
    irix_x11_bridge_init(&rt, 0, 0);
    irix_x11_extra_init(&rt);
    memset(&cpu, 0, sizeof(cpu));
    open_display = irix_x11_extra_lookup("XOpenDisplay");
    CHECK(open_display != 0);
    open_display(&rt, &cpu);
    display = (uint32_t)cpu.gpr[2];
    CHECK(display != 0);
    host = irix_x11_primary_display_from_guest(display);
    CHECK(host != 0);
    root = (uint32_t)DefaultRootWindow(host);
    irix_guest_write32be(&rt, values + 8u, BlackPixel(host, DefaultScreen(host)));
    first = irix_x11_primary_acquire_xt_gc(&rt, display, root, GCForeground, values, 1);
    CHECK(first != 0);
    /* More simultaneous references than the 1024-slot resource table. */
    for (i = 0; i < 4096u; ++i) {
        irix_guest_write32be(&rt, values + 12u, i); /* background is not selected */
        guest = irix_x11_primary_acquire_xt_gc(&rt, display, root, GCForeground, values, 1);
        CHECK(guest == first);
    }
    irix_guest_write32be(&rt, values + 8u, WhitePixel(host, DefaultScreen(host)));
    other = irix_x11_primary_acquire_xt_gc(&rt, display, root, GCForeground, values, 1);
    CHECK(other && other != first);
    mutable_gc = irix_x11_primary_acquire_xt_gc(&rt, display, root, GCForeground, values, 0);
    CHECK(mutable_gc && mutable_gc != other);
    CHECK(irix_x11_primary_release_xt_gc(&rt, mutable_gc));
    CHECK(irix_x11_primary_release_xt_gc(&rt, other));
    for (i = 0; i < 4096u; ++i) {
        CHECK(irix_x11_primary_release_xt_gc(&rt, first));
        CHECK(irix_x11_primary_gc_from_guest(first) != 0);
    }
    CHECK(irix_x11_primary_release_xt_gc(&rt, first));
    CHECK(irix_x11_primary_gc_from_guest(first) == 0);
    CHECK(!irix_x11_primary_release_xt_gc(&rt, first));
    /* Repeated unique allocations must recycle handles after release. */
    for (i = 0; i < 4096u; ++i) {
        guest = irix_x11_primary_acquire_xt_gc(&rt, display, root, GCForeground, values, 0);
        CHECK(guest != 0);
        CHECK(irix_x11_primary_release_xt_gc(&rt, guest));
    }
    /* Ordinary Xlib resources are never owned by the Xt cache. */
    guest = irix_x11_primary_create_gc(&rt, display, root, GCForeground, values);
    CHECK(guest != 0);
    CHECK(!irix_x11_primary_release_xt_gc(&rt, guest));
    CHECK(irix_x11_primary_gc_from_guest(guest) != 0);
    (void)irix_x11_primary_free_gc(&rt, display, guest);
    /* Exercise the recomp dispatch hooks as a pair, not just the helpers. */
    CHECK(setenv("IRIX_EXEC_BACKEND", "static", 1) == 0);
    irix_guest_register_function(&rt, 0x0f61cd84u, unexpected_original_path, "XtGetGC");
    irix_guest_register_function(&rt, 0x0f61e158u, unexpected_original_path, "XtReleaseGC");
    irix_guest_write32be(&rt, values + 0x100u + 0x58u, 0x72440000u);
    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[4] = values + 0x100u;
    cpu.gpr[5] = GCForeground;
    cpu.gpr[6] = values;
    CHECK(irix_guest_call(&rt, &cpu, 0x0f61cd84u) == 0);
    guest = (uint32_t)cpu.gpr[2];
    CHECK(guest != 0);
    cpu.gpr[4] = values + 0x100u;
    cpu.gpr[5] = GCForeground;
    cpu.gpr[6] = values;
    CHECK(irix_guest_call(&rt, &cpu, 0x0f61cd84u) == 0);
    CHECK((uint32_t)cpu.gpr[2] == guest);
    cpu.gpr[5] = guest;
    CHECK(irix_guest_call(&rt, &cpu, 0x0f61e158u) == 0);
    CHECK(irix_x11_primary_gc_from_guest(guest) != 0);
    CHECK(irix_guest_call(&rt, &cpu, 0x0f61e158u) == 0);
    CHECK(irix_x11_primary_gc_from_guest(guest) == 0);
    XSync(host, False);
    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[4] = display;
    irix_x11_extra_lookup("XCloseDisplay")(&rt, &cpu);
    irix_guest_runtime_destroy(&rt);
    puts("Xt GC sharing, reference counts, mutable resources and handle reuse passed");
    return 0;
}
