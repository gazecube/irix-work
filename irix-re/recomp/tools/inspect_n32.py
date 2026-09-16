#!/usr/bin/env python3
"""Inspect ELF properties IID's IRIX static recompiler relies on."""
import argparse
import json
import struct

ELF_MAGIC = b'\x7fELF'
PT_LOAD = 1
PT_INTERP = 3
EF_MIPS_ABI2 = 0x20
EF_MIPS_ARCH_3 = 0x20000000


def cstring(data, off):
    end = data.find(b'\0', off)
    if end < 0:
        end = len(data)
    return data[off:end].decode('ascii', errors='replace')


def inspect(path):
    data = open(path, 'rb').read()
    if data[:4] != ELF_MAGIC:
        raise ValueError('not an ELF file')
    if data[4] != 1 or data[5] != 2:
        raise ValueError('IID recomp currently requires ELF32 MSB')

    hdr = struct.unpack_from('>16sHHIIIIIHHHHHH', data, 0)
    (_, e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
     e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize,
     e_shnum, e_shstrndx) = hdr

    if e_machine != 8:
        raise ValueError('not MIPS ELF (e_machine=%d)' % e_machine)

    segments = []
    interp = None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('>IIIIIIII', data, off)
        if p_type == PT_LOAD:
            segments.append({
                'offset': p_offset,
                'vaddr': p_vaddr,
                'filesz': p_filesz,
                'memsz': p_memsz,
                'flags': p_flags,
                'align': p_align,
            })
        elif p_type == PT_INTERP:
            interp = cstring(data, p_offset)

    return {
        'path': path,
        'entry': e_entry,
        'flags': e_flags,
        'abi2': bool(e_flags & EF_MIPS_ABI2),
        'mips3': (e_flags & 0xf0000000) == EF_MIPS_ARCH_3,
        'interpreter': interp,
        'program_headers': e_phnum,
        'section_headers': e_shnum,
        'load_segments': segments,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf')
    ns = ap.parse_args()
    print(json.dumps(inspect(ns.elf), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
