/*
 * Reconstructed platform glue (not SGI source): the IRIX <elf.h> is not part
 * of the imported tree.  Only the ELF types named by libsk.h are provided,
 * laid out per the public System V gABI.
 */
#ifndef ARCS_UEFI_ELF_H
#define ARCS_UEFI_ELF_H

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    unsigned short e_type, e_machine;
    unsigned int e_version, e_entry, e_phoff, e_shoff, e_flags;
    unsigned short e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    unsigned int p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    unsigned short e_type, e_machine;
    unsigned int e_version;
    unsigned long long e_entry, e_phoff, e_shoff;
    unsigned int e_flags;
    unsigned short e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    unsigned int p_type, p_flags;
    unsigned long long p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

#endif
