#include <stdint.h>
#include <stdio.h>

extern int n32_native_probe(uint64_t *gpr, unsigned char *memory);

int main(void)
{
    uint64_t gpr[32] = {0};
    unsigned char memory[64] = {0};
    gpr[1] = UINT64_C(0x7fffffff);
    gpr[2] = 1;
    gpr[7] = 8;
    gpr[11] = 8;

    if (n32_native_probe(gpr, memory) != 0)
        return 1;
    if (gpr[0] != 0 || gpr[1] != UINT64_C(0x7fffffff) ||
        gpr[2] != 1 || gpr[8] != UINT64_C(0xffffffff80000000) ||
        gpr[9] != UINT64_C(0x7fffffff) ||
        gpr[5] != UINT64_C(0xffffffffffffffff) ||
        gpr[6] != UINT64_C(0xffffffff80000002) ||
        gpr[10] != UINT64_C(0x11223344) ||
        gpr[7] != 8 || gpr[3] != UINT64_C(0x11223344) ||
        gpr[4] != UINT64_C(0x11) ||
        gpr[12] != 1 || gpr[13] != 0 ||
        gpr[14] != 1 || gpr[15] != 1 ||
        memory[12] != 0x11 || memory[13] != 0x22 ||
        memory[14] != 0x33 || memory[15] != 0x44 ||
        memory[16] != 0x11) {
        fprintf(stderr,
                "gpr3=%llx gpr4=%llx gpr6=%llx mem=%02x%02x%02x%02x/%02x\n",
                (unsigned long long)gpr[3], (unsigned long long)gpr[4],
                (unsigned long long)gpr[6], memory[12], memory[13],
                memory[14], memory[15], memory[16]);
        fprintf(stderr, "native i386 instruction probe produced wrong state\n");
        return 2;
    }

    gpr[11] = 9;
    if (n32_native_probe(gpr, memory) != 0 ||
        gpr[12] != 2 || gpr[13] != 1 ||
        gpr[14] != 2 || gpr[15] != 1) {
        fprintf(stderr, "native branch delay-slot probe produced wrong state\n");
        return 3;
    }
    puts("native i386 instruction probe passed");
    return 0;
}
