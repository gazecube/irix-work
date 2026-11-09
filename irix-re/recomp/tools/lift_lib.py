#!/usr/bin/env python3
"""Lift an N32 static archive to native C + assembler: an ordinary i386 ELF library, no CPU model.

    lift_lib.py LIB.a CORPUS.so OUTDIR [--profile data.json]

Per object: <stem>.c (every function lifted to a cdecl C function, registers as locals) and
<stem>.s (data sections with real relocations).  Addresses are resolved symbolically from each
instruction's relocation (CALL16, GOT_DISP, GOT_PAGE/GOT_OFST, GPREL16), so no image is laid out.
`$gp` is the constant 0 in lifted code: every gp-relative form carries a relocation and becomes a
symbol reference.  Functions that cannot be lifted yet become stubs that abort(); the report lists them.
"""
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
sys.path.insert(0, str(here.parents[2] / 'native-libs' / 'nativemem' / 'tools'))
os.environ['NATIVE_MEM'] = '1'
import link_stack_n32 as L          # noqa: E402
import lift_n32 as M                # noqa: E402
from lift_n32 import Insn, Unsupported, R, lhs, sx16, is_control, is_branch, branch_kind, is_likely, \
    branch_target, reads_writes, ARGREGS   # noqa: E402

DET_SITES = []     # (object, function, text offset, imm, type) per narrow load; index = detector site id
R_32, R_26, R_HI, R_LO, R_GPREL16, R_CALL16, R_GOT_DISP, R_GOT_PAGE, R_GOT_OFST, R_SUB, R_JALR = \
    2, 4, 5, 6, 7, 11, 19, 20, 21, 24, 37
SKIP_SEC = ('.debug', '.MIPS.events', '.MIPS.content', '.reginfo', '.MIPS.abiflags', '.mdebug', '.comment',
            '.note', '.gnu', '.pdr', '.options', '.IRIX')


def tag_of(path):
    return re.sub(r'[^A-Za-z0-9]', '_', path)


class ObjInfo:
    def __init__(self, o, idx):
        self.o, self.idx = o, idx
        self.tag = 'o%d_%s' % (idx, tag_of(o.name.rsplit('.', 1)[0]))
        self.relocs = defaultdict(lambda: defaultdict(list))     # section no -> offset -> [(type, symidx, addend)]
        for rs in o.sec:
            if rs['type'] not in (L.SHT_REL, L.SHT_RELA):
                continue
            tgt = rs['info']
            ent = rs['entsz'] or (12 if rs['type'] == L.SHT_RELA else 8)
            for q in range(rs['off'], rs['off'] + rs['size'], ent):
                if rs['type'] == L.SHT_RELA:
                    ro, ri, a = struct.unpack_from('>IIi', o.data, q)
                    self.relocs[tgt][ro].append((ri & 255, ri >> 8, a, True))
                else:
                    ro, ri = struct.unpack_from('>II', o.data, q)
                    self.relocs[tgt][ro].append((ri & 255, ri >> 8, None, False))
        self.text = [s for s in o.sec if s['name'] == '.text' and s['size']]
        self.funcs = {}          # text section no -> list of (value, size, symbol)
        for s in o.syms:
            if s['type'] == L.STT_FUNC and s['shndx'] != 0 and o.sec[s['shndx']]['name'] == '.text' and s['size']:
                self.funcs.setdefault(s['shndx'], []).append((s['value'], s['size'], s))

    def symname(self, s):
        """The assembler-level name of a symbol of this object."""
        if s['type'] == L.STT_SECTION:
            return '%s__sec%d' % (self.tag, s['shndx'])
        if s['shndx'] == L.SHN_UNDEF:
            return s['name']
        if s['bind'] in (L.STB_GLOBAL, L.STB_WEAK) and s['name']:
            return s['name']
        return '%s__%s_%d' % (self.tag, re.sub(r'[^A-Za-z0-9_]', '_', s['name']), s['no'])

    def func_at(self, shndx, off):
        for v, z, s in self.funcs.get(shndx, []):
            if v == off:
                return s
        return None


class Syms:
    """C-side declarations of symbol addresses used by one translation unit."""
    def __init__(self):
        self.names = {}

    def addr(self, asmname):
        if asmname not in self.names:
            self.names[asmname] = 'S%d' % len(self.names)
        return '(uint32_t)(uintptr_t)%s' % self.names[asmname]

    def decls(self):
        return ''.join('extern char %s[] __asm__("%s");\n' % (c, n) for n, c in self.names.items())


# ---------------------------------------------------------------------------------------------
class FuncLifter:
    def __init__(self, info, shndx, start_off, size, name, syms, tables):
        self.info, self.o = info, info.o
        sec = self.o.sec[shndx]
        self.shndx, self.start, self.size, self.name = shndx, start_off, size, name
        self.syms = syms
        self.tables = tables
        base = sec['off'] + start_off
        self.words = [struct.unpack_from('>I', self.o.data, base + 4 * k)[0] for k in range(size // 4)]
        self.rel = info.relocs.get(shndx, {})
        self.insns = [Insn(start_off + 4 * k, w) for k, w in enumerate(self.words)]
        self.byaddr = {i.addr: i for i in self.insns}
        self.end = start_off + size
        self.jt_labels = set()
        self.jt_arrays = []
        self.tag = {}                  # register -> (section symbol, table start offset, address expr)
        self.pending_tag = None
        self.max_out = 0
        self.hdr_regs = self.compute_hdr_regs()
        # X events (XButtonEvent etc.): x, y, x_root, y_root are ints at +32/+36/+40/+44; (Position)event->x is a
        # halfword read of the low half at +34/+38/+42/+46 (BE), i.e. +32/... in little-endian memory.
        self.event_regs = set()
        if self.o.name in ('TextIn.o', 'TextF.o', 'Text.o', 'TextOut.o', 'List.o', 'TextStrSo.o', 'Selection.o'):
            self.event_regs = {5}
            for i in self.insns:
                if i.op == 0 and i.fn in (0x25, 0x21, 0x2d) and i.w:
                    if i.rt == 0 and i.rs == 5 or i.rs == 0 and i.rt == 5:
                        self.event_regs.add(i.rd)
        self.screen_regs = {i.rt for i in self.insns if i.op == 0x23 and i.imm == 88 and i.rs not in (28, 29)}

    # -- symbolic address of a relocation --------------------------------------------------
    def reloc_addr(self, r, imm):
        typ, sn, a, is_rela = r
        sym = self.o.syms[sn]
        nm = self.info.symname(sym)
        if sym['type'] == L.STT_SECTION and self.o.sec[sym['shndx']]['name'] == '.text':
            tf = self.info.func_at(sym['shndx'], (a if is_rela else 0) if typ != R_GOT_OFST else (a or 0))
            if tf is None:
                raise Unsupported('address of a text offset without a function symbol at %08x' % 0)
            return '(%s + 0)' % self.syms.addr(self.info.symname(tf)), tf, 0
        addend = a if is_rela else None
        if addend is None:
            addend = sx16(imm & 0xffff) if typ in (R_GPREL16, R_GOT_OFST) else 0
        if typ == R_GPREL16 and sym['type'] == L.STT_SECTION and sym['bind'] == L.STB_LOCAL:
            addend += self.o.gp0
        return '(%s + %d)' % (self.syms.addr(nm), addend), sym, addend

    def stmt(self, i):
        """C statement for one instruction (no control transfer)."""
        w = i.w
        if w == 0:
            return None
        rl = self.rel.get(i.addr, [])
        types = [r[0] for r in rl if r[0] != R_JALR]
        op = i.op
        if op in (0x2f, 0x22, 0x26, 0x2a, 0x2e):
            raise Unsupported('op 0x%x (unaligned word) at %08x' % (op, i.addr))
        if op in (0x1b, 0x2d):
            return None          # ldr/sdr: the doubleword access is done at the ldl/sdl of the pair
        if op == 0x11 or op in (0x31, 0x35, 0x39, 0x3d):
            return self.fp(i, rl, types)
        if op in (0x1a, 0x2c):
            return self.unaligned_dw(i, rl, types)
        # writes that never matter: gp and sp updates
        wr = self.dest(i)
        if wr in (28,) :
            return None
        if types:
            return self.reloc_stmt(i, rl, types)
        if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b, 0x37, 0x3f):
            return self.mem(i, None)
        if op == 0x09 and i.rs == 29 and i.rt == 29:
            return None
        if op in (0x08, 0x09) and i.rs == 29:
            if 0 <= i.imm < self.frame:
                return '%s = (uint32_t)(uintptr_t)(frame + %d);' % (lhs(i.rt), i.imm)
            raise Unsupported('address of incoming slot at %08x' % i.addr)
        if op == 0 and i.fn in (0x21, 0x25, 0x2d) and (i.rs == 29 or i.rt == 29) and i.rd != 29:
            # move rd, sp
            src = i.rt if i.rs == 0 else i.rs
            if src == 29 or i.rs == 29:
                return '%s = (uint32_t)(uintptr_t)frame;' % lhs(i.rd)
        if op == 0 and i.fn in (0x21, 0x23, 0x2d, 0x2f) and i.rd == 29 and i.rs == 29 and getattr(self, 'reg_frame', None) == i.rt:
            return None                   # frame set-up or tear-down through a register (frames over 32K)
        if i.rt == 29 and op != 0 or (op == 0 and i.rd == 29):
            raise Unsupported('write to sp at %08x' % i.addr)
        if op == 0 and i.fn in (0x34, 0x30, 0x31, 0x32, 0x33, 0x36):   # trap-on-condition: overflow/zero-divide checks
            return None
        if op == 0 and i.fn == 0x3c:      # dsll32 : fused with the following dsra32/dsrl32
            return '%s = %s << %d;' % (lhs(i.rd), R(i.rt), i.sa)
        if op == 0 and i.fn in (0x3b, 0x3e, 0x3f) and False:
            pass
        if op == 0 and i.fn == 0x3f:      # dsra32
            return '%s = (uint32_t)((int32_t)%s >> %d);' % (lhs(i.rd), R(i.rt), i.sa)
        if op == 0 and i.fn == 0x3e:      # dsrl32
            return '%s = %s >> %d;' % (lhs(i.rd), R(i.rt), i.sa)
        if op == 0 and i.fn == 0x38:      # dsll: the low word; the high word only where the register is tracked as 64-bit
            if i.rd in self.widened:
                return '{ uint32_t l_ = %s; h%d = (%s << %d) | (l_ >> %d); %s = l_ << %d; }' % (
                    R(i.rt), i.rd, self.hi_of(i.rt), i.sa, 32 - i.sa, lhs(i.rd), i.sa) if i.sa else \
                    '{ h%d = %s; %s = %s; }' % (i.rd, self.hi_of(i.rt), lhs(i.rd), R(i.rt))
            return '%s = %s << %d;' % (lhs(i.rd), R(i.rt), i.sa)
        if op == 0 and i.fn in (0x1e, 0x1f):      # ddiv, ddivu: lo and hi keep their low words
            T = 'int64_t' if i.fn == 0x1e else 'uint64_t'
            return ('{ %s n_ = (%s)(((uint64_t)%s << 32) | %s), d_ = (%s)(((uint64_t)%s << 32) | %s); '
                    'if (d_) { lo = (uint32_t)(n_ / d_); hi = (uint32_t)(n_ %% d_); } }') % (
                        T, T, self.hi_of(i.rs), R(i.rs), T, self.hi_of(i.rt), R(i.rt))
        return M.alu(i)

    def compute_hdr_regs(self):
        """XmString.o: registers holding an optimized-XmString header pointer (a word loaded and then masked).
        The header packs pixel_width into the low half-word: BE offset +2, which in little-endian memory is +0."""
        regs = set()
        if self.o.name != 'XmString.o':
            return regs
        ins = self.insns
        for k, i in enumerate(ins):
            if i.op == 0x23 and i.imm == 0 and i.rs not in (29, 28):
                win = ins[k + 1:k + 9]
                if any((j.op == 0x0f and j.immu in (0x7fff, 0x0fff, 0x00ff, 0x3fff)) or (j.op == 0 and j.fn == 0x02 and j.sa == 31) for j in win):
                    regs.add(i.rs)
        # a register used for both a word access at 0 and a half-word access at 2 is the same header word
        w0 = {i.rs for i in ins if i.op in (0x23, 0x2b) and i.imm == 0}
        h2 = {i.rs for i in ins if i.op in (0x21, 0x25, 0x29) and i.imm == 2}
        regs |= (w0 & h2) - {28, 29}
        return regs

    def compute_widened(self):
        w = set()
        for i in self.insns:
            if i.op in (0x37, 0x1a):
                w.add(i.rt)
        changed = True
        while changed:
            changed = False
            for i in self.insns:
                mv = self.move_src(i)
                if mv is not None and mv in w and i.rd not in w and i.rd not in (0, 28, 29):
                    w.add(i.rd)
                    changed = True
        w.discard(0)
        return w

    def move_src(self, i):
        if i.op == 0 and i.fn in (0x25, 0x21, 0x2d) and i.w:
            if i.rt == 0:
                return i.rs
            if i.rs == 0:
                return i.rt
        return None

    def hi_of(self, n):
        return 'h%d' % n if n in self.widened else '(uint32_t)((int32_t)%s >> 31)' % R(n)

    def unaligned_dw(self, i, rl, types):
        if types and types != [R_GPREL16]:
            raise Unsupported('relocation on unaligned doubleword at %08x' % i.addr)
        addr = '(uintptr_t)(uint32_t)(%s + %d)' % (R(i.rs), i.imm)
        if i.op == 0x1a:      # ldl: raw eight-byte copy into the (hi, lo) pair
            return '{ uint32_t *p = (uint32_t *)%s; h%d = p[0]; %s = p[1]; }' % (addr, i.rt, lhs(i.rt))
        return '{ uint32_t *p = (uint32_t *)%s; p[0] = %s; p[1] = %s; }' % (addr, self.hi_of(i.rt), R(i.rt))

    def fp(self, i, rl, types):
        self.uses_fp = True
        op = i.op
        if types:
            if op in (0x31, 0x35, 0x39, 0x3d) and set(types) == {R_GPREL16}:
                ex, sym, _ = self.reloc_addr([x for x in rl if x[0] != R_JALR][0], i.immu)
                base = '' if i.rs == 28 else ' + %s' % R(i.rs)
                addr = '(uintptr_t)(uint32_t)(%s%s)' % (ex, base)
            else:
                raise Unsupported('relocation %s on fp op at %08x' % (types, i.addr))
        elif op in (0x31, 0x35, 0x39, 0x3d):
            if i.rs == 29:
                if i.imm >= self.frame:
                    raise Unsupported('fp access of an incoming slot at %08x' % i.addr)
                addr = '(uintptr_t)(frame + %d)' % i.imm
            else:
                addr = '(uintptr_t)(uint32_t)(%s + %d)' % (R(i.rs), i.imm)
        ft = i.rt
        if op == 0x31:   # lwc1
            return 'f[%d] = (f[%d] & 0xffffffff00000000ull) | *(uint32_t *)%s;' % (ft, ft, addr)
        if op == 0x35:   # ldc1: the two words as a native double (low word first)
            return 'f[%d] = *(uint64_t *)%s;' % (ft, addr)
        if op == 0x39:   # swc1
            return '*(uint32_t *)%s = (uint32_t)f[%d];' % (addr, ft)
        if op == 0x3d:   # sdc1
            return '*(uint64_t *)%s = f[%d];' % (addr, ft)
        fmt, fs, fd, fn = i.rs, i.rd, i.sa, i.fn
        if fmt == 0:
            return '%s = (uint32_t)f[%d];' % (lhs(i.rt), fs)
        if fmt == 1:
            return '%s = (uint32_t)f[%d];' % (lhs(i.rt), fs)
        if fmt == 4:
            return 'f[%d] = (f[%d] & 0xffffffff00000000ull) | %s;' % (fs, fs, R(i.rt))
        if fmt == 5:
            return 'f[%d] = (uint64_t)%s;' % (fs, R(i.rt))
        if fmt in (2, 6):
            raise Unsupported('cfc1/ctc1 at %08x' % i.addr)
        if fmt in (0x10, 0x11):
            d = fmt == 0x11
            G = 'FD(f[%d])' if d else 'FS(f[%d])'
            Pu = 'f[%d] = DF(%%s);' % fd if d else 'f[%d] = SF(f[%d], %%s);' % (fd, fd)
            a, b = G % fs, G % ft
            ar = {0: '%s + %s', 1: '%s - %s', 2: '%s * %s', 3: '%s / %s'}
            if fn in ar:
                return Pu % (ar[fn] % (a, b))
            if fn == 4:
                return Pu % ('%s(%s)' % ('sqrt' if d else 'sqrtf', a))
            if fn == 5:
                return Pu % ('%s(%s)' % ('fabs' if d else 'fabsf', a))
            if fn == 6:
                return 'f[%d] = f[%d];' % (fd, fs)
            if fn == 7:
                return Pu % ('-%s' % a)
            if fn == 0x09:   # trunc.l
                return 'f[%d] = (uint64_t)(int64_t)%s;' % (fd, a)
            if fn == 0x0d:   # trunc.w
                return 'f[%d] = (f[%d] & 0xffffffff00000000ull) | (uint32_t)(int32_t)%s;' % (fd, fd, a)
            if fn == 0x20:
                return 'f[%d] = SF(f[%d], (float)%s);' % (fd, fd, a)
            if fn == 0x21:
                return 'f[%d] = DF((double)%s);' % (fd, a)
            cmp = {0x32: '%s == %s', 0x3c: '%s < %s', 0x3e: '%s <= %s', 0x3a: '%s == %s', 0x34: '%s < %s', 0x36: '%s <= %s',
                   0x30: '0', 0x31: '(%s != %s)'}
            if fn in cmp:
                return 'fcc = %s;' % (cmp[fn] % (a, b) if '%s' in cmp[fn] else cmp[fn])
            raise Unsupported('fp funct 0x%x fmt 0x%x at %08x' % (fn, fmt, i.addr))
        if fmt == 0x14:   # from word
            if fn == 0x20:
                return 'f[%d] = SF(f[%d], (float)(int32_t)(uint32_t)f[%d]);' % (fd, fd, fs)
            if fn == 0x21:
                return 'f[%d] = DF((double)(int32_t)(uint32_t)f[%d]);' % (fd, fs)
        if fmt == 0x15:
            if fn == 0x20:
                return 'f[%d] = SF(f[%d], (float)(int64_t)f[%d]);' % (fd, fd, fs)
            if fn == 0x21:
                return 'f[%d] = DF((double)(int64_t)f[%d]);' % (fd, fs)
        raise Unsupported('cop1 fmt 0x%x funct 0x%x at %08x' % (fmt, fn, i.addr))

    def dest(self, i):
        if i.w == 0:
            return None
        op = i.op
        if op == 0:
            if i.fn in (0x08, 0x18, 0x19, 0x1a, 0x1b):
                return None
            return i.rd
        if op in (0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x19, 0x20, 0x21, 0x23, 0x24, 0x25, 0x37):
            return i.rt
        return None

    def mem(self, i, addr_override):
        op = i.op
        spec = {0x20: ('int8_t', 'ld'), 0x24: ('uint8_t', 'ld'), 0x21: ('int16_t', 'ld'), 0x25: ('uint16_t', 'ld'),
                0x23: ('uint32_t', 'ld'), 0x28: ('uint8_t', 'st'), 0x29: ('uint16_t', 'st'), 0x2b: ('uint32_t', 'st'),
                0x37: ('uint64_t', 'ld'), 0x3f: ('uint64_t', 'st')}
        ctype, kind = spec[op]
        if addr_override is None and op == 0x23 and i.rs in self.tag:
            sym, A, ex = self.tag[i.rs]
            ent = self.tables.get((sym['shndx'], A)) if sym['type'] == L.STT_SECTION else None
            if ent is not None:
                return self.jt_load(i, '%s + %d' % (R(i.rs), i.imm), sym, A, ent)
        if addr_override is None:
            if i.rs == 29:
                if i.imm < self.frame:
                    addr = '(frame + %d)' % i.imm
                else:
                    if ctype == 'uint64_t':
                        off = i.imm - self.frame
                        if kind == 'ld' and off % 8 == 0:
                            return '%s = s%d; h%d = (uint32_t)((int32_t)s%d >> 31);' % (lhs(i.rt), off // 8, i.rt, off // 8)
                        return self.bad(i)
                    return M.incoming(i, self.frame, ctype, kind)
            elif i.rs == 28:
                raise Unsupported('gp-relative memory access without relocation at %08x' % i.addr)
            else:
                imm = i.imm
                nr = self.info.narrow.get(i.addr) if hasattr(self.info, 'narrow') else None
                if nr is not None:
                    if nr[0] != imm:
                        raise Unsupported('narrow profile does not match the instruction at %08x' % i.addr)
                    imm = nr[1]
                elif op in (0x21, 0x25) and imm in (34, 38, 42, 46) and i.rs in self.event_regs:
                    imm -= 2        # (Position)event->x / y / x_root / y_root
                elif op in (0x21, 0x25) and imm in (14, 18, 22, 26) and i.rs in self.screen_regs:
                    imm -= 2        # (Dimension)WidthOfScreen etc: low half of the int field of Xlib's Screen
                elif op in (0x21, 0x25, 0x29) and imm == 2 and i.rs in self.hdr_regs:
                    imm = 0                         # pixel_width half-word of an optimized XmString header
                addr = '(uintptr_t)(uint32_t)(%s + %d)' % (R(i.rs), imm)
        else:
            addr = '(uintptr_t)(uint32_t)(%s)' % addr_override
        if ctype == 'uint64_t':
            if kind == 'ld':     # BE slot layout: high word first; a 32-bit value lives in the low word
                return '{ uint32_t *p = (uint32_t *)%s; h%d = p[0]; %s = p[1]; }' % (addr, i.rt, lhs(i.rt))
            return '{ uint32_t *p = (uint32_t *)%s; p[0] = %s; p[1] = %s; }' % (addr, self.hi_of(i.rt), R(i.rt))
        if kind == 'ld':
            det = ''
            if ctype in ('int16_t', 'uint16_t', 'int8_t', 'uint8_t') and not (i.rs == 29 and addr_override is None):
                sid = len(DET_SITES)
                DET_SITES.append((self.info.o.name, self.name, i.addr, i.imm, ctype))
                det = 'LIFT_DET%d(%d, %s); ' % (16 if '16' in ctype else 8, sid, addr)
            if ctype.startswith('int'):
                return det + '%s = (uint32_t)(int32_t)*(%s *)%s;' % (lhs(i.rt), ctype, addr)
            return det + '%s = *(%s *)%s;' % (lhs(i.rt), ctype, addr)
        return '*(%s *)%s = (%s)%s;' % (ctype, addr, ctype, R(i.rt))

    def bad(self, i):
        raise Unsupported('64-bit access of an incoming slot at %08x' % i.addr)

    def reloc_stmt(self, i, rl, types):
        op = i.op
        r = [x for x in rl if x[0] not in (R_JALR,)]
        t = set(types)
        # the PIC prologue computes gp from the function's own address: gp is the constant 0 here
        if t in ({R_GPREL16, R_SUB, R_HI}, {R_GPREL16, R_SUB, R_LO}) and \
                self.is_text_sym(self.o.syms[r[0][1]]):
            # the register receives (gp - function address); added to t9 (= 0 here) it must give gp = 0
            return '%s = 0;' % lhs(i.rt) if i.op == 0x0f else None
        if t == {R_CALL16} or t == {R_GOT_DISP}:
            ex, sym, _ = self.reloc_addr(r[0], 0)
            if op != 0x23:
                raise Unsupported('unexpected GOT load form at %08x' % i.addr)
            s = '%s = %s;' % (lhs(i.rt), ex)
            self.pending_tag = (i.rt, sym, _, ex)
            return s
        if t == {R_GOT_PAGE}:
            ex, sym, _ = self.reloc_addr(r[0], 0)
            return '%s = ((%s) + 0x8000u) & ~0xffffu;' % (lhs(i.rt), ex)
        if t == {R_GOT_OFST}:
            ex, sym, A = self.reloc_addr(r[0], 0)
            off = '(int32_t)((%s) - ((((%s) + 0x8000u)) & ~0xffffu))' % (ex, ex)
            if op in (0x08, 0x09):
                self.pending_tag = (i.rt, sym, A, ex)
                return '%s = %s + %s;' % (lhs(i.rt), R(i.rs), off)
            if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b):
                return self.table_or_mem(i, '%s + %s' % (R(i.rs), off), sym, A)
            raise Unsupported('GOT_OFST form at %08x' % i.addr)
        if t == {R_GPREL16}:
            ex, sym, _ = self.reloc_addr(r[0], i.immu)
            base = '' if i.rs == 28 else ' + %s' % R(i.rs)      # rs is gp or a copy of it (gp = 0)
            if op in (0x08, 0x09):
                return '%s = %s%s;' % (lhs(i.rt), ex, base)
            if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b):
                return self.mem(i, ex + base)
            raise Unsupported('GPREL16 form at %08x' % i.addr)
        if t == {R_GPREL16, R_SUB, R_HI}:
            a = r[0]
            ex, sym, _ = self.reloc_addr((a[0], a[1], a[2], True), 0)
            if op != 0x0f:
                raise Unsupported('neg-gprel hi form at %08x' % i.addr)
            return '%s = (uint32_t)(0 - %s);' % (lhs(i.rt), ex)
        if t == {R_GPREL16, R_SUB, R_LO}:
            if op != 0x09:
                raise Unsupported('neg-gprel lo form at %08x' % i.addr)
            return '%s = %s;' % (lhs(i.rt), R(i.rs))
        raise Unsupported('relocation %s on op 0x%x at %08x' % (sorted(t), op, i.addr))

    def is_text_sym(self, sym):
        sh = sym['shndx']
        return 0 < sh < len(self.o.sec) and self.o.sec[sh]['name'] == '.text'


    def note_table(self, reg, sym, ex):
        pass

    def table_or_mem(self, i, addrexpr, sym, A):
        """A load/store through GOT_OFST.  Loads from a jump table in .rodata become label-array reads."""
        if i.op == 0x23 and sym['type'] == L.STT_SECTION:
            ent = self.tables.get((sym['shndx'], A))
            if ent is not None:
                return self.jt_load(i, addrexpr, sym, A, ent)
        return self.mem(i, addrexpr)

    def jt_load(self, i, addrexpr, sym, A, ent):
        # ent: list of (text shndx, offset) targets
        n = len(self.jt_arrays)
        labels = []
        for tshndx, toff in ent:
            if tshndx != self.shndx or not (self.start <= toff < self.end):
                break          # the run continues into the next table (another function)
            labels.append('&&L_%08x' % toff)
            self.jt_labels.add(toff)
        self.jt_arrays.append('static const void *const jt%d[] = { %s };' % (n, ', '.join(labels)))
        nm = self.info.symname(sym)
        return '%s = (uint32_t)(uintptr_t)jt%d[(((uint32_t)(%s) - ((%s) + %d)) >> 2)]; jr_table = 1;' % (
            lhs(i.rt), n, addrexpr, self.syms.addr(nm), A)

    # -- the whole function ----------------------------------------------------------------
    # -- jump targets and tail calls (a direct jump or branch out of the function is a call that never returns here) --
    def jump_target(self, i):
        return ((i.addr + 4) & 0xf0000000) | (i.target << 2)

    def is_tail_target(self, t):
        return False

    def tail_stmt(self, t):
        raise Unsupported('branch leaves the function at %08x' % t)

    def goto_stmt(self, t):
        if self.start <= t < self.end:
            return 'goto L_%08x;' % t
        return self.tail_stmt(t)

    def pass_args(self, defs, used):
        # a tail call passes the argument registers straight through: every one not written yet is a parameter
        for r_ in ARGREGS:
            if r_ not in defs:
                used.add(r_)

    def fp_pre(self):
        return ' '.join('lift_fpa[%d] = f[%d];' % (k, 12 + k) for k in range(8)) + ' ' if self.has_fp else ''

    def fp_post(self):
        return ' f[0] = lift_fpr;' if self.has_fp else ''

    def ret_stmt(self):
        return '    { lift_fpr = f[0]; return r2; }' if self.has_fp else '    return r2;'

    def lift(self):
        insns, byaddr, start, end = self.insns, self.byaddr, self.start, self.end
        self.has_fp = any(i.op in (0x11, 0x31, 0x35, 0x39, 0x3d) for i in insns)
        frame = 0
        for i in insns:      # the first allocation, wherever it is (shrink-wrapped prologues come after a branch)
            if i.op == 0x09 and i.rs == 29 and i.rt == 29 and i.imm < 0:
                frame = -i.imm
                break
        self.reg_frame = None
        if not frame:
            # frames over 32K: ori at,zero,N ; subu sp,sp,at
            for k, i in enumerate(insns[1:], 1):
                j = insns[k - 1]
                if i.op == 0 and i.fn in (0x23, 0x2f) and i.rd == 29 and i.rs == 29 and \
                        j.op == 0x0d and j.rs == 0 and j.rt == i.rt:
                    frame = j.immu
                    self.reg_frame = i.rt
                    break
        self.frame = frame
        delay_of, targets = {}, set()
        for k, i in enumerate(insns):
            if is_control(i):
                if k + 1 >= len(insns):
                    raise Unsupported('control transfer without delay slot at %08x' % i.addr)
                delay_of[i.addr] = insns[k + 1]
                if is_branch(i):
                    targets.add(branch_target(i))
                elif i.op == 0x02:
                    targets.add(self.jump_target(i))
                elif i.op == 0x03 and not getattr(self, 'allow_jal', False):
                    raise Unsupported('jal at %08x' % i.addr)
        for t in targets:
            if not (start <= t < end) and not self.is_tail_target(t):
                raise Unsupported('branch leaves the function at %08x' % t)
        # arity: liveness over the same walk the leaf lifter used, using the reloc-aware def/use
        used_args, stack_slots = self.liveness(delay_of, frame)
        # every incoming slot the body mentions needs a declaration, also on paths the liveness walk did not take
        stack_slots = set(stack_slots)
        for i in insns:
            if i.rs == 29 and i.imm >= frame and i.op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b):
                stack_slots.add((i.imm - frame) // 8)
        nstack = (max(stack_slots) + 1) if stack_slots else 0
        nargs = 8 if nstack else ((max(used_args) - 3) if used_args else 0)
        self.nargs, self.nstack = nargs, nstack
        # outgoing stack words for calls
        out_slots = 0
        for i in insns:
            if i.rs == 29 and i.op in (0x2b, 0x3f) and i.imm < 64 and i.imm < frame:
                k = i.imm // 8
                if (i.op == 0x2b and i.imm % 8 == 4) or (i.op == 0x3f and i.imm % 8 == 0):
                    out_slots = max(out_slots, k + 1)
        self.out_slots = out_slots
        params = []          # no declared parameters: arguments are read from the caller's stack, never written
        self.widened = self.compute_widened()
        out = ['__attribute__((noinline)) uint32_t %s()' % self.name, '{']
        regs = [n for n in range(1, 32) if n != 29]
        out.append('    uint32_t ' + ', '.join('r%d = 0' % n for n in regs) + ', lo = 0, hi = 0, jr_table = 0;')
        out.append('    (void)lo; (void)hi; (void)jr_table;')
        if self.widened:
            out.append('    uint32_t ' + ', '.join('h%d = 0' % n for n in sorted(self.widened)) + ';')
        if self.has_fp:
            out.append('    uint64_t f[32] = {0}; int fcc = 0; (void)fcc;')
            # floating-point arguments arrive in a side channel (the caller's f12..f19)
            out.append('    ' + ' '.join('f[%d] = lift_fpa[%d];' % (12 + k, k) for k in range(8)))
        if nargs or nstack:
            out.append('    volatile uint32_t *A = (volatile uint32_t *)((char *)__builtin_frame_address(0) + 8);')
        for k in range(nargs):
            out.append('    r%d = A[%d];' % (4 + k, k))
        for k in range(nstack):
            out.append('    uint32_t s%d = A[%d];' % (k, 8 + k))
        if frame:
            out.append('    uint8_t frame[%d] __attribute__((aligned(16)));' % frame)
        body = []
        k = 0
        while k < len(insns):
            i = insns[k]
            if i.addr in targets or i.addr in self.jt_labels_precomputed():
                body.append('L_%08x:;' % i.addr)
            body.append('    /* %08x: %08x */' % (i.addr, i.w))
            if is_control(i):
                d = delay_of[i.addr]
                self.control(i, d, body, targets, insns, k)
                k += 1
                dd = insns[k]
                if dd.addr in targets or dd.addr in self.jt_labels_precomputed():
                    body.append('L_%08x:;' % dd.addr)
                    s = self.safe_stmt(dd)
                    if s:
                        body.append('    ' + s)
                k += 1
                continue
            s = self.safe_stmt(i)
            if s:
                body.append('    ' + s)
            k += 1
        body.append(self.ret_stmt())
        out.extend('    ' + a if not a.startswith('static') else '    ' + a for a in self.jt_arrays)
        out.extend(body)
        out.append('}')
        text = '\n'.join(out)
        # labels needed by jump tables but placed before they were known
        for t in sorted(self.jt_labels):
            if ('L_%08x:;' % t) not in text:
                text = text.replace('    /* %08x: ' % t, 'L_%08x:;\n    /* %08x: ' % (t, t), 1)
        for t in sorted(targets):
            if ('L_%08x:;' % t) not in text:
                text = text.replace('    /* %08x: ' % t, 'L_%08x:;\n    /* %08x: ' % (t, t), 1)
        return text

    def jt_labels_precomputed(self):
        if not hasattr(self, '_jtl'):
            s = set()
            for ent in self.tables.values():
                for tshndx, toff in ent:
                    if tshndx == self.shndx and self.start <= toff < self.end:
                        s.add(toff)
            self._jtl = s
        return self._jtl

    def safe_stmt(self, i):
        if is_control(i):
            raise Unsupported('control transfer in a delay slot at %08x' % i.addr)
        self.pending_tag = None
        st = self.stmt(i)
        self.update_tags(i)
        if st and self.widened and i.op not in (0x37, 0x1a):
            d = self.dest(i)
            if d in self.widened:
                mv = self.move_src(i)
                st += ' h%d = %s;' % (d, ('h%d' % mv) if mv in self.widened else '(uint32_t)((int32_t)%s >> 31)' % lhs(d)) \
                    if mv in self.widened or mv is None or True else ''
        return st

    def update_tags(self, i):
        d = self.dest(i)
        pt, self.pending_tag = self.pending_tag, None
        if pt is not None:
            reg, sym, A, ex = pt
            if sym['type'] == L.STT_SECTION and (sym['shndx'], A) in self.tables:
                self.tag[reg] = (sym, A, ex)
                return
        if d is None:
            return
        if i.op == 0 and i.fn in (0x21, 0x2d) and i.w:
            a, b = i.rs, i.rt
            if a in self.tag and b not in self.tag:
                self.tag[d] = self.tag[a]
                return
            if b in self.tag and a not in self.tag:
                self.tag[d] = self.tag[b]
                return
        if i.op == 0 and i.fn == 0x00 and i.rt in self.tag:      # sll of a table base keeps nothing useful
            self.tag.pop(d, None)
            return
        self.tag.pop(d, None)

    def liveness(self, delay_of, frame):
        insns, byaddr, end = self.insns, self.byaddr, self.end
        used, slots = set(), set()
        defined_at = {self.start: frozenset([0, 28, 29, 31])}
        work = [self.start]
        seen = {}
        while work:
            a = work.pop()
            defs = set(defined_at[a])
            while True:
                if a in seen and seen[a] <= defs and a != self.start:
                    break
                seen[a] = frozenset(defs)
                i = byaddr[a]
                if is_control(i):
                    d = delay_of[a]
                    r1, w1 = (set(), set()) if is_control(d) else self.rw(d)
                    cr = set()
                    if is_branch(i):
                        cr = {i.rs, i.rt} if branch_kind(i) in ('beq', 'bne') else (set() if i.op == 0x11 else {i.rs})
                    elif i.op == 0 and i.fn in (0x08, 0x09):
                        cr = {i.rs}
                    callish = (i.op == 0 and i.fn == 0x09) or i.op == 0x03      # jalr, or jal (executables)
                    if callish or (i.op == 0 and i.fn == 0x08 and i.rs != 31):
                        # a call or tail call may pass argument registers straight through: every one not yet
                        # written on this path is then an incoming parameter (extra cdecl params are harmless)
                        for r_ in ARGREGS:
                            if r_ not in defs and r_ not in (r1 | w1):
                                used.add(r_)
                    for r in cr | r1:
                        if r in ARGREGS and r not in defs:
                            used.add(r)
                    self.stack_slots_of(d, frame, slots)
                    defs |= w1
                    if callish:
                        defs |= {2, 3} | set(range(4, 16)) | {24, 25, 31}
                        nxt = a + 8
                        if nxt >= self.end:
                            break
                        defined_at[nxt] = (defined_at[nxt] & frozenset(defs)) if nxt in defined_at else frozenset(defs)
                        a = nxt
                        defs = set(defined_at[a])
                        continue
                    if is_branch(i):
                        t = branch_target(i)
                        if not (self.start <= t < self.end):
                            self.pass_args(defs, used)
                        else:
                            prev = defined_at.get(t)
                            nd = frozenset(defs) if prev is None else prev & frozenset(defs)
                            if prev is None or nd != prev:
                                defined_at[t] = nd
                                work.append(t)
                        nxt = a + 8
                        if nxt < end:
                            defined_at[nxt] = (defined_at[nxt] & frozenset(defs)) if nxt in defined_at else frozenset(defs)
                            a = nxt
                            defs = set(defined_at[a])
                            continue
                        break
                    if i.op == 0x02:
                        t = self.jump_target(i)
                        if not (self.start <= t < self.end):
                            self.pass_args(defs, used)
                            break
                        prev = defined_at.get(t)
                        nd = frozenset(defs) if prev is None else prev & frozenset(defs)
                        if prev is None or nd != prev:
                            defined_at[t] = nd
                            work.append(t)
                        break
                    # jr: return, tail call, or table jump -- table targets are explored as roots below
                    for toff in self.jt_labels_precomputed():
                        if toff not in defined_at:
                            defined_at[toff] = frozenset(defs)
                            work.append(toff)
                    break
                r, w = self.rw(i)
                self.stack_slots_of(i, frame, slots)
                for x in r:
                    if x in ARGREGS and x not in defs:
                        used.add(x)
                defs |= w
                a += 4
                if a >= end:
                    break
                if a in defined_at:
                    defined_at[a] = defined_at[a] & frozenset(defs)
                    defs = set(defined_at[a])
                else:
                    defined_at[a] = frozenset(defs)
        return used, slots

    def rw(self, i):
        if i.w == 0:
            return set(), set()
        rl = self.rel.get(i.addr, [])
        r, w = reads_writes_ext(i)
        if any(x[0] in (R_CALL16, R_GOT_DISP, R_GOT_PAGE, R_GPREL16) for x in rl) and i.rs in (28, 1):
            r = r - {i.rs} if i.rs == 28 else r
        return r, w

    def stack_slots_of(self, i, frame, slots):
        if i.op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b, 0x37, 0x3f) and i.rs == 29 and i.imm >= frame:
            slots.add((i.imm - frame) // 8)

    def call_args(self):
        parts = ['r%d' % n for n in range(4, 12)]
        for k in range(self.out_slots):
            parts.append('*(uint32_t *)(frame + %d)' % (8 * k + 4))
        return ', '.join(parts)

    def tail_args(self):
        parts = ['r%d' % n for n in range(4, 12)] + ['s%d' % k for k in range(self.nstack)]
        return ', '.join(parts)

    def control(self, i, d, body, targets, insns, k):
        def delay_stmts(indent):
            s = self.safe_stmt(d)
            if s:
                body.append(indent + s)
        if is_branch(i):
            likely = is_likely(i)
            body.append('    { int c = %s;' % cond(i))
            if likely:
                body.append('      if (c) {')
                s = self.safe_stmt(d)
                if s:
                    body.append('        ' + s)
                body.append('        %s }' % self.goto_stmt(branch_target(i)))
            else:
                delay_stmts('      ')
                body.append('      if (c) %s' % self.goto_stmt(branch_target(i)))
            body.append('    }')
            if likely or True:
                nxt = insns[k + 2] if k + 2 < len(insns) else None
                if nxt is not None and (likely or d.addr in targets):
                    targets.add(nxt.addr)
                    body.append('    goto L_%08x;' % nxt.addr)
            return
        if i.op == 0x02:
            delay_stmts('    ')
            body.append('    ' + self.goto_stmt(self.jump_target(i)))
            return
        if i.op == 0 and i.fn == 0x09:      # jalr
            tgt = 'tgt_%08x' % i.addr
            body.append('    { uint32_t %s = %s;' % (tgt, R(i.rs)))
            s = self.safe_stmt(d)
            if s:
                body.append('      ' + s)
            body.append('      %sr2 = ((uint32_t (*)())(uintptr_t)%s)(%s);%s }' % (self.fp_pre(), tgt, self.call_args(), self.fp_post()))
            if i.rd and i.rd != 31:
                raise Unsupported('jalr with a link register other than ra at %08x' % i.addr)
            return
        if i.op == 0 and i.fn == 0x08:
            if i.rs == 31:
                s = self.safe_stmt(d)
                if s:
                    body.append('    ' + s)
                body.append(self.ret_stmt())
                return
            # a table jump (target loaded from a jump table) or a tail call through a GOT-loaded address
            s = self.safe_stmt(d)
            if s:
                body.append('    ' + s)
            body.append('    if (jr_table) { jr_table = 0; goto *(void *)(uintptr_t)%s; }' % R(i.rs))
            body.append('    return ((uint32_t (*)())(uintptr_t)%s)(%s);' % (R(i.rs), self.tail_args()))
            return
        raise Unsupported('control at %08x' % i.addr)


def cond(i):
    k = branch_kind(i)
    if k in ('bc1t', 'bc1f'):
        return 'fcc' if k == 'bc1t' else '!fcc'
    return {'beq': '%s == %s' % (R(i.rs), R(i.rt)), 'bne': '%s != %s' % (R(i.rs), R(i.rt)),
            'blez': '(int32_t)%s <= 0' % R(i.rs), 'bgtz': '(int32_t)%s > 0' % R(i.rs),
            'bltz': '(int32_t)%s < 0' % R(i.rs), 'bgez': '(int32_t)%s >= 0' % R(i.rs),
            'bc1t': 'fcc', 'bc1f': '!fcc'}[k]


def reads_writes_ext(i):
    """reads_writes with the 64-bit/extra ops the lifted code understands."""
    op = i.op
    if op == 0x11:
        fmt = i.rs
        if fmt in (0, 1):
            return set(), {i.rt}
        if fmt in (4, 5):
            return {i.rt}, set()
        return set(), set()
    if op in (0x31, 0x35):
        return {i.rs}, set()
    if op in (0x39, 0x3d):
        return {i.rs}, set()
    if op in (0x1a, 0x1b):
        return {i.rs}, {i.rt}
    if op in (0x2c, 0x2d):
        return {i.rs, i.rt}, set()
    if op == 0 and i.fn in (0x3c, 0x3e, 0x3f):
        return {i.rt}, {i.rd}
    if op in (0x37,):
        return {i.rs}, {i.rt}
    if op == 0x3f:
        return {i.rs, i.rt}, set()
    return reads_writes(i)


# ---------------------------------------------------------------------------------------------
def jump_tables(info):
    """(section no, offset) of a table start -> list of (text shndx, offset) targets, from R_MIPS_32 runs."""
    tables = {}
    o = info.o
    for sno, rel in info.relocs.items():
        sec = o.sec[sno]
        if sec['flags'] & 4 or sec['name'].startswith(SKIP_SEC):
            continue
        offs = sorted(off for off, rl in rel.items() if rl and rl[0][0] == R_32)
        runs, cur = [], []
        for off in offs:
            sym = o.syms[rel[off][0][1]]
            tgt = o.sec[sym['shndx']] if sym['shndx'] < len(o.sec) and sym['shndx'] else None
            is_text = tgt is not None and tgt['name'] == '.text' and sym['type'] == L.STT_SECTION
            if is_text and cur and off == cur[-1][0] + 4:
                cur.append((off, sym, rel[off][0]))
            else:
                if cur:
                    runs.append(cur)
                cur = [(off, sym, rel[off][0])] if is_text else []
        if cur:
            runs.append(cur)
        for run in runs:
            for k, (off, sym, r) in enumerate(run):
                ent = []
                for off2, sym2, r2 in run[k:]:
                    base = r2[2] if r2[3] else struct.unpack_from('>I', o.data, o.sec[sno]['off'] + off2)[0]
                    ent.append((sym2['shndx'], base))
                tables[(sno, off)] = ent
    return tables


def emit_data(info, w):
    o = info.o
    w.write('\t.text\n')
    secs = []
    for s in o.sec:
        if s['type'] not in (L.SHT_PROGBITS, L.SHT_NOBITS) or not (s['flags'] & 2) or (s['flags'] & 4) or not s['size']:
            continue
        if s['name'].startswith(SKIP_SEC):
            continue
        secs.append(s)
    for s in secs:
        lab = '%s__sec%d' % (info.tag, s['no'])
        writable = bool(s['flags'] & 1)
        if s['type'] == L.SHT_NOBITS:
            w.write('\t.bss\n')
        else:
            w.write('\t.data\n' if writable else '\t.section .rodata,"a"\n')
        w.write('\t.balign %d\n\t.globl %s\n\t.hidden %s\n%s:\n' % (max(s['align'], 4), lab, lab, lab))
        img = o.image.get(s['no']) or bytearray(s['size'])
        rel = info.relocs.get(s['no'], {})
        # symbols inside this section
        for sym in o.syms:
            if sym['shndx'] == s['no'] and sym['type'] != L.STT_SECTION and sym['name']:
                pass
        if s['type'] == L.SHT_NOBITS:
            w.write('\t.zero %d\n' % s['size'])
        else:
            off = 0
            while off < s['size']:
                if off in rel and rel[off][0][0] == R_32 and off + 4 <= s['size']:
                    typ, sn, a, is_rela = rel[off][0]
                    sym = o.syms[sn]
                    addend = a if is_rela else struct.unpack_from('>I', o.data, s['off'] + off)[0]
                    w.write('\t.long %s\n' % reloc_target(info, sym, addend))
                    off += 4
                    continue
                n = 0
                chunk = []
                while off + n < s['size'] and (off + n) not in rel and n < 16:
                    chunk.append(img[off + n])
                    n += 1
                w.write('\t.byte %s\n' % ','.join(str(b) for b in chunk))
                off += n
    # symbol definitions
    for sym in o.syms:
        if not sym['name'] or sym['shndx'] in (0,) or sym['type'] == L.STT_SECTION:
            continue
        if sym['shndx'] == L.SHN_COMMON:
            w.write('\t.comm %s,%d,%d\n' % (info.symname(sym), sym['size'], max(sym['value'], 4)))
            continue
        if sym['shndx'] >= len(o.sec):
            continue
        sec = o.sec[sym['shndx']]
        if sec['flags'] & 4 or sec['name'].startswith(SKIP_SEC) or not sec['size'] or sec['name'] == '.text':
            continue
        nm = info.symname(sym)
        if sym['bind'] in (L.STB_GLOBAL, L.STB_WEAK):
            w.write('\t.%s %s\n' % ('weak' if sym['bind'] == L.STB_WEAK else 'globl', nm))
        w.write('\t.set %s, %s__sec%d + %d\n' % (nm, info.tag, sym['shndx'], sym['value']))
        w.write('\t.type %s,@object\n' % nm)
        if sym['size']:
            w.write('\t.size %s,%d\n' % (nm, sym['size']))


def reloc_target(info, sym, addend):
    o = info.o
    if sym['type'] == L.STT_SECTION and sym['shndx'] < len(o.sec) and o.sec[sym['shndx']]['name'] == '.text':
        f = info.func_at(sym['shndx'], addend)
        if f is None:
            return '0 /* text offset %d without a function symbol */' % addend
        return info.symname(f)
    if sym['type'] == L.STT_SECTION:
        return '%s + %d' % (info.symname(sym), addend)
    return '%s + %d' % (info.symname(sym), addend)


def main():
    import nm_swap
    args = sys.argv[1:]
    archive, so, out = args[0], args[1], Path(args[2])
    narrow = {}
    if '--narrow' in args:
        for e in json.load(open(args[args.index('--narrow') + 1]))['sites']:
            narrow[(e['member'], int(e['offset'], 0))] = (e['from'], e['to'])
    prof = None
    if '--profile' in args:
        prof = json.load(open(args[args.index('--profile') + 1]))['byte_arrays']
    out.mkdir(parents=True, exist_ok=True)
    (out / 'lift_rt.h').write_text((here / 'lift_rt.h').read_text())
    st = L.StackLink()
    st.add_archive(archive)
    st.layout()                                  # allocates images; relocation is NOT applied
    nm_swap.apply(st, so, byte_arrays=prof or ())
    infos = [ObjInfo(o, k) for k, o in enumerate(st.objs)]
    for info in infos:
        info.narrow = {off: ft for (m, off), ft in narrow.items() if m == info.o.name}
    report = Counter()
    failed = []
    cfiles, sfiles = [], []
    for info in infos:
        syms = Syms()
        tables = jump_tables(info)
        funcs_c = []
        for shndx, lst in info.funcs.items():
            for v, z, s in sorted(lst, key=lambda x: x[0]):
                nm = info.symname(s)
                try:
                    fl = FuncLifter(info, shndx, v, z, nm, syms, tables)
                    text = fl.lift()
                    funcs_c.append((text, s))
                    report['lifted'] += 1
                except Unsupported as e:
                    report['unsupported'] += 1
                    failed.append((info.o.name, s['name'], str(e)))
                    funcs_c.append(('uint32_t %s() { extern void abort(void); abort(); return 0; } /* %s */' % (nm, str(e).replace('*/', '')), s))
                except Exception as e:     # noqa
                    report['error'] += 1
                    failed.append((info.o.name, s['name'], 'ERROR %r' % e))
                    funcs_c.append(('uint32_t %s() { extern void abort(void); abort(); return 0; }' % nm, s))
        cpath = out / (info.tag + '.c')
        with open(cpath, 'w') as w:
            w.write('#include "lift_rt.h"\n')
            w.write('uint64_t lift_fpa[8] __attribute__((weak)); uint64_t lift_fpr __attribute__((weak));\n')
            w.write(syms.decls())
            for text, s in funcs_c:
                if s['bind'] == L.STB_LOCAL:
                    pass
                w.write(text + '\n\n')
        cfiles.append(cpath)
        spath = out / (info.tag + '.s')
        with open(spath, 'w') as w:
            emit_data(info, w)
            # function symbols: global ones are defined by the C file already
        sfiles.append(spath)
    (out / 'det_sites.json').write_text(json.dumps(DET_SITES))
    (out / 'report.json').write_text(json.dumps({'counts': report, 'failed': failed}, indent=1))
    print('lifted=%d unsupported=%d error=%d' % (report['lifted'], report['unsupported'], report['error']))
    reasons = Counter(re.sub(r'at [0-9a-f]{8}', '', f[2]) for f in failed)
    for r, n in reasons.most_common(15):
        print('  %5d  %s' % (n, r))


if __name__ == '__main__':
    main()
