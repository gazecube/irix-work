#define _GNU_SOURCE

#include "IrixGuest.h"
#include "IrixPosixBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    const IrixGuestAddr base = 0x31000000u;
    const IrixGuestAddr stack = base + 0x8000u;
    const IrixGuestAddr path = base + 0x100u;
    const IrixGuestAddr arg0 = base + 0x120u;
    const IrixGuestAddr shell_flag = base + 0x130u;
    const IrixGuestAddr command = base + 0x400u;
    const IrixGuestAddr dummy0 = base + 0x180u;
    const IrixGuestAddr dummy1 = base + 0x190u;
    const IrixGuestAddr dummy2 = base + 0x1a0u;
    const IrixGuestAddr dummy3 = base + 0x1b0u;
    const IrixGuestAddr env_string = base + 0x200u;
    const IrixGuestAddr env_vector = base + 0x300u;
    IrixGuestRuntime rt;
    IrixGuestCpu cpu;
    IrixGuestFunction execle;
    char root_template[] = "/tmp/irix-execle-test-XXXXXX";
    char *root;
    char bin_dir[512];
    char shell_path[512];
    const char *old_root = getenv("IRIX_RECOMP_ROOT");
    char *saved_root = old_root ? strdup(old_root) : NULL;
    pid_t child;
    int status;
    int result = 1;

    root = mkdtemp(root_template);
    if (!root) {
        perror("mkdtemp");
        free(saved_root);
        return 1;
    }
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin", root);
    snprintf(shell_path, sizeof(shell_path), "%s/bin/sh", root);
    if (mkdir(bin_dir, 0700) != 0 || symlink("/bin/sh", shell_path) != 0) {
        perror("prepare temporary guest root");
        goto cleanup_root;
    }
    if (setenv("IRIX_RECOMP_ROOT", root, 1) != 0) {
        perror("setenv IRIX_RECOMP_ROOT");
        goto cleanup_root;
    }

    irix_guest_runtime_init(&rt);
    if (irix_guest_map(&rt, base, 0x10000u, 3u) != 0) {
        fprintf(stderr, "failed to map guest memory for execle test\n");
        irix_guest_runtime_destroy(&rt);
        goto cleanup_env;
    }
    memcpy(irix_guest_ptr(&rt, path, 8u), "/bin/sh", 8u);
    memcpy(irix_guest_ptr(&rt, arg0, 3u), "sh", 3u);
    memcpy(irix_guest_ptr(&rt, shell_flag, 3u), "-c", 3u);
    memcpy(irix_guest_ptr(&rt, command, sizeof("test \"$FOO\" = execle-test && test \"$#\" = 3 && test \"$3\" = dummy3")),
           "test \"$FOO\" = execle-test && test \"$#\" = 3 && test \"$3\" = dummy3",
           sizeof("test \"$FOO\" = execle-test && test \"$#\" = 3 && test \"$3\" = dummy3"));
    memcpy(irix_guest_ptr(&rt, dummy0, 7u), "dummy0", 7u);
    memcpy(irix_guest_ptr(&rt, dummy1, 7u), "dummy1", 7u);
    memcpy(irix_guest_ptr(&rt, dummy2, 7u), "dummy2", 7u);
    memcpy(irix_guest_ptr(&rt, dummy3, 7u), "dummy3", 7u);
    memcpy(irix_guest_ptr(&rt, env_string, 16u), "FOO=execle-test", 16u);
    irix_guest_write32be(&rt, env_vector, env_string);
    irix_guest_write32be(&rt, env_vector + 4u, 0u);

    memset(&cpu, 0, sizeof(cpu));
    cpu.gpr[4] = path;       /* path */
    cpu.gpr[5] = arg0;       /* argv[0] */
    cpu.gpr[6] = shell_flag;
    cpu.gpr[7] = command;
    cpu.gpr[8] = dummy0;
    cpu.gpr[9] = dummy1;
    cpu.gpr[10] = dummy2;
    cpu.gpr[11] = dummy3;
    cpu.gpr[29] = stack;
    irix_guest_write64be(&rt, stack, 0u); /* variadic NULL terminator */
    irix_guest_write64be(&rt, stack + 8u, env_vector);

    execle = irix_posix_bridge_lookup("execle");
    if (!execle) {
        fprintf(stderr, "execle bridge was not registered\n");
        irix_guest_runtime_destroy(&rt);
        goto cleanup_env;
    }

    child = fork();
    if (child < 0) {
        perror("fork");
        irix_guest_runtime_destroy(&rt);
        goto cleanup_env;
    }
    if (child == 0) {
        execle(&rt, &cpu);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) {
        perror("waitpid");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result = 0;
    } else {
        fprintf(stderr, "execle child did not receive argv/envp (status=%d)\n",
                status);
    }

    if (result == 0) {
        IrixGuestFunction execl = irix_posix_bridge_lookup("execl");
        setenv("FOO", "execle-test", 1);
        child = fork();
        if (child == 0) {
            if (execl) execl(&rt, &cpu);
            _exit(127);
        }
        if (child < 0 || waitpid(child, &status, 0) < 0 ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "execl lost its shell command or inherited environment\n");
            result = 1;
        }
        unsetenv("FOO");
    }
    irix_guest_runtime_destroy(&rt);
cleanup_env:
    if (saved_root)
        setenv("IRIX_RECOMP_ROOT", saved_root, 1);
    else
        unsetenv("IRIX_RECOMP_ROOT");
    free(saved_root);
cleanup_root:
    unlink(shell_path);
    rmdir(bin_dir);
    rmdir(root);
    return result;
}
