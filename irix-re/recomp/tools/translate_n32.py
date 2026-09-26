#!/usr/bin/env python3
"""Translate a single IRIX N32 MIPS function into portable C.

This is the first execution-oriented translator for IID. It intentionally
fails on unsupported instructions instead of emitting guessed semantics.
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
        self.etype = h[1]
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

    def words(self, addr, size, shndx=None, section_base=0):
        if size % 4:
            raise ValueError('function size is not word aligned')
        if self.etype == 1:
            if shndx is None:
                raise ValueError('ET_REL read requires symbol section')
            sh = self.sections[shndx]
            rel = addr - section_base
            if rel < 0 or rel + size > sh['size']:
                raise ValueError('ET_REL symbol range is outside section')
            off = sh['offset'] + rel
        else:
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
                if (st_info & 0xf) != STT_FUNC:
                    continue
                if self.etype != 1 and not st_value:
                    continue
                if st_shndx == 0:
                    continue
                end = strings.find(b'\0', st_name)
                name = strings[st_name:end].decode('ascii', 'replace')
                if name:
                    out.append((name, st_value, st_size, st_shndx))
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
        return ['/* nop */'], None

    if op == 0:
        if fn == 0x0d:
            code = (insn >> 6) & 0xfffff
            return ['irix_guest_trap(rt, cpu, %du);' % code, 'return;'], 'control'
        if fn == 0x08:
            if rs == 31:
                return ['pending_kind = IRIX_PENDING_RETURN;', 'pending_target = (IrixGuestAddr)%s;' % reg(rs)], 'control'
            return ['pending_kind = IRIX_PENDING_JUMP;', 'pending_target = (IrixGuestAddr)%s;' % reg(rs)], 'control'
        if fn == 0x09:
            # JALR writes the architecturally encoded rd.  rd==0 is a
            # deliberate no-link tail jump (commonly "jalr $zero,$t9"), not
            # an implicit request to use $ra.
            if rd == 0:
                return ['pending_kind = IRIX_PENDING_JUMP;', 'pending_target = (IrixGuestAddr)%s;' % reg(rs)], 'control'
            return ['%s = %s;' % (reg(rd), sx32_expr('0x%08xu' % (addr + 8))), 'pending_kind = IRIX_PENDING_CALL;', 'pending_target = (IrixGuestAddr)%s;' % reg(rs), 'pending_return = 0x%08xu;' % (addr + 8)], 'control'
        if fn in (0x21,0x2d):
            expr=sx32_expr('(uint32_t)%s + (uint32_t)%s'%(reg(rs),reg(rt))) if fn==0x21 else '%s + %s'%(reg(rs),reg(rt))
            return ['%s = %s;'%(reg(rd),expr)],None
        if fn==0x23: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)%s - (uint32_t)%s'%(reg(rs),reg(rt))))],None
        if fn==0x25: return ['%s = %s | %s;'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x2a: return ['%s = ((int64_t)%s < (int64_t)%s) ? 1 : 0;'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x00: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)%s << %d'%(reg(rt),sa)))],None
        if fn==0x02: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)%s >> %d'%(reg(rt),sa)))],None
        if fn==0x03: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)((int32_t)(uint32_t)%s >> %d)'%(reg(rt),sa)))],None
        if fn==0x04: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)%s << ((uint32_t)%s & 31u)'%(reg(rt),reg(rs))))],None
        if fn==0x06: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)%s >> ((uint32_t)%s & 31u)'%(reg(rt),reg(rs))))],None
        if fn==0x07: return ['%s = %s;'%(reg(rd),sx32_expr('(uint32_t)((int32_t)(uint32_t)%s >> ((uint32_t)%s & 31u))'%(reg(rt),reg(rs))))],None
        if fn==0x24: return ['%s = %s & %s;'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x26: return ['%s = %s ^ %s;'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x27: return ['%s = ~(%s | %s);'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x2b: return ['%s = ((uint64_t)%s < (uint64_t)%s) ? 1 : 0;'%(reg(rd),reg(rs),reg(rt))],None
        if fn==0x12: return ['%s = cpu->lo;'%reg(rd)],None
        if fn==0x10: return ['%s = cpu->hi;'%reg(rd)],None
        if fn==0x18:
            return ['{ int64_t p = (int64_t)(int32_t)(uint32_t)%s * (int64_t)(int32_t)(uint32_t)%s;'%(reg(rs),reg(rt)), '  cpu->lo = %s;'%sx32_expr('(uint32_t)p'), '  cpu->hi = %s; }'%sx32_expr('(uint32_t)((uint64_t)p >> 32)')],None
        if fn==0x19:
            return ['{ uint64_t p = (uint64_t)(uint32_t)%s * (uint64_t)(uint32_t)%s;'%(reg(rs),reg(rt)), '  cpu->lo = %s;'%sx32_expr('(uint32_t)p'), '  cpu->hi = %s; }'%sx32_expr('(uint32_t)(p >> 32)')],None
        if fn==0x1a:
            return ['if ((int32_t)(uint32_t)%s == 0) {'%reg(rt), '    cpu->lo = ((int32_t)(uint32_t)%s < 0) ? 1 : UINT64_C(0xffffffffffffffff);'%reg(rs), '    cpu->hi = %s;'%sx32_expr('(uint32_t)%s'%reg(rs)), '} else if ((uint32_t)%s == 0x80000000u && (uint32_t)%s == 0xffffffffu) {'%(reg(rs),reg(rt)), '    cpu->lo = UINT64_C(0xffffffff80000000); cpu->hi = 0;', '} else {', '    cpu->lo = %s;'%sx32_expr('(uint32_t)((int32_t)(uint32_t)%s / (int32_t)(uint32_t)%s)'%(reg(rs),reg(rt))), '    cpu->hi = %s;'%sx32_expr('(uint32_t)((int32_t)(uint32_t)%s %% (int32_t)(uint32_t)%s)'%(reg(rs),reg(rt))), '}'],None
        if fn==0x1b:
            return ['if ((uint32_t)%s == 0) {'%reg(rt),'    cpu->lo = UINT64_C(0xffffffffffffffff);','    cpu->hi = %s;'%sx32_expr('(uint32_t)%s'%reg(rs)),'} else {','    cpu->lo = %s;'%sx32_expr('(uint32_t)%s / (uint32_t)%s'%(reg(rs),reg(rt))),'    cpu->hi = %s;'%sx32_expr('(uint32_t)%s %% (uint32_t)%s'%(reg(rs),reg(rt))),'}'],None
        if fn==0x38: return ['%s = (uint64_t)%s << %d;'%(reg(rd),reg(rt),sa)],None
        if fn==0x3a: return ['%s = (uint64_t)%s >> %d;'%(reg(rd),reg(rt),sa)],None
        if fn==0x3b: return ['%s = (uint64_t)((int64_t)%s >> %d);'%(reg(rd),reg(rt),sa)],None
        if fn==0x34:
            code=(insn>>6)&0x3ff; return ['if (%s == %s) { irix_guest_trap(rt, cpu, %du); return; }'%(reg(rs),reg(rt),code)],None
        if fn==0x3c: return ['%s = (uint64_t)%s << %d;'%(reg(rd),reg(rt),sa+32)],None
        if fn==0x3e: return ['%s = (uint64_t)%s >> %d;'%(reg(rd),reg(rt),sa+32)],None
        if fn==0x3f: return ['%s = (uint64_t)((int64_t)%s >> %d);'%(reg(rd),reg(rt),sa+32)],None
        raise ValueError('unsupported SPECIAL fn=0x%x at 0x%08x word=0x%08x'%(fn,addr,insn))

    if op==0x02:
        dst=((addr+4)&0xf0000000)|(target<<2); return ['pending_kind = IRIX_PENDING_JUMP;','pending_target = 0x%08xu;'%dst],'control'
    if op==0x03:
        dst=((addr+4)&0xf0000000)|(target<<2); return ['%s = %s;'%(reg(31),sx32_expr('0x%08xu'%(addr+8))),'pending_kind = IRIX_PENDING_CALL;','pending_target = 0x%08xu;'%dst,'pending_return = 0x%08xu;'%(addr+8)],'control'
    if op in (0x04,0x05):
        dst=(addr+4+(imm<<2))&0xffffffff; cond='%s == %s'%(reg(rs),reg(rt)) if op==0x04 else '%s != %s'%(reg(rs),reg(rt)); return ['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'}'],'control'
    if op in (0x14,0x15):
        dst=(addr+4+(imm<<2))&0xffffffff; cond='%s == %s'%(reg(rs),reg(rt)) if op==0x14 else '%s != %s'%(reg(rs),reg(rt)); return ['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'} else {','    pc += 4;','}'],'control'
    if op in (0x06,0x07):
        dst=(addr+4+(imm<<2))&0xffffffff; cmp='<= 0' if op==0x06 else '> 0'; return ['if ((int64_t)%s %s)'%(reg(rs),cmp)+' {','    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'}'],'control'
    if op in (0x16,0x17):
        dst=(addr+4+(imm<<2))&0xffffffff; cmp='<= 0' if op==0x16 else '> 0'; return ['if ((int64_t)%s %s) {'%(reg(rs),cmp),'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'} else {','    pc += 4;','}'],'control'
    if op==0x01:
        dst=(addr+4+(imm<<2))&0xffffffff
        if rt in (0,2): cond='(int64_t)%s < 0'%reg(rs)
        elif rt in (1,3): cond='(int64_t)%s >= 0'%reg(rs)
        elif rt==0x11: cond='(int64_t)%s >= 0'%reg(rs)
        else: raise ValueError('unsupported REGIMM rt=0x%x at 0x%08x word=0x%08x'%(rt,addr,insn))
        likely=rt in (2,3); lines=[]
        if rt==0x11:
            lines.append('%s = %s;'%(reg(31),sx32_expr('0x%08xu'%(addr+8))))
            lines.extend(['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_LINK;','    pending_target = 0x%08xu;'%dst,'    pending_return = 0x%08xu;'%(addr+8),'}'])
        else:
            lines.extend(['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'}'])
        if likely: lines=['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'} else {','    pc += 4;','}']
        return lines,'control'
    if op in (0x08,0x09): return ['%s = %s;'%(reg(rt),sx32_expr('(uint32_t)%s + (uint32_t)(int32_t)%d'%(reg(rs),imm)))],None
    if op==0x1a:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; unsigned n = a & 7u; uint64_t m = irix_guest_read64be(rt, a & ~7u); uint64_t mask = n ? ((UINT64_C(1) << (n * 8u)) - 1u) : 0u;'%a,'  %s = (%s & mask) | (m << (n * 8u)); }'%(reg(rt),reg(rt))],None
    if op==0x1b:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; unsigned n = a & 7u; unsigned sh = (7u - n) * 8u; uint64_t m = irix_guest_read64be(rt, a & ~7u); uint64_t mask = sh ? (~UINT64_C(0) << (64u - sh)) : 0u;'%a,'  %s = (%s & mask) | (m >> sh); }'%(reg(rt),reg(rt))],None
    if op==0x19: return ['%s = %s + (uint64_t)(int64_t)(int16_t)0x%04xu;'%(reg(rt),reg(rs),immu)],None
    if op==0x0a: return ['%s = ((int64_t)%s < (int64_t)%d) ? 1 : 0;'%(reg(rt),reg(rs),imm)],None
    if op==0x0b: return ['%s = ((uint64_t)%s < (uint64_t)(int64_t)(int16_t)0x%04xu) ? 1 : 0;'%(reg(rt),reg(rs),immu)],None
    if op==0x0d: return ['%s = %s | 0x%xu;'%(reg(rt),reg(rs),immu)],None
    if op==0x0c: return ['%s = %s & 0x%xu;'%(reg(rt),reg(rs),immu)],None
    if op==0x0e: return ['%s = %s ^ 0x%xu;'%(reg(rt),reg(rs),immu)],None
    if op==0x0f: return ['%s = %s;'%(reg(rt),sx32_expr('0x%08xu'%((immu<<16)&0xffffffff)))],None
    if op==0x35: return ['cpu->fpr[%d] = irix_guest_read64be(rt, %s);'%(rt,mem_addr(rs,imm))],None
    if op==0x3d: return ['irix_guest_write64be(rt, %s, cpu->fpr[%d]);'%(mem_addr(rs,imm),rt)],None
    if op==0x39: return ['irix_guest_write32be(rt, %s, (uint32_t)cpu->fpr[%d]);'%(mem_addr(rs,imm),rt)],None
    if op==0x21: return ['%s = (uint64_t)(int64_t)(int16_t)irix_guest_read16be(rt, %s);'%(reg(rt),mem_addr(rs,imm))],None
    if op==0x25: return ['%s = irix_guest_read16be(rt, %s);'%(reg(rt),mem_addr(rs,imm))],None
    if op==0x20: return ['%s = (uint64_t)(int64_t)(int8_t)irix_guest_read8(rt, %s);'%(reg(rt),mem_addr(rs,imm))],None
    if op==0x24: return ['%s = irix_guest_read8(rt, %s);'%(reg(rt),mem_addr(rs,imm))],None
    if op==0x22:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; uint32_t old = (uint32_t)%s; uint32_t w = irix_guest_read32be(rt, a & ~3u);'%(a,reg(rt)),'  switch (a & 3u) {','  case 0: old = w; break;','  case 1: old = (old & 0x000000ffu) | (w << 8); break;','  case 2: old = (old & 0x0000ffffu) | (w << 16); break;','  default: old = (old & 0x00ffffffu) | (w << 24); break;','  } %s = %s; }'%(reg(rt),sx32_expr('old'))],None
    if op==0x26:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; uint32_t old = (uint32_t)%s; uint32_t w = irix_guest_read32be(rt, a & ~3u);'%(a,reg(rt)),'  switch (a & 3u) {','  case 0: old = (old & 0xffffff00u) | (w >> 24); break;','  case 1: old = (old & 0xffff0000u) | (w >> 16); break;','  case 2: old = (old & 0xff000000u) | (w >> 8); break;','  default: old = w; break;','  } %s = %s; }'%(reg(rt),sx32_expr('old'))],None
    if op==0x23:
        a=mem_addr(rs,imm)
        if addr==0x098382ac:
            return ['{ IrixGuestAddr a = %s; uint32_t v = irix_guest_read32be(rt, a);'%a,
                    '  fprintf(stderr, "IRIX ImdString LW probe: pc=0x098382ac a1=%08x addr=%08x value=%08x\\n", (uint32_t)cpu->gpr[5], a, v);',
                    '  %s = (uint64_t)(int64_t)(int32_t)v; }'%reg(rt)],None
        if addr==0x0fa34c9c:
            return ['{ IrixGuestAddr a = %s; uint32_t v = irix_guest_read32be(rt, a);'%a,
                    '  fprintf(stderr, "IRIX malloc GOT load: pc=0x%08x addr=0x%08x value=%08x gp=%08x\\n", 0x0fa34c9cu, a, v, (uint32_t)%s);'%reg(28),
                    '  %s = (uint64_t)(int64_t)(int32_t)v; }'%reg(rt)],None
        return ['%s = (uint64_t)(int64_t)(int32_t)irix_guest_read32be(rt, %s);'%(reg(rt),a)],None
    if op==0x31: return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | irix_guest_read32be(rt, %s);'%(rt,rt,mem_addr(rs,imm))],None
    if op==0x37: return ['%s = irix_guest_read64be(rt, %s);'%(reg(rt),mem_addr(rs,imm))],None
    if op==0x28: return ['irix_guest_write8(rt, %s, (uint8_t)%s);'%(mem_addr(rs,imm),reg(rt))],None
    if op==0x29: return ['irix_guest_write16be(rt, %s, (uint16_t)%s);'%(mem_addr(rs,imm),reg(rt))],None
    if op==0x2a:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; IrixGuestAddr q = a & ~3u; uint32_t m = irix_guest_read32be(rt, q); uint32_t v = (uint32_t)%s;'%(a,reg(rt)),'  switch (a & 3u) {','  case 0: m = v; break;','  case 1: m = (m & 0xff000000u) | (v >> 8); break;','  case 2: m = (m & 0xffff0000u) | (v >> 16); break;','  default: m = (m & 0xffffff00u) | (v >> 24); break;','  } irix_guest_write32be(rt, q, m); }'],None
    if op==0x2e:
        a=mem_addr(rs,imm); return ['{ IrixGuestAddr a = %s; IrixGuestAddr q = a & ~3u; uint32_t m = irix_guest_read32be(rt, q); uint32_t v = (uint32_t)%s;'%(a,reg(rt)),'  switch (a & 3u) {','  case 0: m = (m & 0x00ffffffu) | (v << 24); break;','  case 1: m = (m & 0x0000ffffu) | (v << 16); break;','  case 2: m = (m & 0x000000ffu) | (v << 8); break;','  default: m = v; break;','  } irix_guest_write32be(rt, q, m); }'],None
    if op==0x2b: return ['irix_guest_write32be(rt, %s, (uint32_t)%s);'%(mem_addr(rs,imm),reg(rt))],None
    if op==0x3f: return ['irix_guest_write64be(rt, %s, %s);'%(mem_addr(rs,imm),reg(rt))],None

    if op==0x11:
        fmt=rs; ft=rt; fs=rd; fd=sa; funct=fn
        if fmt==0x08:
            dst=(addr+4+(imm<<2))&0xffffffff; tf=ft&1; likely=bool(ft&2); cond='(cpu->fcr31 & (1u << 23)) != 0'
            if not tf: cond='!(%s)'%cond
            lines=['if (%s) {'%cond,'    pending_kind = IRIX_PENDING_JUMP;','    pending_target = 0x%08xu;'%dst,'}']
            if likely: lines += ['else {','    pc += 4;','}']
            return lines,'control'
        if fmt==0x01: return ['%s = cpu->fpr[%d];'%(reg(ft),fs)],None
        if fmt==0x05: return ['cpu->fpr[%d] = %s;'%(fs,reg(ft))],None
        if fmt==0x00: return ['%s = (uint64_t)(int64_t)(int32_t)(uint32_t)cpu->fpr[%d];'%(reg(ft),fs)],None
        if fmt==0x04: return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)%s;'%(fs,fs,reg(ft))],None
        if fmt==0x10:
            if funct==0x00: return ['irix_guest_fpr_set_s(cpu, %d, irix_guest_fpr_get_s(cpu, %d) + irix_guest_fpr_get_s(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x01: return ['irix_guest_fpr_set_s(cpu, %d, irix_guest_fpr_get_s(cpu, %d) - irix_guest_fpr_get_s(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x02: return ['irix_guest_fpr_set_s(cpu, %d, irix_guest_fpr_get_s(cpu, %d) * irix_guest_fpr_get_s(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x03: return ['irix_guest_fpr_set_s(cpu, %d, irix_guest_fpr_get_s(cpu, %d) / irix_guest_fpr_get_s(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x04: return ['irix_guest_fpr_set_s(cpu, %d, sqrtf(irix_guest_fpr_get_s(cpu, %d)));'%(fd,fs)],None
            if funct==0x05: return ['irix_guest_fpr_set_s(cpu, %d, fabsf(irix_guest_fpr_get_s(cpu, %d)));'%(fd,fs)],None
            if funct==0x06: return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)cpu->fpr[%d];'%(fd,fd,fs)],None
            if funct==0x07: return ['irix_guest_fpr_set_s(cpu, %d, -irix_guest_fpr_get_s(cpu, %d));'%(fd,fs)],None
            if funct==0x0d: return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)(int32_t)irix_guest_fpr_get_s(cpu, %d);'%(fd,fd,fs)],None
            if funct==0x21: return ['irix_guest_fpr_set_d(cpu, %d, (double)irix_guest_fpr_get_s(cpu, %d));'%(fd,fs)],None
            if funct==0x32: return ['if (irix_guest_fpr_get_s(cpu, %d) == irix_guest_fpr_get_s(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
            if funct==0x3c: return ['if (irix_guest_fpr_get_s(cpu, %d) < irix_guest_fpr_get_s(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
            if funct==0x3e: return ['if (irix_guest_fpr_get_s(cpu, %d) <= irix_guest_fpr_get_s(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
        if fmt==0x11:
            if funct==0x00: return ['irix_guest_fpr_set_d(cpu, %d, irix_guest_fpr_get_d(cpu, %d) + irix_guest_fpr_get_d(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x01: return ['irix_guest_fpr_set_d(cpu, %d, irix_guest_fpr_get_d(cpu, %d) - irix_guest_fpr_get_d(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x02: return ['irix_guest_fpr_set_d(cpu, %d, irix_guest_fpr_get_d(cpu, %d) * irix_guest_fpr_get_d(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x03: return ['irix_guest_fpr_set_d(cpu, %d, irix_guest_fpr_get_d(cpu, %d) / irix_guest_fpr_get_d(cpu, %d));'%(fd,fs,ft)],None
            if funct==0x04: return ['irix_guest_fpr_set_d(cpu, %d, sqrt(irix_guest_fpr_get_d(cpu, %d)));'%(fd,fs)],None
            if funct==0x05: return ['irix_guest_fpr_set_d(cpu, %d, fabs(irix_guest_fpr_get_d(cpu, %d)));'%(fd,fs)],None
            if funct==0x06: return ['cpu->fpr[%d] = cpu->fpr[%d];'%(fd,fs)],None
            if funct==0x07: return ['irix_guest_fpr_set_d(cpu, %d, -irix_guest_fpr_get_d(cpu, %d));'%(fd,fs)],None
            if funct==0x0d: return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)(int32_t)irix_guest_fpr_get_d(cpu, %d);'%(fd,fd,fs)],None
            if funct==0x20: return ['irix_guest_fpr_set_s(cpu, %d, (float)irix_guest_fpr_get_d(cpu, %d));'%(fd,fs)],None
            if funct==0x32: return ['if (irix_guest_fpr_get_d(cpu, %d) == irix_guest_fpr_get_d(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
            if funct==0x3c: return ['if (irix_guest_fpr_get_d(cpu, %d) < irix_guest_fpr_get_d(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
            if funct==0x3e: return ['if (irix_guest_fpr_get_d(cpu, %d) <= irix_guest_fpr_get_d(cpu, %d)) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);'%(fs,ft)],None
        if fmt==0x14 and funct==0x20: return ['irix_guest_fpr_set_s(cpu, %d, (float)(int32_t)(uint32_t)cpu->fpr[%d]);'%(fd,fs)],None
        if fmt==0x14 and funct==0x21: return ['irix_guest_fpr_set_d(cpu, %d, (double)(int32_t)(uint32_t)cpu->fpr[%d]);'%(fd,fs)],None
        if fmt==0x15 and funct==0x20: return ['irix_guest_fpr_set_s(cpu, %d, (float)(int64_t)cpu->fpr[%d]);'%(fd,fs)],None
        if fmt==0x15 and funct==0x21: return ['irix_guest_fpr_set_d(cpu, %d, (double)(int64_t)cpu->fpr[%d]);'%(fd,fs)],None
        raise ValueError('unsupported COP1 fmt=0x%x funct=0x%x at 0x%08x word=0x%08x'%(fmt,funct,addr,insn))

    raise ValueError('unsupported op=0x%x at 0x%08x word=0x%08x'%(op,addr,insn))


def c_name(name): return re.sub(r'[^A-Za-z0-9_]', '_', name)

def static_control_info(addr, insn):
    """Describe statically-resolvable MIPS control flow for the C backend."""
    op = insn >> 26
    rs = (insn >> 21) & 31
    rt = (insn >> 16) & 31
    rd = (insn >> 11) & 31
    fn = insn & 63
    imm = sx16(insn & 0xffff)

    if op == 0:
        if fn == 0x08:
            return {'kind': 'return' if rs == 31 else 'jump_indirect',
                    'likely': False}
        if fn == 0x09:
            return {'kind': 'call_indirect' if rd else 'jump_indirect',
                    'likely': False}

    if op == 0x02:
        return {'kind': 'jump',
                'target': ((addr + 4) & 0xf0000000) |
                          ((insn & 0x03ffffff) << 2),
                'likely': False}
    if op == 0x03:
        return {'kind': 'call',
                'target': ((addr + 4) & 0xf0000000) |
                          ((insn & 0x03ffffff) << 2),
                'ret': (addr + 8) & 0xffffffff,
                'likely': False}

    if op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):
        return {'kind': 'jump',
                'target': (addr + 4 + (imm << 2)) & 0xffffffff,
                'likely': op in (0x14, 0x15, 0x16, 0x17)}

    if op == 0x01:
        if rt in (0x00, 0x01, 0x02, 0x03):
            return {'kind': 'jump',
                    'target': (addr + 4 + (imm << 2)) & 0xffffffff,
                    'likely': rt in (0x02, 0x03)}
        if rt in (0x10, 0x11, 0x12, 0x13):
            return {'kind': 'link',
                    'target': (addr + 4 + (imm << 2)) & 0xffffffff,
                    'ret': (addr + 8) & 0xffffffff,
                    'likely': rt in (0x12, 0x13)}

    if op == 0x11 and rs == 0x08:
        return {'kind': 'jump',
                'target': (addr + 4 + (imm << 2)) & 0xffffffff,
                'likely': bool(rt & 2)}

    return None


def static_branch_condition(addr, insn):
    """Return a C expression for a statically-decodable conditional branch."""
    op = insn >> 26
    rs = (insn >> 21) & 31
    rt = (insn >> 16) & 31
    if op in (0x04, 0x14):
        return '%s == %s' % (reg(rs), reg(rt))
    if op in (0x05, 0x15):
        return '%s != %s' % (reg(rs), reg(rt))
    if op in (0x06, 0x16):
        return '(int64_t)%s <= 0' % reg(rs)
    if op in (0x07, 0x17):
        return '(int64_t)%s > 0' % reg(rs)
    if op == 0x01:
        if rt in (0x00, 0x02):
            return '(int64_t)%s < 0' % reg(rs)
        if rt in (0x01, 0x03, 0x11, 0x13):
            return '(int64_t)%s >= 0' % reg(rs)
    if op == 0x11 and rs == 0x08:
        cond = '(cpu->fcr31 & (1u << 23)) != 0'
        if not (rt & 1):
            cond = '!(%s)' % cond
        return cond
    return None


def static_cfg_entries(decoded, start, size):
    """Return addresses that can legitimately receive local dynamic control."""
    entries = {start}
    end = start + size
    for addr, word, _lines, control in decoded:
        if not control:
            continue
        ctl = static_control_info(addr, word)
        if ctl and 'target' in ctl and start <= ctl['target'] < end:
            entries.add(ctl['target'])
        if ctl and ctl.get('kind') in ('call', 'call_indirect', 'link'):
            ret = ctl.get('ret', (addr + 8) & 0xffffffff)
            if start <= ret < end:
                entries.add(ret)
        elif ctl is None:
            ret = (addr + 8) & 0xffffffff
            if start <= ret < end:
                entries.add(ret)
    return entries


def static_block_labels(decoded, start, size):
    """Labels required by emitted static CFG edges.

    Ordinary sequential instructions deliberately have no C label.  Delay
    slots are block entries because control instructions jump to them, and
    branch-likely needs the post-delay address as a second successor.
    """
    labels = set(static_cfg_entries(decoded, start, size))
    for idx, (addr, word, _lines, control) in enumerate(decoded):
        if not control:
            continue
        if idx + 1 < len(decoded):
            labels.add(decoded[idx + 1][0])
        ctl = static_control_info(addr, word)
        # Native conditional-branch lowering explicitly jumps to the
        # post-delay fallthrough block, so it must have a C label.
        if (ctl and static_branch_condition(addr, word) is not None and
                idx + 2 < len(decoded)):
            labels.add(decoded[idx + 2][0])
    return labels


def emit_range(path, name, start, size, c_prefix='', shndx=None,
               section_base=0, recover_trailing_delay_slot=False):
    # SGI libXm's static GetDisplay() is recovered as a truncated 0x70-byte
    # range at 0x0b294240 even though the function continues into shell
    # creation/realization code.  Extend this one known range so execution
    # reaches the real epilogue instead of returning with stale v0 from the
    # preceding void XtGetApplicationNameAndClass() call.
    if start == 0x0b294240 and size < 0x110:
        size = 0x110
    """Emit one function from an already-resolved executable address range.

    All front ends use this routine so symbol-backed and metadata-recovered
    functions share exactly the same instruction/control-flow emitter.
    """
    elf=Elf32BE(path)
    if elf.etype==1:
        if shndx is None:
            raise ValueError('ET_REL emit_range requires symbol section')
        words=elf.words(start,size,shndx=shndx,section_base=section_base)
    else:
        words=elf.words(start,size)
    decoded=[]
    for i,word in enumerate(words):
        addr=start+i*4
        lines,control=decode(addr,word)
        decoded.append((addr,word,lines,control))

    # Metadata-derived ranges can end on a control transfer while omitting its
    # architectural delay slot.  Recovery callers may request that one word.
    if recover_trailing_delay_slot and decoded and decoded[-1][3] == 'control':
        slot_addr=start+size
        try:
            if elf.etype==1:
                slot_word=elf.words(slot_addr,4,shndx=shndx,section_base=section_base)[0]
            else:
                slot_word=elf.words(slot_addr,4)[0]
            slot_lines,slot_control=decode(slot_addr,slot_word)
            decoded.append((slot_addr,slot_word,slot_lines,slot_control))
            size+=4
        except Exception:
            pass

    fn='irix_recomp_%s%s'%(c_name(c_prefix),c_name(name))
    print('/* Generated from %s: %s @ 0x%08x, 0x%x bytes. */'%(path,name,start,size))
    print('#include "IrixGuest.h"'); print('#include "IrixMipsInterp.h"'); print('#include <math.h>'); print('#include <stdio.h>'); print('')
    print('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN, IRIX_PENDING_LINK };'); print('')
    print('void %s(IrixGuestRuntime *rt, IrixGuestCpu *cpu)'%fn); print('{')
    has_delay_slot = any(
        idx and decoded[idx - 1][0] + 4 == addr and
        decoded[idx - 1][3] == 'control'
        for idx, (addr, _word, _lines, _control) in enumerate(decoded)
    )
    emitted_lines = '\n'.join(
        line for _addr, _word, lines, _control in decoded for line in lines
    )
    control_infos = [
        static_control_info(addr, word)
        for addr, word, _lines, control in decoded if control
    ]
    # A local PC dispatcher is only required when translated control can
    # return to an address chosen at runtime. Direct branches/calls resolve
    # to C labels after their delay slots; an ordinary jr $ra exits the host
    # function. Keep dispatch for indirect linked calls and intra-function
    # branch-and-link returns, whose return PC is represented dynamically.
    needs_local_dispatch = any(
        ctl is None or ctl.get('kind') == 'call_indirect' or
        (ctl.get('kind') == 'link' and
         start <= ctl.get('target', 0) < start + size)
        for ctl in control_infos
    )
    print('    IrixGuestAddr pc = cpu->pc;')
    if has_delay_slot or 'pending_target' in emitted_lines:
        print('    IrixGuestAddr pending_target = 0;')
    if has_delay_slot or 'pending_return' in emitted_lines:
        print('    IrixGuestAddr pending_return = 0;')
    if has_delay_slot or 'pending_kind' in emitted_lines:
        print('    int pending_kind = IRIX_PENDING_NONE;')
    if has_delay_slot:
        print('    int old_pending;')
        print('    IrixGuestAddr old_target;')
        print('    IrixGuestAddr old_return;')
        print('    IrixGuestAddr local_link_returns[64];')
        print('    unsigned local_link_depth = 0;')
    print('')
    cfg_entries = static_cfg_entries(decoded, start, size)
    block_labels = static_block_labels(decoded, start, size)
    print('    goto L_%08x;' % start)
    print('')
    if has_delay_slot and needs_local_dispatch:
        print('dispatch_local:')
        print('    switch (pc) {')
        for entry in sorted(cfg_entries):
            print('    case 0x%08xu: goto L_%08x;' % (entry, entry))
        print('    default:')
        print('        /* Computed edge outside the recovered CFG: preserve correctness')
        print('         * by handing this path to the legacy interpreter. */')
        print('        (void)irix_mips_interp_call(rt, cpu, pc);')
        print('        return;')
        print('    }')
        print('')
    for idx,(addr,word,lines,control) in enumerate(decoded):
        is_delay_slot = bool(idx and decoded[idx - 1][0] + 4 == addr and
                             decoded[idx - 1][3] == 'control')
        if addr in block_labels:
            print('L_%08x:' % addr)
        print('    /* 0x%08x: 0x%08x */' % (addr, word))
        if is_delay_slot:
            print('    old_pending = pending_kind;')
            print('    old_target = pending_target;')
            print('    old_return = pending_return;')
            print('    pending_kind = IRIX_PENDING_NONE;')
        print('    pc = 0x%08xu;' % addr)
        print('    cpu->pc = pc;')
        print('    rt->current_pc = pc;')
        if addr in (0x0983931c, 0x098402b4):
            tag='DT_INIT' if addr==0x0983931c else '__cplinit'
            print('            fprintf(stderr, "IRIX libimdUtil init probe: %s pc=0x%08x sp=%%08x gp=%%08x ra=%%08x kNull.master=%%08x kEmpty.master=%%08x\\\\n", (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31], irix_guest_read32be(rt, 0x09852dccu), irix_guest_read32be(rt, 0x09852dd4u));' % (tag,addr))
        if addr == 0x0f6154e8:
            print('            cpu->gpr[8] = irix_guest_xt_ensure_resource_table(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9]);')
        if addr == 0x0f62caa8:
            print('            fprintf(stderr, "IRIX XtAppCreateShell entry: a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x\\n", (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9]);')
        if 0x0f62cb60 <= addr <= 0x0f62cb84:
            print('            fprintf(stderr, "IRIX XtAppCreateShell tail: pc=0x%08x insn=%08x v0=%%08x ra=%%08x sp=%%08x\\n", (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[31], (uint32_t)cpu->gpr[29]);' % (addr,word))
        if addr == 0x0f6156a4:
            print('            { IrixGuestAddr wc = 0u; IrixGuestAddr frame = (IrixGuestAddr)(uint32_t)cpu->gpr[7]; if ((uint32_t)cpu->gpr[2] == 0x0f6959d8u) { if (frame && irix_guest_ptr(rt, frame + 12u, 4u)) wc = irix_guest_read32be(rt, frame + 12u); if (!wc) wc = 0x0f695a28u; irix_guest_xt_repair_class_resources(rt, wc); cpu->gpr[2] = irix_guest_read32be(rt, wc + 44u); cpu->gpr[16] = cpu->gpr[2]; } }')
        if addr == 0x0f62b6a4:
            print('            { IrixGuestAddr w = (IrixGuestAddr)(uint32_t)cpu->gpr[20]; if (w && irix_guest_ptr(rt, w + 4u, 4u)) { IrixGuestAddr wc = irix_guest_read32be(rt, w + 4u); if (wc) irix_guest_xt_repair_class_resources(rt, wc); } }')
        if addr == 0x0f615d60:
            print('            { IrixGuestAddr r = (IrixGuestAddr)(uint32_t)cpu->gpr[16]; uint32_t qcall = (uint32_t)cpu->gpr[13]; fprintf(stderr, "IRIX Xt default snapshot: s0=%08x type=%08x size=%08x def_type=%08x def_addr=%08x qcall=%08x\\n", (uint32_t)r, irix_guest_read32be(rt, r + 8u), irix_guest_read32be(rt, r + 12u), irix_guest_read32be(rt, r + 20u), irix_guest_read32be(rt, r + 24u), qcall); irix_guest_xt_normalize_resource(rt, r); fprintf(stderr, "IRIX Xt default normalized: def_type=%08x def_addr=%08x\\n", irix_guest_read32be(rt, r + 20u), irix_guest_read32be(rt, r + 24u)); }')
        if addr == 0x0b2a813c:
            print('            { IrixGuestAddr p = (IrixGuestAddr)(uint32_t)cpu->gpr[16]; fprintf(stderr, "IRIX 4Dwm table probe: pc=0x0b2a813c s0=%08x prev=%08x cur=%08x next=%08x next2=%08x ra=%08x gp=%08x\\n", (uint32_t)p, p >= 4u ? irix_guest_read32be(rt, p - 4u) : 0u, irix_guest_read32be(rt, p), irix_guest_read32be(rt, p + 4u), irix_guest_read32be(rt, p + 8u), (uint32_t)cpu->gpr[31], (uint32_t)cpu->gpr[28]); }')
        if addr == 0x0b2a80d8:
            print('            fprintf(stderr, "IRIX 4Dwm setup insns: 80d8=%08x 80dc=%08x 80e0=%08x 80e4=%08x 80e8=%08x 80ec=%08x 80f0=%08x 80f4=%08x 80f8=%08x 80fc=%08x 8100=%08x 8104=%08x 8108=%08x 810c=%08x 8110=%08x 8114=%08x 8118=%08x 811c=%08x 8120=%08x 8124=%08x\\n", irix_guest_read32be(rt,0x0b2a80d8u),irix_guest_read32be(rt,0x0b2a80dcu),irix_guest_read32be(rt,0x0b2a80e0u),irix_guest_read32be(rt,0x0b2a80e4u),irix_guest_read32be(rt,0x0b2a80e8u),irix_guest_read32be(rt,0x0b2a80ecu),irix_guest_read32be(rt,0x0b2a80f0u),irix_guest_read32be(rt,0x0b2a80f4u),irix_guest_read32be(rt,0x0b2a80f8u),irix_guest_read32be(rt,0x0b2a80fcu),irix_guest_read32be(rt,0x0b2a8100u),irix_guest_read32be(rt,0x0b2a8104u),irix_guest_read32be(rt,0x0b2a8108u),irix_guest_read32be(rt,0x0b2a810cu),irix_guest_read32be(rt,0x0b2a8110u),irix_guest_read32be(rt,0x0b2a8114u),irix_guest_read32be(rt,0x0b2a8118u),irix_guest_read32be(rt,0x0b2a811cu),irix_guest_read32be(rt,0x0b2a8120u),irix_guest_read32be(rt,0x0b2a8124u));')
        if 0x0b294288 <= addr < 0x0b294380:
            print('            fprintf(stderr, "IRIX Xm GetDisplay: pc=0x%08x insn=%08x v0=%%08x a0=%%08x a1=%%08x s0=%%08x s1=%%08x s2=%%08x s3=%%08x t9=%%08x ra=%%08x\\n", (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17], (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19], (uint32_t)cpu->gpr[25], (uint32_t)cpu->gpr[31]);' % (addr,word))
        if addr == 0x0b2a80d4:
            print('            fprintf(stderr, "IRIX 4Dwm call probe: pc=0x0b2a80d4 t9=%08x a0=%08x a1=%08x a2=%08x a3=%08x gp=%08x ra=%08x\\n", (uint32_t)cpu->gpr[25], (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31]);')
        if addr == 0x0b2a80d8:
            print('            { IrixGuestFunctionEntry *e = irix_guest_find_function(rt, (IrixGuestAddr)(uint32_t)cpu->gpr[25]); fprintf(stderr, "IRIX 4Dwm callee: t9=%08x name=%s a0=%08x\\n", (uint32_t)cpu->gpr[25], e && e->name ? e->name : "?", (uint32_t)cpu->gpr[4]); }')
        if addr == 0x0b2a80dc:
            print('            fprintf(stderr, "IRIX 4Dwm callee return: v0=%08x a0=%08x\\n", (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[4]);')
        if 0x0b2a80d8 <= addr <= 0x0b2a8138:
            print('            fprintf(stderr, "IRIX 4Dwm pretable trace: pc=0x%08x s0=%%08x s1=%%08x s2=%%08x s3=%%08x s4=%%08x s5=%%08x s6=%%08x s7=%%08x v0=%%08x v1=%%08x a0=%%08x a1=%%08x a2=%%08x a3=%%08x t0=%%08x t1=%%08x t2=%%08x t3=%%08x t4=%%08x t5=%%08x t6=%%08x t7=%%08x\\n", (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17], (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19], (uint32_t)cpu->gpr[20], (uint32_t)cpu->gpr[21], (uint32_t)cpu->gpr[22], (uint32_t)cpu->gpr[23], (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[3], (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7], (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9], (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11], (uint32_t)cpu->gpr[12], (uint32_t)cpu->gpr[13], (uint32_t)cpu->gpr[14], (uint32_t)cpu->gpr[15]);' % addr)
        ctl_here = static_control_info(addr, word) if control else None
        branch_cond = static_branch_condition(addr, word) if control else None
        # For direct conditional branches, preserve the branch decision in a
        # plain C boolean. The architectural delay slot still executes below,
        # but it no longer needs pending_kind/target as its CFG representation.
        if branch_cond is not None and ctl_here and ctl_here.get('kind') == 'jump':
            print('    int branch_taken_%08x = (%s);' % (addr, branch_cond))
            for line in lines:
                if ('pending_kind' not in line and
                        'pending_target' not in line and
                        'pc += 4' not in line and
                        line.strip() not in ('if (%s) {' % branch_cond, '} else {', '}')):
                    print('    '+line)
        else:
            for line in lines: print('    '+line)
        print('    if (rt->trapped) return;')
        print('    cpu->gpr[0] = 0;')

        # The instruction immediately before this one is its architectural
        # delay-slot owner.  For statically-known branch/call targets, resolve
        # the pending transfer directly to C labels instead of re-entering the
        # local-PC dispatcher.
        prev_ctl = None
        if is_delay_slot:
            prev_ctl = static_control_info(decoded[idx - 1][0],
                                           decoded[idx - 1][1])
        # Pending transfers exist only while executing a MIPS delay slot.
        # Ordinary straight-line instructions cannot observe pending state,
        # so do not emit the large generic resolver for every instruction.
        if is_delay_slot:
            if prev_ctl and 'target' in prev_ctl:
                target = prev_ctl['target']
                kind = prev_ctl['kind']
                if kind == 'jump':
                    owner_addr = decoded[idx - 1][0]
                    branch_cond = static_branch_condition(owner_addr, decoded[idx - 1][1])
                    if branch_cond is not None:
                        after = addr + 4
                        if start <= target < start + size:
                            print('    if (branch_taken_%08x) goto L_%08x;' % (owner_addr, target))
                        else:
                            print('    if (branch_taken_%08x) {' % owner_addr)
                            print('        if (irix_guest_call(rt, cpu, 0x%08xu) != 0) return;' % target)
                            print('        return;')
                            print('    }')
                        if start <= after < start + size:
                            print('    goto L_%08x;' % after)
                        else:
                            print('    return;')
                    elif start <= target < start + size:
                        print('    if (old_pending == IRIX_PENDING_JUMP && old_target == 0x%08xu) goto L_%08x;' %
                              (target, target))
                    else:
                        print('    if (old_pending == IRIX_PENDING_JUMP && old_target == 0x%08xu) {' % target)
                        print('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
                        print('        return;')
                        print('    }')
                elif kind == 'call':
                    ret = prev_ctl['ret']
                    print('    if (old_pending == IRIX_PENDING_CALL && old_target == 0x%08xu) {' % target)
                    print('        { static IrixGuestFunctionEntry *direct_entry_%08x;' % addr)
                    print('          IrixGuestFunctionEntry *direct_entry = direct_entry_%08x;' % addr)
                    print('          if (!direct_entry) {')
                    print('              direct_entry = irix_guest_find_function(rt, 0x%08xu);' % target)
                    print('              if (direct_entry && direct_entry->address == 0x%08xu)' % target)
                    print('                  direct_entry_%08x = direct_entry;' % addr)
                    print('          }')
                    print('          if (direct_entry && direct_entry->address == 0x%08xu) {' % target)
                    print('              if (irix_guest_call_entry(rt, cpu, 0x%08xu, direct_entry) != 0) return;' % target)
                    print('          } else if (irix_guest_call(rt, cpu, 0x%08xu) != 0) return;' % target)
                    print('        }')
                    if start <= ret < start + size:
                        print('        goto L_%08x;' % ret)
                    else:
                        print('        return;')
                    print('    }')
                elif kind == 'link':
                    ret = prev_ctl['ret']
                    print('    if (old_pending == IRIX_PENDING_LINK && old_target == 0x%08xu) {' % target)
                    if start <= target < start + size:
                        print('        if (local_link_depth >= sizeof(local_link_returns) / sizeof(local_link_returns[0])) {')
                        print('            irix_guest_trap(rt, cpu, 0xeeee0003u); return;')
                        print('        }')
                        print('        local_link_returns[local_link_depth++] = 0x%08xu;' % ret)
                        print('        goto L_%08x;' % target)
                    else:
                        print('        { static IrixGuestFunctionEntry *direct_entry_%08x;' % addr)
                        print('          IrixGuestFunctionEntry *direct_entry = direct_entry_%08x;' % addr)
                        print('          if (!direct_entry) {')
                        print('              direct_entry = irix_guest_find_function(rt, 0x%08xu);' % target)
                        print('              if (direct_entry && direct_entry->address == 0x%08xu)' % target)
                        print('                  direct_entry_%08x = direct_entry;' % addr)
                        print('          }')
                        print('          if (direct_entry && direct_entry->address == 0x%08xu) {' % target)
                        print('              if (irix_guest_call_entry(rt, cpu, 0x%08xu, direct_entry) != 0) return;' % target)
                        print('          } else if (irix_guest_call(rt, cpu, 0x%08xu) != 0) return;' % target)
                        print('        }')
                        if start <= ret < start + size:
                            print('        goto L_%08x;' % ret)
                        else:
                            print('        return;')
                    print('    }')

            print('    if (old_pending == IRIX_PENDING_RETURN) {')
            if needs_local_dispatch:
                print('        if (local_link_depth && old_target == local_link_returns[local_link_depth - 1]) {')
                print('            local_link_depth--; pc = old_target; goto dispatch_local;')
                print('        }')
            print('        return;')
            print('    }')
            print('    if (old_pending == IRIX_PENDING_CALL) {')
            print('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
            print('        pc = old_return; goto dispatch_local;')
            print('    }')
            print('    if (old_pending == IRIX_PENDING_LINK) {')
            print('        if (old_target >= 0x%08xu && old_target < 0x%08xu) {'%(start,start+size))
            print('            if (local_link_depth >= sizeof(local_link_returns) / sizeof(local_link_returns[0])) {')
            print('                irix_guest_trap(rt, cpu, 0xeeee0003u); return;')
            print('            }')
            print('            local_link_returns[local_link_depth++] = old_return;')
            print('            pc = old_target; goto dispatch_local;')
            print('        }')
            print('        if (irix_guest_call(rt, cpu, old_target) != 0) return;')
            print('        pc = old_return; goto dispatch_local;')
            print('    }')
            print('    if (old_pending == IRIX_PENDING_JUMP) {')
            print('        if (old_target < 0x%08xu || old_target >= 0x%08xu) {'%(start,start+size))
            print('            if (irix_guest_call(rt, cpu, old_target) != 0) return;')
            print('            return;')
            print('        }')
            print('        pc = old_target; goto dispatch_local;')
            print('    }')
        if control:
            ctl = static_control_info(addr, word)
            # Direct control transfers always execute the following delay
            # slot, except branch-likely instructions whose decoder advances
            # pc by one extra word when the branch is not taken.
            if idx + 1 < len(decoded):
                delay = decoded[idx + 1][0]
                after_delay = decoded[idx + 2][0] if idx + 2 < len(decoded) else None
                if ctl and ctl.get('likely') and after_delay is not None:
                    print('    if (pc == 0x%08xu) goto L_%08x;' % (addr, delay))
                    print('    goto L_%08x;' % after_delay)
                else:
                    print('    goto L_%08x;' % delay)
            else:
                print('    return;')
        elif idx + 1 < len(decoded):
            # Straight-line code falls through in C.  Emit no per-instruction
            # label/goto pair; a label appears only if the next instruction is
            # a genuine CFG entry for some other edge.
            pass
        else:
            print('    return;')
        print('')
    print('}')


def emit(path,name,section_base=None,c_prefix=''):
    elf=Elf32BE(path)
    sym_name,sym_value,size,shndx=elf.find_function(name)
    if not size:
        raise ValueError('symbol has zero size: %s'%name)
    if elf.etype==1:
        if section_base is None:
            section_base=0x50000000
        start=(section_base+sym_value)&0xffffffff
        return emit_range(path,sym_name,start,size,c_prefix,shndx,section_base)
    return emit_range(path,sym_name,sym_value,size,c_prefix)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('elf'); ap.add_argument('function'); ap.add_argument('--section-base',type=lambda x:int(x,0)); ap.add_argument('--c-prefix',default=''); ns=ap.parse_args()
    try: emit(ns.elf,ns.function,ns.section_base,ns.c_prefix)
    except Exception as exc:
        print('translate_n32.py: %s'%exc,file=sys.stderr); return 1
    return 0

if __name__=='__main__': raise SystemExit(main())
