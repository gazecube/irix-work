#include "IrixGuest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int
irix_guest_load_elf32be(IrixGuestRuntime *rt, const char *path,
                        IrixGuestAddr *entry_out)
{
    FILE *fp;
    unsigned char *file_data;
    long file_size_long;
    size_t file_size;
    uint32_t phoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t machine;
    uint16_t i;

    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    file_size_long = ftell(fp);
    if (file_size_long < 52) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    file_size = (size_t)file_size_long;
    file_data = (unsigned char *)malloc(file_size);
    if (!file_data) {
        fclose(fp);
        return -1;
    }
    if (fread(file_data, 1, file_size, fp) != file_size) {
        free(file_data);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (memcmp(file_data, "\177ELF", 4) != 0 ||
        file_data[4] != 1 || file_data[5] != 2) {
        free(file_data);
        return -1;
    }
    machine = read_be16(file_data + 18);
    if (machine != 8) {
        free(file_data);
        return -1;
    }

    if (entry_out)
        *entry_out = read_be32(file_data + 24);
    phoff = read_be32(file_data + 28);
    phentsize = read_be16(file_data + 42);
    phnum = read_be16(file_data + 44);
    if ((uint64_t)phoff + (uint64_t)phentsize * phnum > file_size) {
        free(file_data);
        return -1;
    }

    for (i = 0; i < phnum; ++i) {
        const unsigned char *ph = file_data + phoff + (size_t)i * phentsize;
        uint32_t type = read_be32(ph + 0);
        uint32_t offset;
        uint32_t vaddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
        void *dst;

        if (type != 1)
            continue;
        offset = read_be32(ph + 4);
        vaddr = read_be32(ph + 8);
        filesz = read_be32(ph + 16);
        memsz = read_be32(ph + 20);
        flags = read_be32(ph + 24);
        if (!memsz || filesz > memsz ||
            (uint64_t)offset + filesz > file_size) {
            free(file_data);
            return -1;
        }
        if (irix_guest_map(rt, vaddr, memsz, flags) != 0) {
            free(file_data);
            return -1;
        }
        dst = irix_guest_ptr(rt, vaddr, memsz);
        if (!dst) {
            free(file_data);
            return -1;
        }
        memcpy(dst, file_data + offset, filesz);
    }

    free(file_data);
    return 0;
}
