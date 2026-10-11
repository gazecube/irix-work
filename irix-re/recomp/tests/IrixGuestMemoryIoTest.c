#define _POSIX_C_SOURCE 200809L

#include "IrixGuest.h"
#include "IrixSyscallNumbers.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    static const char expected[] = "hello world!";
    IrixGuestRuntime rt;
    FILE *file;
    char *first;
    char *second;
    char *read_first;
    char *read_second;
    IrixGuestCpu cpu;
    int pipefd[2];
    char host_result[sizeof(expected)] = { 0 };
    int ok = 1;

    irix_guest_runtime_init(&rt);
    if (irix_guest_map(&rt, 0x10000u, 5u, 3u) != 0 ||
        irix_guest_map(&rt, 0x10005u, 7u, 3u) != 0 ||
        irix_guest_map(&rt, 0x20000u, 6u, 3u) != 0 ||
        irix_guest_map(&rt, 0x20006u, 6u, 3u) != 0 ||
        irix_guest_map(&rt, 0x30000u, 16u, 3u) != 0 ||
        irix_guest_map(&rt, 0x31000u, 64u, 3u) != 0 ||
        irix_guest_map(&rt, 0x32000u, 48u, 3u) != 0) {
        fprintf(stderr, "could not map test memory\n");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }

    first = (char *)irix_guest_ptr(&rt, 0x10000u, 5u);
    second = (char *)irix_guest_ptr(&rt, 0x10005u, 7u);
    read_first = (char *)irix_guest_ptr(&rt, 0x20000u, 6u);
    read_second = (char *)irix_guest_ptr(&rt, 0x20006u, 6u);
    memcpy(first, expected, 5u);
    memcpy(second, expected + 5u, 7u);

    file = tmpfile();
    if (!file) {
        perror("tmpfile");
        irix_guest_runtime_destroy(&rt);
        return 1;
    }
    if (irix_guest_write_fd(&rt, fileno(file), 0x10000u,
                            sizeof(expected) - 1u) !=
        (int64_t)(sizeof(expected) - 1u)) {
        perror("guest write across mappings");
        ok = 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0 ||
        irix_guest_read_fd(&rt, fileno(file), 0x20000u,
                           sizeof(expected) - 1u) !=
        (int64_t)(sizeof(expected) - 1u)) {
        perror("guest read across mappings");
        ok = 0;
    }
    if (memcmp(read_first, expected, 6u) != 0 ||
        memcmp(read_second, expected + 6u, 6u) != 0) {
        fprintf(stderr, "cross-mapping I/O changed the buffer contents\n");
        ok = 0;
    }

    irix_guest_write32be(&rt, 0x30000u, 0x10000u);
    irix_guest_write32be(&rt, 0x30004u, 5u);
    irix_guest_write32be(&rt, 0x30008u, 0x10005u);
    irix_guest_write32be(&rt, 0x3000cu, 7u);
    if (pipe(pipefd) != 0) {
        perror("pipe for writev");
        ok = 0;
    } else {
        memset(&cpu, 0, sizeof(cpu));
        cpu.gpr[2] = SYS_writev;
        cpu.gpr[4] = (uint32_t)pipefd[1];
        cpu.gpr[5] = 0x30000u;
        cpu.gpr[6] = 2u;
        irix_guest_syscall(&rt, &cpu);
        if (cpu.gpr[7] || (int64_t)cpu.gpr[2] !=
                (int64_t)(sizeof(expected) - 1u) ||
            read(pipefd[0], host_result, sizeof(host_result)) !=
                (ssize_t)(sizeof(expected) - 1u) ||
            memcmp(host_result, expected, sizeof(expected) - 1u) != 0) {
            fprintf(stderr, "guest writev did not gather both buffers\n");
            ok = 0;
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    memset(&cpu, 0, sizeof(cpu));
    irix_guest_write16be(&rt, 0x32000u, 2u); /* F_WRLCK */
    irix_guest_write16be(&rt, 0x32002u, SEEK_SET);
    irix_guest_write64be(&rt, 0x32008u, 0u);
    irix_guest_write64be(&rt, 0x32010u, 0u);
    cpu.gpr[2] = SYS_fcntl;
    cpu.gpr[4] = (uint32_t)fileno(file);
    cpu.gpr[5] = 13u; /* IRIX F_SETBSDLKW */
    cpu.gpr[6] = 0x32000u;
    irix_guest_syscall(&rt, &cpu);
    if (cpu.gpr[7] || (int64_t)cpu.gpr[2] != 0) {
        fprintf(stderr, "IRIX F_SETBSDLKW translation failed: v0=%lld a3=%llu\n",
                (long long)(int64_t)cpu.gpr[2],
                (unsigned long long)cpu.gpr[7]);
        ok = 0;
    }

    memset(&cpu, 0, sizeof(cpu));
    irix_guest_write16be(&rt, 0x32000u, 3u); /* F_UNLCK */
    cpu.gpr[2] = SYS_fcntl;
    cpu.gpr[4] = (uint32_t)fileno(file);
    cpu.gpr[5] = 12u; /* IRIX F_SETBSDLK */
    cpu.gpr[6] = 0x32000u;
    irix_guest_syscall(&rt, &cpu);
    if (cpu.gpr[7] || (int64_t)cpu.gpr[2] != 0) {
        fprintf(stderr, "IRIX F_SETBSDLK unlock translation failed: v0=%lld a3=%llu\n",
                (long long)(int64_t)cpu.gpr[2],
                (unsigned long long)cpu.gpr[7]);
        ok = 0;
    }

    memset(&cpu, 0, sizeof(cpu));
    irix_guest_write16be(&rt, 0x32000u, 2u); /* F_WRLCK probe */
    cpu.gpr[2] = SYS_fcntl;
    cpu.gpr[4] = (uint32_t)fileno(file);
    cpu.gpr[5] = 14u; /* IRIX F_GETLK */
    cpu.gpr[6] = 0x32000u;
    irix_guest_syscall(&rt, &cpu);
    if (cpu.gpr[7] || (int64_t)cpu.gpr[2] != 0 ||
        irix_guest_read16be(&rt, 0x32000u) != 3u) {
        fprintf(stderr, "IRIX F_GETLK translation failed: v0=%lld a3=%llu type=%u\n",
                (long long)(int64_t)cpu.gpr[2],
                (unsigned long long)cpu.gpr[7],
                (unsigned)irix_guest_read16be(&rt, 0x32000u));
        ok = 0;
    }

    if (pipe(pipefd) != 0) {
        perror("pipe for readv");
        ok = 0;
    } else {
        irix_guest_write32be(&rt, 0x30000u, 0x20000u);
        irix_guest_write32be(&rt, 0x30004u, 6u);
        irix_guest_write32be(&rt, 0x30008u, 0x20006u);
        irix_guest_write32be(&rt, 0x3000cu, 6u);
        if (write(pipefd[1], expected, sizeof(expected) - 1u) !=
            (ssize_t)(sizeof(expected) - 1u)) {
            perror("seed pipe for readv");
            ok = 0;
        }
        memset(read_first, 0, 6u);
        memset(read_second, 0, 6u);
        memset(&cpu, 0, sizeof(cpu));
        cpu.gpr[2] = SYS_readv;
        cpu.gpr[4] = (uint32_t)pipefd[0];
        cpu.gpr[5] = 0x30000u;
        cpu.gpr[6] = 2u;
        irix_guest_syscall(&rt, &cpu);
        if (cpu.gpr[7] || (int64_t)cpu.gpr[2] !=
                (int64_t)(sizeof(expected) - 1u) ||
            memcmp(read_first, expected, 6u) != 0 ||
            memcmp(read_second, expected + 6u, 6u) != 0) {
            fprintf(stderr,
                    "guest readv failed: v0=%llu a3=%llu first='%.*s' second='%.*s'\n",
                    (unsigned long long)cpu.gpr[2],
                    (unsigned long long)cpu.gpr[7], 6, read_first,
                    6, read_second);
            ok = 0;
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    {
        int sockfd[2];
        const IrixGuestAddr msg_addr = 0x31000u;
        const IrixGuestAddr vector_addr = 0x31020u;
        const uint32_t payload_length = (uint32_t)(sizeof(expected) - 1u);

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfd) != 0) {
            perror("socketpair for sendmsg/recvmsg");
            ok = 0;
        } else {
            irix_guest_write32be(&rt, msg_addr + 0u, 0u);
            irix_guest_write32be(&rt, msg_addr + 4u, 0u);
            irix_guest_write32be(&rt, msg_addr + 8u, vector_addr);
            irix_guest_write32be(&rt, msg_addr + 12u, 1u);
            irix_guest_write32be(&rt, msg_addr + 16u, 0u);
            irix_guest_write32be(&rt, msg_addr + 20u, 0u);
            irix_guest_write32be(&rt, vector_addr + 0u, 0x10000u);
            irix_guest_write32be(&rt, vector_addr + 4u, payload_length);
            memset(&cpu, 0, sizeof(cpu));
            cpu.gpr[2] = SYS_sendmsg;
            cpu.gpr[4] = (uint32_t)sockfd[0];
            cpu.gpr[5] = msg_addr;
            irix_guest_syscall(&rt, &cpu);
            if (cpu.gpr[7] || (int64_t)cpu.gpr[2] != payload_length ||
                read(sockfd[1], host_result, payload_length) != payload_length ||
                memcmp(host_result, expected, payload_length) != 0) {
                fprintf(stderr, "guest sendmsg failed to gather a cross-mapping buffer\n");
                ok = 0;
            }

            if (write(sockfd[1], expected, payload_length) != payload_length) {
                perror("seed socket for recvmsg");
                ok = 0;
            }
            irix_guest_write32be(&rt, vector_addr + 0u, 0x20000u);
            irix_guest_write32be(&rt, vector_addr + 4u, payload_length);
            memset(read_first, 0, 6u);
            memset(read_second, 0, 6u);
            memset(&cpu, 0, sizeof(cpu));
            cpu.gpr[2] = SYS_recvmsg;
            cpu.gpr[4] = (uint32_t)sockfd[0];
            cpu.gpr[5] = msg_addr;
            irix_guest_syscall(&rt, &cpu);
            if (cpu.gpr[7] || (int64_t)cpu.gpr[2] != payload_length ||
                memcmp(read_first, expected, 6u) != 0 ||
                memcmp(read_second, expected + 6u, 6u) != 0) {
                fprintf(stderr, "guest recvmsg failed to scatter into a cross-mapping buffer\n");
                ok = 0;
            }
            close(sockfd[0]);
            close(sockfd[1]);
        }
    }

    fclose(file);
    irix_guest_runtime_destroy(&rt);
    return ok ? 0 : 1;
}
