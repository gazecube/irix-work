#!/usr/bin/env python3
"""Reader for linked big-endian N64 MIPS ELF files (IRIX 6.5 shared libraries and executables).

Same interface as elf_n32.Elf, over ELF64: 64-bit headers, 24-byte symbols, 8-byte GOT entries, 64-bit DWARF
frame records, and the version 6 .MIPS.Xlate table of objects rearranged after linking."""
import bisect
import struct
from pathlib import Path

R_MIPS_REL32 = 3
R_MIPS_64 = 18


class Elf:
    def __init__(self, path):
        self.path = str(path)
        d = self.data = Path(path).read_bytes()
        assert d[:4] == b'\x7fELF' and d[4] == 2 and d[5] == 2, 'not a 64-bit big-endian ELF'
        (self.etype, self.machine, _v, self.entry, self.phoff, self.shoff, self.flags, _eh, self.phentsize, self.phnum,
         self.shentsize, self.shnum, self.shstrndx) = struct.unpack_from('>HHIQQQIHHHHHH', d, 16)
        self.sec = []
        raw = [struct.unpack_from('>IIQQQQIIQQ', d, self.shoff + i * self.shentsize) for i in range(self.shnum)]
        strs = raw[self.shstrndx]
        names = d[strs[4]:strs[4] + strs[5]]
        for i, x in enumerate(raw):
            e = names.find(b'\0', x[0])
            self.sec.append(dict(no=i, name=names[x[0]:e].decode(), type=x[1], flags=x[2], addr=x[3], off=x[4], size=x[5],
                                 link=x[6], info=x[7], align=x[8] or 1, entsz=x[9]))
        self.byname = {s['name']: s for s in self.sec}
        self.segs = []
        for i in range(self.phnum):
            t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('>IIQQQQQQ', d, self.phoff + i * self.phentsize)
            self.segs.append(dict(type=t, off=off, vaddr=va, filesz=fsz, memsz=msz, flags=fl))
        self.dyn = {}
        self.needed = []
        ds = self.byname.get('.dynamic')
        if ds:
            for k in range(0, ds['size'], 16):
                tag, val = struct.unpack_from('>qQ', d, ds['off'] + k)
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
        for i in range(s['size'] // 24):
            n, info, other, sh, v, z = struct.unpack_from('>IBBHQQ', self.data, s['off'] + 24 * i)
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

    def dword(self, addr):
        return struct.unpack('>Q', self.bytes_at(addr, 8))[0]

    # -- GOT (8-byte entries) --------------------------------------------------------------
    def got_entry(self, idx):
        """('local', value) or ('global', dynsym record) for GOT index idx."""
        if idx < self.local_gotno:
            return ('local', self.dword(self.pltgot + 8 * idx))
        k = self.gotsym + idx - self.local_gotno
        return ('global', self.dynsyms[k])

    @property
    def gp(self):
        return self.pltgot + 0x7ff0

    # -- dynamic relocations -------------------------------------------------------------
    def rel_dyn(self):
        """(offset, symbol index, types) per entry.  An ELF64 MIPS Rel entry carries up to three relocation types that
        apply in sequence; for a quickstarted library it is R_MIPS_REL32 followed by R_MIPS_64: one 64-bit word."""
        s = self.byname.get('.rel.dyn')
        out = []
        if not s:
            return out
        for q in range(0, s['size'], 16):
            off, sym, ssym, t3, t2, t1 = struct.unpack_from('>QIBBBB', self.data, s['off'] + q)
            out.append((off, sym, (t1, t2, t3)))
        return out

    # -- function boundaries ----------------------------------------------------------------
    def xlate_table(self):
        """The .MIPS.Xlate table (version 6, 64-bit fields) of an object rearranged after linking: a sorted list of
        (new_address, old_address, size).  Header: version, kind, block size, block count, entry count, then the
        low and high addresses; per block a record of five 64-bit values (first new, first old, entries, old low,
        old high), and the blocks of LEB128 pairs (size / 4, old address step / 4) start after the records."""
        if hasattr(self, '_xlate'):
            return self._xlate
        self._xlate = []
        s = self.byname.get('.MIPS.Xlate')
        if not s:
            return self._xlate
        d = self.data[s['off']:s['off'] + s['size']]
        version, kind = struct.unpack_from('>IB3x', d, 0)
        bsize, nblocks, total = struct.unpack_from('>I4xQQ', d, 8)
        if version != 6 or kind != 1:
            raise ValueError('.MIPS.Xlate version %d kind %d is not supported' % (version, kind))

        def leb(p, signed):
            v = sh = 0
            while True:
                b = d[p]
                p += 1
                v |= (b & 0x7f) << sh
                sh += 7
                if not b & 0x80:
                    if signed and b & 0x40:
                        v -= 1 << sh
                    return v, p
        hdr = 0x58
        for k in range(nblocks):
            new, old, n, _, _ = struct.unpack_from('>5Q', d, hdr + 40 * k)
            p = hdr + 40 * nblocks + bsize * k
            for _ in range(n):
                size, p = leb(p, False)
                step, p = leb(p, True)
                old += 4 * step
                self._xlate.append((new, old, 4 * size))
                new += 4 * size
        assert len(self._xlate) == total, (len(self._xlate), total)
        self._xlate.sort()
        return self._xlate

    def old_to_new(self, a):
        """Address in the rearranged text for an address the DWARF sections give (identity without a table)."""
        t = self.xlate_table()
        if not t:
            return a
        if not hasattr(self, '_xlate_old'):
            self._xlate_old = sorted((o, n, z) for n, o, z in t)
            self._xlate_keys = [o for o, _, _ in self._xlate_old]
        k = bisect.bisect_right(self._xlate_keys, a) - 1
        if k >= 0:
            o, n, z = self._xlate_old[k]
            if a < o + z:
                return n + (a - o)
        return a

    def frame_functions(self):
        """(start, size) of every function with a DWARF frame record.  IRIX N64 uses 64-bit records: an 8-byte
        length, an 8-byte CIE id (all ones for a CIE, else the CIE offset), and in an FDE an 8-byte start and range."""
        s = self.byname.get('.debug_frame')
        if not s:
            return []
        d = self.data[s['off']:s['off'] + s['size']]
        out, p = [], 0
        while p + 16 <= len(d):
            ln, cid = struct.unpack_from('>QQ', d, p)
            if ln == 0:
                break
            if cid != 0xffffffffffffffff:
                loc, rng = struct.unpack_from('>QQ', d, p + 16)
                if rng:
                    out.append((self.old_to_new(loc), rng))
            p += 8 + ln
        return sorted(set(out))


if __name__ == '__main__':
    import sys
    e = Elf(sys.argv[1])
    fr = e.frame_functions()
    print('needed', e.needed)
    print('base %#x gp %#x local_gotno %d gotsym %d symtabno %d' % (e.base, e.gp, e.local_gotno, e.gotsym, e.symtabno))
    t = e.byname['.text']
    print('text %#x size %#x' % (t['addr'], t['size']))
    print('frame functions', len(fr), 'xlate entries', len(e.xlate_table()))
    exp = [s for s in e.dynsyms if s['type'] == 2 and s['shndx'] and s['name']]
    starts = {a for a, _ in fr}
    xs = {n for n, _, _ in e.xlate_table()}
    print('exported FUNC', len(exp), 'that start a frame function:', sum(1 for s in exp if s['value'] in starts),
          'that start an xlate range:', sum(1 for s in exp if s['value'] in xs))
    sizes = {a: z for a, z in fr}
    print('exports whose size equals the frame range at the same address:',
          sum(1 for s in exp if sizes.get(s['value']) == s['size']))
    rd = e.rel_dyn()
    print('rel.dyn', len(rd), 'types', sorted({r[2] for r in rd}))
