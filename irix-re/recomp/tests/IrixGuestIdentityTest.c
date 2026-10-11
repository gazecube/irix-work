#define _POSIX_C_SOURCE 200809L

#include "IrixGuest.h"
#include "IrixSyscallNumbers.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int check_pair(IrixGuestRuntime *rt, uint32_t syscall_no,
                      uid_t real_id, uid_t effective_id, const char *name)
{
    IrixGuestCpu cpu = {0};
    cpu.gpr[2] = syscall_no;
    irix_guest_syscall(rt, &cpu);
    if (cpu.gpr[7] != 0u || (uid_t)cpu.gpr[2] != real_id ||
        (uid_t)cpu.gpr[3] != effective_id) {
        fprintf(stderr,
                "%s returned error=%llu real=%llu effective=%llu; expected %lu/%lu\n",
                name, (unsigned long long)cpu.gpr[7],
                (unsigned long long)cpu.gpr[2],
                (unsigned long long)cpu.gpr[3],
                (unsigned long)real_id, (unsigned long)effective_id);
        return 0;
    }
    return 1;
}

static int check_setpgrp(IrixGuestRuntime *rt)
{
    IrixGuestCpu cpu = {0};
    pid_t child;
    int status;

    cpu.gpr[2] = SYS_setpgrp;
    cpu.gpr[4] = 0u;
    irix_guest_syscall(rt, &cpu);
    if (cpu.gpr[7] != 0u || (pid_t)cpu.gpr[2] != getpgrp()) {
        fprintf(stderr, "setpgrp(0) returned error=%llu value=%llu; expected pgrp=%ld\n",
                (unsigned long long)cpu.gpr[7],
                (unsigned long long)cpu.gpr[2], (long)getpgrp());
        return 0;
    }

    child = fork();
    if (child < 0) {
        perror("fork for setpgrp(flags=1)");
        return 0;
    }
    if (child == 0) {
        IrixGuestCpu child_cpu = {0};
        pid_t sid;

        child_cpu.gpr[2] = SYS_setpgrp;
        child_cpu.gpr[4] = 1u;
        irix_guest_syscall(rt, &child_cpu);
        sid = getsid(0);
        if (child_cpu.gpr[7] != 0u || sid != getpid() ||
            (pid_t)child_cpu.gpr[2] != sid) {
            fprintf(stderr,
                    "setpgrp(1) returned error=%llu value=%llu; expected new sid=%ld\n",
                    (unsigned long long)child_cpu.gpr[7],
                    (unsigned long long)child_cpu.gpr[2], (long)getpid());
            _exit(1);
        }
        _exit(0);
    }

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr, "setpgrp(flags=1) child failed\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    IrixGuestRuntime rt;
    int ok;

    irix_guest_runtime_init(&rt);
    ok = check_pair(&rt, SYS_getuid, getuid(), geteuid(), "getuid/geteuid") &&
         check_pair(&rt, SYS_getgid, getgid(), getegid(), "getgid/getegid") &&
         check_setpgrp(&rt);
    irix_guest_runtime_destroy(&rt);
    return ok ? 0 : 1;
}
