#!/usr/bin/env python3
"""Translate one IRIX N32 MIPS function into portable C.

The translator is deliberately conservative: unsupported instructions stop
translation instead of silently inventing semantics. Guest addresses remain
32-bit and generated code uses the runtime's big-endian memory helpers.
"""
import argparse
import re
import struct
import sys

SHT_SYMTAB = 2
SHT_DYNSYM = 11
STT_FUNC = 2

REG = {
    0: 'zero', 1: 'at', 2: 'v0', 3: 'v1', 4: 'a0', 5: 'a1', 6: 'a2', 7: 'a3',
    8: 'a4', 9: 'a5', 10: 'a6', 11: 'a7', 12: 't0', 13: 't1', 14: 't2',
    15: 't3', 16: 's0', 17: 's1', 18: 's2', 19: 's3', 20: 's4', 21: 's5',
    22: 's6', 23: 's7', 24: 't8', 25: 't9', 26: 'k0', 27: 'k1', 28: 'gp',
    29: 'sp', 30: 'fp', 31: 'ra'
}


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def sx32_expr(expr):
    return '(uint64_t)(int64_t)(int32_t)(%s)' % expr


class Elf32BE:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        if self.data[:4] != b'\x7fELF' or self.data[4] != 1 or self.data[5] != 2:
            raise ValueError('expected ELF32 big-endian input')
        h = struct.unpack_from('>16sHHIIIIIHHHHHH', self.data, 0)
        self.entry = h[4]
        self.phoff = h[5]
        self.shoff = h[6]
        self.flags = h[7]
        self.phentsize = h[9]
        self.phnum = h[10]
        self.shentsize = h[11]
        self.shnum = h[12]
        self.shstrndx = h[13]
        self.sections = []
        for i in range(self.shnum):
            off = self.shoff + i * self.shentsize
            sh = struct.unpack_from('>IIIIIIIIII', self.data, off)
            self.sections.append({
                'name_off': sh[0], 'type': sh[1], 'flags': sh[2], 'addr': sh[3],
                'offset': sh[4], 'size': sh[5], 'link': sh[6], 'info': sh[7],
                'align': sh[8], 'entsize': sh[9]
            })
        shstr = self.sections[self.shstrndx]
        names = self.data[shstr['offset']:shstr['offset'] + shstr['size']]
        for sh in self.sections:
            end = names.find(b'\0', sh['name_off'])
            sh['name'] = names[sh['name_off']:end].decode('ascii', 'replace')

    def vaddr_to_offset(self, addr, size=1):
        for i in range(self.phnum):
            off = self.phoff + i * self.phentsize
            p = struct.unpack_from('>IIIIIIII', self.data, off)
            p_type, p_offset, p_vaddr, _, p_filesz, _, _, _ = p
            if p_type == 1 and addr >= p_vaddr and addr + size <= p_vaddr + p_filesz:
                return p_offset + (addr - p_vaddr)
        raise ValueError('address 0x%08x is not file-backed' % addr)

    def words(self, addr, size):
        if size % 4:
            raise ValueError('function size is not word aligned')
        off = self.vaddr_to_offset(addr, size)
        return struct.unpack_from('>%dI' % (size // 4), self.data, off)

    def symbols(self):
        out = []
        for sh in self.sections:
            if sh['type'] not in (SHT_SYMTAB, SHT_DYNSYM) or not sh['entsize']:
                continue
            strsh = self.sections[sh['link']]
            strings = self.data[strsh['offset']:strsh['offset'] + strsh['size']]
            count = sh['size'] // sh['entsize']
            for i in range(count):
                off = sh['offset'] + i * sh['entsize']
                st_name, st_value, st_size, st_info, st_other, st_shndx = \
                    struct.unpack_from('>IIIBBH', self.data, off)
                if (st_info & 0xf) != STT_FUNC or not st_value:
                    continue
                end = strings.find(b'\0', st_name)
                name = strings[st_name:end].decode('ascii', 'replace')
                if name:
                    out.append((name, st_value, st_size))
        return out

    def find_function(self, name):
        matches = [s for s in self.symbols() if s[0] == name]
        if not matches:
            raise ValueError('function not found: %s' % name)
        return max(matches, key=lambda x: x[2])


def reg(n):
    return 'cpu->gpr[%d] /* %s */' % (n, REG.get(n, 'r%d' % n))


def mem_addr(rs, imm):
    return '(IrixGuestAddr)((uint32_t)(%s) + (uint32_t)(int32_t)%d)' % (reg(rs), imm)


def decode(addr, insn):
    op = insn >> 26
    rs = (insn >> 21) & 31
    rt = (insn >> 16) & 31
    rd = (insn >> 11) & 31
    sa = (insn >> 6) & 31
    fn = insn & 63
    immu = insn & 0xffff
    imm = sx16(immu)
    target = insn & 0x03ffffff

    if insn == 0:
        return ['/* nop */']

    if op == 0:
        if fn == 0x08:
            kind = 'IRIX_PENDING_RETURN' if rs == 31 else 'IRIX_PENDING_JUMP'
            return ['pending_kind = %s;' % kind,
                    'pending_target = (IrixGuestAddr)%s;' % reg(rs)]
        if fn == 0x09:
            link = rd if rd else 31
            return [
                '%s = %s;' % (reg(link), sx32_expr('0x%08xu' % (addr + 8))),
                'pending_kind = IRIX_PENDING_CALL;',
                'pending_target = (IrixGuestAddr)%s;' % reg(rs),
                'pending_return = 0x%08xu;' % (addr + 8)
            ]
        if fn in (0x21, 0x2d):
            if fn == 0x21:
                expr = sx32_expr('(uint32_t)%s + (uint32_t)%s' % (reg(rs), reg(rt)))
            else:
                expr = '%s + %s' % (reg(rs), reg(rt))
            return ['%s = %s;' % (reg(rd), expr)]
        if fn == 0x25:
            return ['%s = %s | %s;' % (reg(rd), reg(rs), reg(rt))]
        if fn == 0x00:
            return ['%s = %s;' % (reg(rd), sx32_expr('(uint32_t)%s << %d' % (reg(rt), sa)))]
        raise ValueError('unsupported SPECIAL fn=0x%x at 0x%08x word=0x%08x' % (fn, addr, insn))

    if op == 0x02:
        dst = ((addr + 4) & 0xf0000000) | (target << 2)
        return ['pending_kind = IRIX_PENDING_JUMP;', 'pending_target = 0x%08xu;' % dst]
    if op == 0x03:
        dst = ((addr + 4) & 0xf0000000) | (target << 2)
        return [
            '%s = %s;' % (reg(31), sx32_expr('0x%08xu' % (addr + 8))),
            'pending_kind = IRIX_PENDING_CALL;',
            'pending_target = 0x%08xu;' % dst,
            'pending_return = 0x%08xu;' % (addr + 8)
        ]
    if op in (0x04, 0x05):
        dst = (addr + 4 + (imm << 2)) & 0xffffffff
        cond = '%s == %s' % (reg(rs), reg(rt)) if op == 0x04 else '%s != %s' % (reg(rs), reg(rt))
        return [
            'if (%s) {' % cond,
            '    pending_kind = IRIX_PENDING_JUMP;',
            '    pending_target = 0x%08xu;' % dst,
            '}'
        ]
    if op in (0x08, 0x09):
        return ['%s = %s;' % (reg(rt), sx32_expr('(uint32_t)%s + (uint32_t)(int32_t)%d' % (reg(rs), imm)))]
    if op == 0x0d:
        return ['%s = %s | 0x%xu;' % (reg(rt), reg(rs), immu)]
    if op == 0x0c:
        return ['%s = %s & 0x%xu;' % (reg(rt), reg(rs), immu)]
    if op == 0x0f:
        return ['%s = %s;' % (reg(rt), sx32_expr('0x%08xu' % ((immu << 16) & 0xffffffff)))]
    if op == 0x20:
        return ['%s = (uint64_t)(int64_t)(int8_t)irix_guest_read8(rt, %s);' % (reg(rt), mem_addr(rs, imm))]
    if op == 0x24:
        return ['%s = irix_guest_read8(rt, %s);' % (reg(rt), mem_addr(rs, imm))]
    if op == 0x23:
        return ['%s = (uint64_t)(int64_t)(int32_t)irix_guest_read32be(rt, %s);' % (reg(rt), mem_addr(rs, imm))]
    if op == 0x37:
        return ['%s = irix_guest_read64be(rt, %s);' % (reg(rt), mem_addr(rs, imm))]
    if op == 0x28:
        return ['irix_guest_write8(rt, %s, (uint8_t)%s);' % (mem_addr(rs, imm), reg(rt))]
    if op == 0x2b:
        return ['irix_guest_write32be(rt, %s, (uint32_t)%s);' % (mem_addr(rs, imm), reg(rt))]
    if op == 0x3f:
        return ['irix_guest_write64be(rt, %s, %s);' % (mem_addr(rs, imm), reg(rt))]

    raise ValueError('unsupported op=0x%x at 0x%08x word=0x%08x' % (op, addr, insn))


def c_name(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name)


def emit(path, name):
    elf = Elf32BE(path)
    sym_name, start, size = elf.find_function(name)
    if not size:
        raise ValueError('symbol has zero size: %s' % name)
    words = elf.words(start, size)
    decoded = []
    for i, word in enumerate(words):
        addr = start + i * 4
        decoded.append((addr, word, decode(addr, word)))

    fn = 'irix_recomp_%s' % c_name(name)
    print('/* Generated from %s: %s @ 0x%08x, 0x%x bytes. */' % (path, sym_name, start, size))
    print('#include "IrixGuest.h"')
    print('')
    print('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN };')
    print('')
    print('void %s(IrixGuestRuntime *rt, IrixGuestCpu *cpu)' % fn)
    print('{')
    print('    IrixGuestAddr pc = 0x%08xu;' % start)
    print('    IrixGuestAddr pending_target = 0;')
    print('    IrixGuestAddr pending_return = 0;')
    print('    int pending_kind = IRIX_PENDING_NONE;')
    print('    int old_pending;')
    print('    IrixGuestAddr old_target;')
    print('    IrixGuestAddr old_return;')
    print('')
    print('    for (;;) {')
    print('        old_pending = pending_kind;')
    print('        old_target = pending_target;')
    print('        old_return = pending_return;')
    print('        pending_kind = IRIX_PENDING_NONE;')
    print('        cpu->pc = pc;')
    print('')
    print('        switch (pc) {')
    for addr, word, lines in decoded:
        print('        case 0x%08xu: /* 0x%08x */' % (addr, word))
        for line in lines:
            print('            ' + line)
        print('            break;')
    print('        default:')
    print('            return;')
    print('        }')
    print('')
    print('        cpu->gpr[0] = 0;')
    print('        if (old_pending == IRIX_PENDING_RETURN)')
    print('            return;')
    print('        if (old_pending == IRIX_PENDING_CALL) {')
    print('            if (irix_guest_call(rt, cpu, old_target) != 0)')
    print('                return;')
    print('            pc = old_return;')
    print('            continue;')
    print('        }')
    print('        if (old_pending == IRIX_PENDING_JUMP) {')
    print('            pc = old_target;')
    print('            continue;')
    print('        }')
    print('        pc += 4;')
    print('    }')
    print('}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf')
    ap.add_argument('function')
    ns = ap.parse_args()
    try:
        emit(ns.elf, ns.function)
    except Exception as exc:
        print('translate_n32.py: %s' % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
