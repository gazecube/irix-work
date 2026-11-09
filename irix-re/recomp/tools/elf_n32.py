#!/usr/bin/env python3
"""Reader for linked big-endian N32 MIPS ELF files (IRIX shared libraries and executables).

Gives the allocated image, the dynamic symbol table, the GOT layout, dynamic relocations and function
boundaries recovered from .debug_frame (the files are stripped of .symtab)."""
import struct
from pathlib import Path

PT_LOAD = 1


class Elf:
    def __init__(self, path):
        self.path = str(path)
        d = self.data = Path(path).read_bytes()
        assert d[:4] == b'\x7fELF' and d[4] == 1 and d[5] == 2, 'not a 32-bit big-endian ELF'
        (self.etype, self.machine, _v, self.entry, self.phoff, self.shoff, self.flags, _eh, self.phentsize, self.phnum,
         self.shentsize, self.shnum, self.shstrndx) = struct.unpack_from('>HHIIIIIHHHHHH', d, 16)
        self.sec = []
        raw = [struct.unpack_from('>IIIIIIIIII', d, self.shoff + i * self.shentsize) for i in range(self.shnum)]
        strs = raw[self.shstrndx]
        names = d[strs[4]:strs[4] + strs[5]]
        for i, x in enumerate(raw):
            e = names.find(b'\0', x[0])
            self.sec.append(dict(no=i, name=names[x[0]:e].decode(), type=x[1], flags=x[2], addr=x[3], off=x[4], size=x[5],
                                 link=x[6], info=x[7], align=x[8] or 1, entsz=x[9]))
        self.byname = {s['name']: s for s in self.sec}
        self.segs = []
        for i in range(self.phnum):
            t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('>IIIIIIII', d, self.phoff + i * self.phentsize)
            self.segs.append(dict(type=t, off=off, vaddr=va, filesz=fsz, memsz=msz, flags=fl))
        self.dyn = {}
        self.needed = []
        ds = self.byname.get('.dynamic')
        if ds:
            for k in range(0, ds['size'], 8):
                tag, val = struct.unpack_from('>iI', d, ds['off'] + k)
                if tag == 0:
                    break
                if tag == 1:
                    self.needed.append(val)
                self.dyn.setdefault(tag, val)
        self.dynstr = self.read_sec('.dynstr') if '.dynstr' in self.byname else b''
        self.needed = [self.cstr(self.dynstr, n) for n in self.needed]
        self.base = self.dyn.get(0x70000006, 0)            # MIPS_BASE_ADDRESS
        self.local_gotno = self.dyn.get(0x7000000a, 0)
        self.gotsym = self.dyn.get(0x70000013, 0)
        self.symtabno = self.dyn.get(0x70000011, 0)
        self.pltgot = self.dyn.get(3, 0)
        self.dynsyms = self.read_dynsym()

    @staticmethod
    def cstr(b, off):
        return b[off:b.index(b'\0', off)].decode()

    def read_sec(self, name):
        s = self.byname[name]
        return self.data[s['off']:s['off'] + s['size']]

    def read_dynsym(self):
        out = []
        s = self.byname.get('.dynsym')
        if not s:
            return out
        for i in range(s['size'] // 16):
            n, v, z, info, other, sh = struct.unpack_from('>IIIBBH', self.data, s['off'] + 16 * i)
            out.append(dict(no=i, name=self.cstr(self.dynstr, n) if n else '', value=v, size=z, bind=info >> 4,
                            type=info & 15, shndx=sh))
        return out

    # -- image ---------------------------------------------------------------------------
    def allocated(self):
        return [s for s in self.sec if s['flags'] & 2 and s['size']]

    def section_at(self, addr):
        for s in self.allocated():
            if s['addr'] <= addr < s['addr'] + s['size']:
                return s
        return None

    def bytes_at(self, addr, n):
        s = self.section_at(addr)
        if s is None or s['type'] == 8:
            return bytes(n)
        o = s['off'] + addr - s['addr']
        return self.data[o:o + n]

    def word(self, addr):
        return struct.unpack('>I', self.bytes_at(addr, 4))[0]

    # -- GOT -----------------------------------------------------------------------------
    def got_entry(self, idx):
        """('local', value) or ('global', dynsym record) for GOT index idx."""
        if idx < self.local_gotno:
            return ('local', self.word(self.pltgot + 4 * idx))
        k = self.gotsym + idx - self.local_gotno
        return ('global', self.dynsyms[k])

    @property
    def gp(self):
        return self.pltgot + 0x7ff0

    # -- dynamic relocations -------------------------------------------------------------
    def rel_dyn(self):
        s = self.byname.get('.rel.dyn')
        out = []
        if not s:
            return out
        for q in range(0, s['size'], 8):
            off, info = struct.unpack_from('>II', self.data, s['off'] + q)
            out.append((off, info >> 8, info & 255))
        return out

    # -- function boundaries from .debug_frame ----------------------------------------------
    def frame_functions(self):
        s = self.byname.get('.debug_frame')
        if not s:
            return []
        d = self.data[s['off']:s['off'] + s['size']]
        cies, out, p = {}, [], 0
        while p + 8 <= len(d):
            ln, cid = struct.unpack_from('>II', d, p)
            if ln == 0:
                break
            if cid == 0xffffffff:
                cies[p] = True
            else:
                loc, rng = struct.unpack_from('>II', d, p + 8)
                if rng:
                    out.append((loc, rng))
            p += 4 + ln
        return sorted(set(out))


if __name__ == '__main__':
    import sys
    e = Elf(sys.argv[1])
    fr = e.frame_functions()
    print('needed', e.needed)
    print('base %#x gp %#x local_gotno %d gotsym %d symtabno %d' % (e.base, e.gp, e.local_gotno, e.gotsym, e.symtabno))
    print('frame functions', len(fr), 'text', hex(e.byname['.text']['addr']), hex(e.byname['.text']['size']))
    exp = [s for s in e.dynsyms if s['type'] == 2 and s['shndx'] and s['name']]
    starts = {a for a, _ in fr}
    print('exported FUNC', len(exp), 'of which start of a frame function:', sum(1 for s in exp if s['value'] in starts))
    print('rel.dyn', len(e.rel_dyn()))
