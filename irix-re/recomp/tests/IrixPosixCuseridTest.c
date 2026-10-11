#include "IrixGuest.h"
#include "IrixPosixBridge.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const IrixGuestAddr base = 0x32000000u;
    const IrixGuestAddr buffer = base + 0x100u;
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestFunction cuserid;
    const struct passwd *pw = getpwuid(geteuid());
    const char *guest_name;

    if (!pw || !pw->pw_name) {
        fprintf(stderr, "host account has no passwd entry for cuserid test\n");
        return 1;
    }

    irix_guest_runtime_init(&rt);
    irix_posix_bridge_init(&rt);
    if (irix_guest_map(&rt, base, 0x4000u, 3u) != 0) {
        fprintf(stderr, "failed to map guest memory for cuserid test\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    cuserid = irix_posix_bridge_lookup("cuserid");
    if (!cuserid) {
        fprintf(stderr, "cuserid bridge was not registered\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    memset(&cpu, 0, sizeof(cpu));
    cuserid(&rt, &cpu);
    guest_name = cpu.gpr[2] ? irix_guest_cstr(&rt, (IrixGuestAddr)cpu.gpr[2], 9u) : 0;
    if (rt.trapped || !guest_name || strcmp(guest_name, pw->pw_name) != 0) {
        fprintf(stderr, "cuserid(NULL) bridge mismatch: trapped=%d name=%s\n",
                rt.trapped, guest_name ? guest_name : "(null)");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[4] = buffer;
    cuserid(&rt, &cpu);
    guest_name = (const char *)irix_guest_ptr(&rt, buffer, 9u);
    if (rt.trapped || cpu.gpr[2] != buffer || !guest_name ||
        strcmp(guest_name, pw->pw_name) != 0) {
        fprintf(stderr, "cuserid(buffer) bridge mismatch: trapped=%d result=0x%llx name=%s\n",
                rt.trapped, (unsigned long long)cpu.gpr[2],
                guest_name ? guest_name : "(unmapped)");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    irix_guest_runtime_destroy(&rt);
    return 0;
}
