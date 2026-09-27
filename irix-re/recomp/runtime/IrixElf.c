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

/*
 * SGI MIPS .MIPS.options / PT_MIPS_OPTIONS records.
 *
 * Elf_Options is 8 bytes:
 *   uint8 kind, uint8 size, uint16 section, uint32 info
 *
 * ODK_REGINFO (kind == 1) is followed by a 24-byte Elf32_RegInfo whose
 * ri_gp_value is the final 32-bit word at offset +20 within RegInfo.
 * N32 DSOs commonly use this representation instead of PT_MIPS_REGINFO.
 */
static uint32_t read_mips_options_gp(const unsigned char *p, size_t size)
{
    size_t off = 0;
    while (off + 8u <= size) {
        uint8_t kind = p[off + 0u];
        uint8_t rec_size = p[off + 1u];

        if (rec_size < 8u || off + rec_size > size)
            break;

        if (kind == 1u && rec_size >= 32u)
            return read_be32(p + off + 8u + 20u);

        off += rec_size;
    }
    return 0;
}

int
irix_guest_load_elf32be_info(IrixGuestRuntime *rt, const char *path,
                             IrixGuestElfInfo *info_out)
{
    FILE *fp;
    unsigned char *file_data;
    long file_size_long;
    size_t file_size;
    uint32_t phoff;
    uint32_t shoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t machine;
    uint16_t i;
    IrixGuestElfInfo info;

    memset(&info, 0, sizeof(info));

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

    info.entry = read_be32(file_data + 24);
    phoff = read_be32(file_data + 28);
    shoff = read_be32(file_data + 32);
    phentsize = read_be16(file_data + 42);
    phnum = read_be16(file_data + 44);
    shentsize = read_be16(file_data + 46);
    shnum = read_be16(file_data + 48);
    info.phent = phentsize;
    info.phnum = phnum;
    if ((uint64_t)phoff + (uint64_t)phentsize * phnum > file_size) {
        free(file_data);
        return -1;
    }
    if (shoff && shentsize && shnum &&
        (uint64_t)shoff + (uint64_t)shentsize * shnum > file_size) {
        free(file_data);
        return -1;
    }

    for (i = 0; i < phnum; ++i) {
        const unsigned char *ph = file_data + phoff + (size_t)i * phentsize;
        uint32_t type = read_be32(ph + 0);
        uint32_t offset = read_be32(ph + 4);
        uint32_t vaddr = read_be32(ph + 8);
        uint32_t filesz = read_be32(ph + 16);

        if (type == 6) /* PT_PHDR */
            info.phdr = vaddr;
        else if (type == 3) { /* PT_INTERP */
            size_t n;
            if ((uint64_t)offset + filesz > file_size || filesz == 0) {
                free(file_data);
                return -1;
            }
            n = filesz;
            if (n >= sizeof(info.interp))
                n = sizeof(info.interp) - 1u;
            memcpy(info.interp, file_data + offset, n);
            info.interp[n] = '\0';
        } else if (type == 0x70000000u) { /* PT_MIPS_REGINFO */
            if ((uint64_t)offset + 24u <= file_size)
                info.gp_value = read_be32(file_data + offset + 20u);
        } else if (type == 0x70000002u) { /* PT_MIPS_OPTIONS */
            if ((uint64_t)offset + filesz <= file_size) {
                uint32_t gp =
                    read_mips_options_gp(file_data + offset, (size_t)filesz);
                if (gp)
                    info.gp_value = gp;
            }
        }
        uint32_t memsz;
        uint32_t flags;
        void *dst;

        if (type != 1)
            continue;
        memsz = read_be32(ph + 20);
        flags = read_be32(ph + 24);
        if (!memsz || filesz > memsz ||
            (uint64_t)offset + filesz > file_size) {
            free(file_data);
            return -1;
        }
        if (!info.load_base || vaddr < info.load_base)
            info.load_base = vaddr;
        if (!info.phdr && phoff >= offset &&
            (uint64_t)phoff + (uint64_t)phentsize * phnum <= (uint64_t)offset + filesz)
            info.phdr = vaddr + (phoff - offset);
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

    /*
     * Some SGI N32 DSOs expose GP metadata only through sections rather than
     * PT_MIPS_REGINFO/PT_MIPS_OPTIONS. Recover the same information from:
     *
     *   SHT_MIPS_REGINFO = 0x70000006
     *   SHT_MIPS_OPTIONS = 0x7000000d
     *
     * Prefer program-header metadata when present, but use section metadata as
     * a fallback so every mapped DSO can participate in GP ownership.
     */
    if (!info.gp_value && shoff && shentsize >= 40u && shnum) {
        for (i = 0; i < shnum; ++i) {
            const unsigned char *sh =
                file_data + shoff + (size_t)i * shentsize;
            uint32_t type = read_be32(sh + 4u);
            uint32_t offset = read_be32(sh + 16u);
            uint32_t size = read_be32(sh + 20u);

            if ((uint64_t)offset + size > file_size)
                continue;

            if (type == 0x70000006u && size >= 24u) {
                info.gp_value = read_be32(file_data + offset + 20u);
                if (info.gp_value)
                    break;
            }
            if (type == 0x7000000du && size >= 8u) {
                uint32_t gp =
                    read_mips_options_gp(file_data + offset, (size_t)size);
                if (gp) {
                    info.gp_value = gp;
                    break;
                }
            }
        }
    }

    if (info_out)
        *info_out = info;
    free(file_data);
    return 0;
}

int
irix_guest_load_elf32be(IrixGuestRuntime *rt, const char *path,
                        IrixGuestAddr *entry_out)
{
    IrixGuestElfInfo info;
    int rc = irix_guest_load_elf32be_info(rt, path, &info);
    if (rc == 0 && entry_out)
        *entry_out = info.entry;
    return rc;
}
