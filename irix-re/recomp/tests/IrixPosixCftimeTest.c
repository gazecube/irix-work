#include "IrixGuest.h"
#include "IrixPosixBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
    const IrixGuestAddr base = 0x31000000u;
    const IrixGuestAddr output = base + 0x100u;
    const IrixGuestAddr format = base + 0x200u;
    const IrixGuestAddr clock = base + 0x300u;
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestFunction cftime;
    char *guest_output;

    irix_guest_runtime_init(&rt);
    setenv("TZ", "UTC", 1);
    tzset();
    if (irix_guest_map(&rt, base, 0x4000u, 3u) != 0) {
        fprintf(stderr, "failed to map guest memory for cftime test\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    memcpy(irix_guest_ptr(&rt, format, 5u), "%Y", 3u);
    irix_guest_write32be(&rt, clock, 0u);
    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[4] = output;
    cpu.gpr[5] = format;
    cpu.gpr[6] = clock;

    cftime = irix_posix_bridge_lookup("cftime");
    if (!cftime) {
        fprintf(stderr, "cftime bridge was not registered\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }
    cftime(&rt, &cpu);
    guest_output = (char *)irix_guest_ptr(&rt, output, 5u);
    if (rt.trapped || cpu.gpr[2] != 4u || !guest_output ||
        strcmp(guest_output, "1970") != 0) {
        fprintf(stderr,
                "cftime bridge mismatch: trapped=%d result=%llu output=%s\n",
                rt.trapped, (unsigned long long)cpu.gpr[2],
                guest_output ? guest_output : "(unmapped)");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    irix_guest_runtime_destroy(&rt);
    return 0;
}
