#!/usr/bin/env python3
"""Lift a linked IRIX N64 shared library to native x86_64 C + assembler.

    lift_n64.py LIB.so OUTDIR [--name NAME]

N64 is LP64 like x86_64, so the lifted code uses the machine's own pointers and struct layouts:
  * every register is a uint64_t local; 32-bit instructions sign-extend their result as on MIPS;
  * a function is `uint64_t f(uint64_t a0 .. a7, ...)`: the eight argument registers in order, then the
    caller's stack arguments, which is where the x86_64 C ABI puts integer and pointer arguments too, so
    native code can call lifted functions and the reverse without stubs;
  * the frame is a local byte array, memory is native little-endian, addresses are real pointers;
  * $gp is a link-time constant: GOT loads become symbol or image addresses, small data is addressed directly;
  * data sections are emitted as one image blob; every relocated word is a `.quad` expression;
  * code addresses that are not function starts (jump tables) stay as their original numbers and `jr` becomes a
    switch over the targets inside the function.
Functions that cannot be lifted become stubs that abort(); the report counts them by reason.
"""
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
sys.path.insert(0, str(here.parents[2] / 'native-libs' / 'nativemem' / 'tools'))
from elf_n64 import Elf, R_MIPS_REL32                    # noqa: E402
import nm_swap                                           # noqa: E402

NARGS = 8
DYN_RESERVE = 1 << 18        # room below the frame for a function that moves its stack pointer down
VA_SLOTS = 24                # stack arguments copied behind the frame of a function that homes its registers
PROTO = 'uint64_t %s(' + ', '.join('uint64_t a%d' % k for k in range(NARGS)) + ', ...)'
CALLT = '((uint64_t (*)(uint64_t, ...))(uintptr_t)(%s))'


class Unsupported(Exception):
    pass


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


class Insn:
    __slots__ = ('addr', 'w', 'op', 'rs', 'rt', 'rd', 'sa', 'fn', 'imm', 'immu')

    def __init__(self, addr, w):
        self.addr, self.w = addr, w
        self.op = w >> 26
        self.rs = (w >> 21) & 31
        self.rt = (w >> 16) & 31
        self.rd = (w >> 11) & 31
        self.sa = (w >> 6) & 31
        self.fn = w & 63
        self.immu = w & 0xffff
        self.imm = sx16(self.immu)


BR = {0x04: 'eq', 0x05: 'ne', 0x06: 'lez', 0x07: 'gtz', 0x14: 'eq', 0x15: 'ne', 0x16: 'lez', 0x17: 'gtz'}


def is_branch(i):
    return i.op in BR or (i.op == 1 and i.rt in (0, 1, 2, 3)) or (i.op == 0x11 and i.rs == 8)


def is_bal(i):
    return i.op == 1 and i.rt in (0x10, 0x11, 0x12, 0x13)


def is_control(i):
    return is_branch(i) or is_bal(i) or i.op in (2, 3) or (i.op == 0 and i.fn in (8, 9))


def is_likely(i):
    return i.op in (0x14, 0x15, 0x16, 0x17) or (i.op == 1 and i.rt in (2, 3, 0x12, 0x13)) or \
        (i.op == 0x11 and i.rs == 8 and bool(i.rt & 2))


def btarget(i):
    return i.addr + 4 + (i.imm << 2)


def S32(x):
    return '(uint64_t)(int64_t)(int32_t)(%s)' % x


class Image:
    def __init__(self, path, name):
        self.elf = e = Elf(path)
        self.name = name
        self.text = [s for s in e.sec if s['name'] in ('.text', '.init', '.fini') and s['size']]
        self.tlo = min(s['addr'] for s in self.text)
        self.thi = max(s['addr'] + s['size'] for s in self.text)
        data = [s for s in e.allocated() if s['addr'] >= self.thi]
        self.data_lo = min(s['addr'] for s in data)
        self.hi = max(s['addr'] + s['size'] for s in data)
        self.datasecs = data
        self.got_lo = e.pltgot
        self.got_hi = e.pltgot + 8 * (e.local_gotno + e.symtabno - e.gotsym)
        self.width = defaultdict(set)         # link-time data address -> access widths seen in the code
        self.rel = {}                         # offset -> symbol index, for the 64-bit pointer words
        for off, sym, types in e.rel_dyn():
            if types[0] == R_MIPS_REL32:
                self.rel[off] = sym
        starts = {a for a, _ in e.frame_functions() if self.in_text(a)}
        for s in e.dynsyms:
            if s['type'] == 2 and s['shndx'] and self.in_text(s['value']):
                starts.add(s['value'])
        for tag in (12, 13):
            if e.dyn.get(tag):
                starts.add(e.dyn[tag])
        order = sorted(starts)
        self.funcs = []
        for k, a in enumerate(order):
            sec = self.text_sec(a)
            end = min(order[k + 1] if k + 1 < len(order) else self.thi, sec['addr'] + sec['size'])
            self.funcs.append((a, end - a))
        self.fstart = {a: 'f_%08x' % a for a, _ in self.funcs}
        # words in the data that point into the middle of a function: jump table targets
        self.inner = defaultdict(set)         # function start -> target addresses
        import bisect
        for off in self.rel:
            v = e.dword(off)
            if self.in_text(v) and v not in self.fstart:
                k = bisect.bisect_right(order, v) - 1
                if k >= 0:
                    self.inner[order[k]].add(v)
        self.syms = {}                        # external name -> C identifier

    def in_text(self, a):
        return any(s['addr'] <= a < s['addr'] + s['size'] for s in self.text)

    def text_sec(self, a):
        for s in self.text:
            if s['addr'] <= a < s['addr'] + s['size']:
                return s
        return None

    def sym(self, name):
        if name not in self.syms:
            self.syms[name] = 'S%d' % len(self.syms)
        return self.syms[name]

    def addr_c(self, a):
        """C expression (uint64_t) for a link-time address."""
        if a in self.fstart:
            return '(uint64_t)(uintptr_t)%s' % self.fstart[a]
        if self.data_lo - 0x10000 <= a < self.hi + 0x10000 and not self.in_text(a):
            return '(uint64_t)(uintptr_t)(IMG + %d)' % (a - self.data_lo)
        return '0x%xull' % a                  # text that is not a function start, or a plain number

    def addr_s(self, a):
        if a in self.fstart:
            return self.fstart[a]
        if self.data_lo <= a < self.hi:
            return '__img_%s + %d' % (self.name, a - self.data_lo)
        return '0x%x' % a


class Lifter:
    def __init__(self, img, addr, size):
        self.img, self.addr, self.end = img, addr, addr + size
        e = img.elf
        sec = img.text_sec(addr)
        raw = e.data[sec['off'] + addr - sec['addr']:sec['off'] + addr - sec['addr'] + size]
        self.insns = [Insn(addr + 4 * k, w) for k, w in enumerate(struct.unpack('>%dI' % (size // 4), raw))]
        self.by = {i.addr: i for i in self.insns}
        self.name = img.fstart[addr]
        self.dyn = False
        self.const = {}                        # reg -> (value, is_address)
        self.fp = any(i.op in (0x11, 0x31, 0x35, 0x39, 0x3d) or (i.op == 0 and i.fn == 1 and i.rd) for i in self.insns)

    # ---- helpers ---------------------------------------------------------------------------
    def R(self, n):
        if n == 0:
            return '0ull'
        if n == 28:
            return self.img.addr_c(self.img.elf.gp)
        if n == 29:
            return 'r29' if self.dyn else '(uint64_t)(uintptr_t)frame'
        return 'r%d' % n

    def setk(self, reg, val):
        if reg in (0, 28, 29):
            return
        if val is None:
            self.const.pop(reg, None)
        else:
            self.const[reg] = val

    def K(self, reg):
        if reg == 0:
            return (0, False)
        if reg == 28:
            return (self.img.elf.gp, True)
        return self.const.get(reg)

    def W(self, reg, expr, k=None):
        """Assignment to a register (writes to $zero, $gp and $sp are dropped)."""
        self.setk(reg, k)
        if reg in (0, 28):
            return None
        if reg == 29 and not self.dyn:
            raise Unsupported('write to sp')
        return 'r%d = %s;' % (reg, expr)

    def call(self, target):
        args = ['r%d' % n for n in range(4, 12)] + [self.outarg(k) for k in range(self.out_slots)]
        pre = ' '.join('lift_fpa[%d] = f[%d];' % (k, 12 + k) for k in range(8)) + ' ' if self.fp else ''
        post = ' f[0] = lift_fpr;' if self.fp else ''
        return '%sr2 = %s(%s);%s' % (pre, CALLT % target, ', '.join(args), post)

    def outarg(self, k):
        return '*(uint64_t *)(uintptr_t)(r29 + %d)' % (8 * k) if self.dyn else '*(uint64_t *)(frame + %d)' % (8 * k)

    def ret(self):
        return ('lift_fpr = f[0]; ' if self.fp else '') + 'return r2;'

    def tail(self, target):
        args = ['r%d' % n for n in range(4, 12)] + [self.outarg(k) for k in range(self.out_slots)]
        pre = ' '.join('lift_fpa[%d] = f[%d];' % (k, 12 + k) for k in range(8)) + ' ' if self.fp else ''
        return '{ %sreturn %s(%s); }' % (pre, CALLT % target, ', '.join(args))

    def goto(self, t):
        if self.addr <= t < self.end:
            self.labels.add(t)
            return 'goto L_%x;' % t
        if t in self.img.fstart:
            return self.tail(self.img.addr_c(t))
        raise Unsupported('branch leaves the function')

    # ---- memory ----------------------------------------------------------------------------
    LD = {0x20: ('int8_t', 1), 0x24: ('uint8_t', 1), 0x21: ('int16_t', 2), 0x25: ('uint16_t', 2),
          0x23: ('int32_t', 4), 0x27: ('uint32_t', 4), 0x37: ('uint64_t', 8)}
    ST = {0x28: ('uint8_t', 1), 0x29: ('uint16_t', 2), 0x2b: ('uint32_t', 4), 0x3f: ('uint64_t', 8)}

    def ea(self, i, width):
        """Address expression of a load or store; records the access width of link-time data addresses."""
        k = self.K(i.rs)
        if i.rs == 29:
            if self.dyn:
                return '(uintptr_t)(r29 + %d)' % i.imm
            if 0 <= i.imm < self.fsize:
                return '(frame + %d)' % i.imm
            return None                        # incoming argument slot
        if k and k[1]:
            a = k[0] + i.imm
            if self.img.data_lo <= a < self.img.hi:
                self.img.width[a].add(width)
                if i.rs == 28:
                    return '(uintptr_t)(IMG + %d)' % (a - self.img.data_lo)
        return '(uintptr_t)(%s + %d)' % (self.R(i.rs), i.imm)

    def incoming(self, i, ctype, width, store):
        off = i.imm - self.frame
        if off < 0:
            raise Unsupported('access below the stack pointer')
        slot, within = off // 8, off % 8
        self.nstack = max(self.nstack, slot + 1)
        if store:
            if width == 8 and within == 0:
                return 's%d = %s;' % (slot, self.R(i.rt))
            raise Unsupported('narrow store to an incoming slot')
        if within + width > 8:
            raise Unsupported('incoming slot access crosses a slot')
        sh = (8 - within - width) * 8          # the big-endian lane of a narrow argument is the end of the slot
        v = '(s%d >> %d)' % (slot, sh) if sh else 's%d' % slot
        return self.W(i.rt, '(uint64_t)(int64_t)(%s)%s' % (ctype, v) if ctype[0] == 'i' else '(uint64_t)(%s)%s' % (ctype, v))

    def mem(self, i):
        op = i.op
        if op in self.LD:
            ctype, width = self.LD[op]
            # GOT load: the slot address is a link-time constant
            k = self.K(i.rs)
            if op == 0x37 and k and k[1] and self.img.got_lo <= k[0] + i.imm < self.img.got_hi:
                return self.got(i, (k[0] + i.imm - self.img.got_lo) // 8)
            a = self.ea(i, width)
            if a is None:
                return self.incoming(i, ctype, width, False)
            v = '*(%s *)%s' % (ctype, a)
            return self.W(i.rt, '(uint64_t)(int64_t)%s' % v if ctype[0] == 'i' else '(uint64_t)%s' % v)
        ctype, width = self.ST[op]
        if i.rt == 28 and i.rs == 29:
            return None                        # saving $gp: it is a constant here
        a = self.ea(i, width)
        if a is None:
            return self.incoming(i, ctype, width, True)
        return '*(%s *)%s = (%s)%s;' % (ctype, a, ctype, self.R(i.rt))

    def got(self, i, idx):
        e = self.img.elf
        kind, v = e.got_entry(idx)
        if kind == 'local':
            return self.W(i.rt, self.img.addr_c(v), (v, True))
        if v['shndx'] and v['type'] == 2 and v['value'] in self.img.fstart and v['bind'] == 0:
            return self.W(i.rt, self.img.addr_c(v['value']), (v['value'], True))
        name = v['name']
        if not name:
            raise Unsupported('unnamed global GOT entry')
        k = (v['value'], True) if v['shndx'] and not self.img.in_text(v['value']) else None
        return self.W(i.rt, '(uint64_t)(uintptr_t)%s' % self.img.sym(name), k)

    # ---- one non-control instruction -------------------------------------------------------
    def stmt(self, i):
        op, rs, rt, rd, sa, fn = i.op, i.rs, i.rt, i.rd, i.sa, i.fn
        R, W = self.R, self.W
        if i.w == 0:
            return None
        if op == 0:
            if fn == 1:                        # movf / movt (0x00000001 is padding)
                if rd == 0:
                    return None
                return 'if (fcc[%d] == %d) %s' % ((i.w >> 18) & 7, (i.w >> 16) & 1, W(rd, R(rs)))
            a, b = R(rs), R(rt)
            ka, kb = self.K(rs), self.K(rt)
            if fn in (0x21, 0x2d, 0x25) and (rt == 0 or rs == 0):        # move
                src = rs if rt == 0 else rt
                return W(rd, R(src) if fn != 0x21 else S32(R(src)), self.K(src) if fn != 0x21 else None)
            if fn in (0x2d, 0x2f) and rd == 29 and rs == 29 and getattr(self, 'reg_frame', None) == rt:
                return None                    # frame set-up or tear-down through a register
            if fn == 0x2d and ka and kb and (ka[1] or kb[1]):            # daddu of two known values: an address
                v = (ka[0] + kb[0]) & 0xffffffffffffffff
                return W(rd, self.img.addr_c(v), (v, True))
            t = {
                0x00: S32('(uint32_t)%s << %d' % (b, sa)), 0x02: S32('(uint32_t)%s >> %d' % (b, sa)),
                0x03: S32('(int32_t)%s >> %d' % (b, sa)),
                0x04: S32('(uint32_t)%s << (%s & 31)' % (b, a)), 0x06: S32('(uint32_t)%s >> (%s & 31)' % (b, a)),
                0x07: S32('(int32_t)%s >> (%s & 31)' % (b, a)),
                0x14: '%s << (%s & 63)' % (b, a), 0x16: '%s >> (%s & 63)' % (b, a),
                0x17: '(uint64_t)((int64_t)%s >> (%s & 63))' % (b, a),
                0x20: S32('(uint32_t)%s + (uint32_t)%s' % (a, b)), 0x21: S32('(uint32_t)%s + (uint32_t)%s' % (a, b)),
                0x22: S32('(uint32_t)%s - (uint32_t)%s' % (a, b)), 0x23: S32('(uint32_t)%s - (uint32_t)%s' % (a, b)),
                0x24: '%s & %s' % (a, b), 0x25: '%s | %s' % (a, b), 0x26: '%s ^ %s' % (a, b), 0x27: '~(%s | %s)' % (a, b),
                0x2a: '(int64_t)%s < (int64_t)%s' % (a, b), 0x2b: '%s < %s' % (a, b),
                0x2c: '%s + %s' % (a, b), 0x2d: '%s + %s' % (a, b), 0x2e: '%s - %s' % (a, b), 0x2f: '%s - %s' % (a, b),
                0x38: '%s << %d' % (b, sa), 0x3a: '%s >> %d' % (b, sa), 0x3b: '(uint64_t)((int64_t)%s >> %d)' % (b, sa),
                0x3c: '%s << %d' % (b, sa + 32), 0x3e: '%s >> %d' % (b, sa + 32),
                0x3f: '(uint64_t)((int64_t)%s >> %d)' % (b, sa + 32),
                0x10: 'hi', 0x12: 'lo',
            }
            if fn in t:
                return W(rd, t[fn])
            if fn == 0x0a:
                self.setk(rd, None)
                return 'if (%s == 0) r%d = %s;' % (b, rd, a) if rd else None
            if fn == 0x0b:
                self.setk(rd, None)
                return 'if (%s != 0) r%d = %s;' % (b, rd, a) if rd else None
            if fn == 0x11:
                return 'hi = %s;' % a
            if fn == 0x13:
                return 'lo = %s;' % a
            if fn == 0x18:
                return '{ int64_t p_ = (int64_t)(int32_t)%s * (int64_t)(int32_t)%s; lo = %s; hi = %s; }' % (
                    a, b, S32('p_'), S32('p_ >> 32'))
            if fn == 0x19:
                return '{ uint64_t p_ = (uint64_t)(uint32_t)%s * (uint64_t)(uint32_t)%s; lo = %s; hi = %s; }' % (
                    a, b, S32('p_'), S32('p_ >> 32'))
            if fn == 0x1a:
                return ('{ int32_t n_ = (int32_t)%s, d_ = (int32_t)%s; if (d_ == -1) { lo = %s; hi = 0; } '
                        'else if (d_) { lo = %s; hi = %s; } }') % (a, b, S32('0u - (uint32_t)n_'), S32('n_ / d_'), S32('n_ % d_'))
            if fn == 0x1b:
                return '{ uint32_t n_ = (uint32_t)%s, d_ = (uint32_t)%s; if (d_) { lo = %s; hi = %s; } }' % (
                    a, b, S32('n_ / d_'), S32('n_ % d_'))
            if fn == 0x1c:
                return '{ __int128 p_ = (__int128)(int64_t)%s * (int64_t)%s; lo = (uint64_t)p_; hi = (uint64_t)(p_ >> 64); }' % (a, b)
            if fn == 0x1d:
                return ('{ unsigned __int128 p_ = (unsigned __int128)%s * %s; lo = (uint64_t)p_; '
                        'hi = (uint64_t)(p_ >> 64); }') % (a, b)
            if fn == 0x1e:
                return ('{ int64_t n_ = (int64_t)%s, d_ = (int64_t)%s; if (d_ == -1) { lo = 0ull - (uint64_t)n_; hi = 0; } '
                        'else if (d_) { lo = (uint64_t)(n_ / d_); hi = (uint64_t)(n_ %% d_); } }') % (a, b)
            if fn == 0x1f:
                return '{ uint64_t n_ = %s, d_ = %s; if (d_) { lo = n_ / d_; hi = n_ %% d_; } }' % (a, b)
            if fn in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36):          # traps on a condition (divide checks)
                return None
            if fn in (0x0c, 0x0d):
                return 'abort();'
            if fn == 0x0f:
                return None
            raise Unsupported('SPECIAL fn 0x%x' % fn)
        if op in (0x08, 0x09):
            k = self.K(rs)
            if rs == 29:
                return self.sp_addr(i)
            return W(rt, S32('(uint32_t)%s + %d' % (R(rs), i.imm)), ((k[0] + i.imm) & 0xffffffffffffffff, False) if k and not k[1] else None)
        if op in (0x18, 0x19):
            if rs == 29:
                return self.sp_addr(i)
            k = self.K(rs)
            if k:
                v = (k[0] + i.imm) & 0xffffffffffffffff
                if k[1]:
                    return W(rt, self.img.addr_c(v), (v, True))
                return W(rt, '%s + %d' % (R(rs), i.imm), (v, False))
            return W(rt, '%s + %d' % (R(rs), i.imm))
        if op == 0x0a:
            return W(rt, '(int64_t)%s < %d' % (R(rs), i.imm))
        if op == 0x0b:
            return W(rt, '%s < (uint64_t)(int64_t)%d' % (R(rs), i.imm))
        if op == 0x0c:
            return W(rt, '%s & 0x%xull' % (R(rs), i.immu))
        if op == 0x0d:
            k = self.K(rs)
            return W(rt, '%s | 0x%xull' % (R(rs), i.immu), (k[0] | i.immu, False) if k and not k[1] else None)
        if op == 0x0e:
            return W(rt, '%s ^ 0x%xull' % (R(rs), i.immu))
        if op == 0x0f:
            v = (i.immu << 16)
            if v & 0x80000000:
                v |= 0xffffffff00000000
            return W(rt, '0x%xull' % v, (v, False))
        if op in self.LD or op in self.ST:
            return self.mem(i)
        if op == 0x1a:                          # ldl: with the ldr that follows, an unaligned doubleword
            return W(rt, '({ uint64_t v_; __builtin_memcpy(&v_, (void *)(uintptr_t)(%s + %d), 8); v_; })' % (R(rs), i.imm))
        if op == 0x1b:
            return None
        if op == 0x2c:
            return '{ uint64_t v_ = %s; __builtin_memcpy((void *)(uintptr_t)(%s + %d), &v_, 8); }' % (R(rt), R(rs), i.imm)
        if op == 0x2d:
            return None
        if op in (0x11, 0x31, 0x35, 0x39, 0x3d):
            return self.fpu(i)
        if op in (0x2f, 0x33):
            return None
        raise Unsupported('op 0x%x' % op)

    def sp_addr(self, i):
        if i.rt == 29:
            if i.imm == -self.frame or (i.imm == self.frame and not self.dyn):
                return None                     # frame set-up and tear-down
            if self.dyn:
                return 'r29 = r29 + %d;' % i.imm
            raise Unsupported('write to sp')
        if self.dyn:
            return self.W(i.rt, 'r29 + %d' % i.imm)
        if 0 <= i.imm <= self.fsize:
            return self.W(i.rt, '(uint64_t)(uintptr_t)(frame + %d)' % i.imm)
        raise Unsupported('address of an incoming slot')

    def fpu(self, i):
        op = i.op
        if op in (0x31, 0x35, 0x39, 0x3d):
            width = 4 if op in (0x31, 0x39) else 8
            a = self.ea(Insn2(i), width)
            if a is None:
                raise Unsupported('fp access of an incoming slot')
            ft = i.rt
            if op == 0x31:
                return 'f[%d] = *(uint32_t *)%s;' % (ft, a)
            if op == 0x35:
                return 'f[%d] = *(uint64_t *)%s;' % (ft, a)
            if op == 0x39:
                return '*(uint32_t *)%s = (uint32_t)f[%d];' % (a, ft)
            return '*(uint64_t *)%s = f[%d];' % (a, ft)
        fmt, ft, fs, fd, fn = i.rs, i.rt, i.rd, i.sa, i.fn
        if fmt == 0:
            return self.W(i.rt, S32('(uint32_t)f[%d]' % fs))
        if fmt == 1:
            return self.W(i.rt, 'f[%d]' % fs)
        if fmt == 4:
            return 'f[%d] = (uint32_t)%s;' % (fs, self.R(i.rt))
        if fmt == 5:
            return 'f[%d] = %s;' % (fs, self.R(i.rt))
        if fmt in (2, 6):
            return self.W(i.rt, '0ull') if fmt == 2 else None       # fcsr: rounding mode stays the default
        if fmt in (0x10, 0x11):
            d = fmt == 0x11
            G = 'FD(f[%d])' if d else 'FS(f[%d])'
            P = ('f[%d] = DF(%%s);' if d else 'f[%d] = SF(0, %%s);') % fd
            a, b = G % fs, G % ft
            ar = {0: '%s + %s', 1: '%s - %s', 2: '%s * %s', 3: '%s / %s'}
            if fn in ar:
                return P % (ar[fn] % (a, b))
            one = {4: 'sqrt', 5: 'fabs'}
            if fn in one:
                return P % ('%s%s(%s)' % (one[fn], '' if d else 'f', a))
            if fn == 6:
                return 'f[%d] = f[%d];' % (fd, fs)
            if fn == 7:
                return P % ('-%s' % a)
            rl = {8: 'llround', 9: '(int64_t)', 10: '(int64_t)ceil', 11: '(int64_t)floor'}
            if fn in rl:
                return 'f[%d] = (uint64_t)(int64_t)%s(%s);' % (fd, rl[fn], a)
            rw = {12: '(int32_t)lround', 13: '(int32_t)', 14: '(int32_t)ceil', 15: '(int32_t)floor'}
            if fn in rw:
                return 'f[%d] = (uint32_t)%s(%s);' % (fd, rw[fn], a)
            if fn == 0x11:
                return 'if (fcc[%d] == %d) f[%d] = f[%d];' % ((i.w >> 18) & 7, (i.w >> 16) & 1, fd, fs)
            if fn == 0x12:
                return 'if (%s == 0) f[%d] = f[%d];' % (self.R(ft), fd, fs)
            if fn == 0x13:
                return 'if (%s != 0) f[%d] = f[%d];' % (self.R(ft), fd, fs)
            if fn == 0x20:
                return 'f[%d] = SF(0, (float)%s);' % (fd, a)
            if fn == 0x21:
                return 'f[%d] = DF((double)%s);' % (fd, a)
            if fn == 0x24:
                return 'f[%d] = (uint32_t)(int32_t)lrint(%s);' % (fd, a)
            if fn == 0x25:
                return 'f[%d] = (uint64_t)(int64_t)llrint(%s);' % (fd, a)
            if fn >= 0x30:
                return 'fcc[%d] = (%d && %s < %s) || (%d && %s == %s) || (%d && (%s != %s || %s != %s));' % (
                    (i.w >> 8) & 7, (fn >> 2) & 1, a, b, (fn >> 1) & 1, a, b, fn & 1, a, a, b, b)
            raise Unsupported('fp funct 0x%x' % fn)
        if fmt in (0x14, 0x15):
            src = '(int32_t)(uint32_t)f[%d]' % fs if fmt == 0x14 else '(int64_t)f[%d]' % fs
            if fn == 0x20:
                return 'f[%d] = SF(0, (float)%s);' % (fd, src)
            if fn == 0x21:
                return 'f[%d] = DF((double)%s);' % (fd, src)
        raise Unsupported('fp fmt 0x%x funct 0x%x' % (fmt, fn))

    # ---- control ---------------------------------------------------------------------------
    def cond(self, i):
        a, b = self.R(i.rs), self.R(i.rt)
        if i.op in BR:
            k = BR[i.op]
            return {'eq': '%s == %s' % (a, b), 'ne': '%s != %s' % (a, b),
                    'lez': '(int64_t)%s <= 0' % a, 'gtz': '(int64_t)%s > 0' % a}[k]
        if i.op == 1:
            return '(int64_t)%s %s 0' % (a, '<' if i.rt in (0, 2) else '>=')
        return 'fcc[%d] == %d' % ((i.w >> 18) & 7, i.rt & 1)

    def lift(self):
        img, insns = self.img, self.insns
        self.frame = 0
        self.reg_frame = None
        for k, i in enumerate(insns):
            if i.op in (0x19, 0x09) and i.rs == 29 and i.rt == 29 and i.imm < 0:
                self.frame = -i.imm
                break
            if k and i.op == 0 and i.fn == 0x2f and i.rd == 29 and i.rs == 29 and insns[k - 1].op == 0x0d and \
                    insns[k - 1].rs == 0 and insns[k - 1].rt == i.rt:
                self.frame = insns[k - 1].immu
                self.reg_frame = i.rt
                break
        F = self.frame
        # a function that homes its argument registers at the top of the frame walks them with a va_list: the
        # caller's stack arguments have to follow them in memory
        homes = {i.rt for i in insns if i.op == 0x3f and i.rs == 29 and 5 <= i.rt <= 11 and i.imm == F - 8 * (12 - i.rt)}
        # ... and so does one that takes the address of the end of its frame (a frame pointer, alloca)
        beyond = any(i.op in (0x19, 0x09) and i.rs == 29 and i.rt != 29 and i.imm >= F for i in insns)
        self.va = F > 0 and ({10, 11} <= homes or beyond)
        # the stack pointer moves while the function runs (alloca, or a frame pointer restored into it): then it
        # is a real variable, and the frame sits above a reserve that the function may grow into
        def moves_sp(i):
            if i.op in (0x19, 0x09) and i.rt == 29:
                return not (i.rs == 29 and abs(i.imm) == F)
            if i.op == 0 and i.rd == 29 and i.fn not in (8, 9, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f) and i.w:
                return not (i.rs == 29 and self.reg_frame == i.rt)
            return i.op in self.LD and i.rt == 29
        self.dyn = any(moves_sp(i) for i in insns)
        if self.dyn:
            self.va = F > 0
        self.fsize = F + 8 * VA_SLOTS if self.va else F
        self.nstack = 0
        slots = 0
        for i in insns:
            if i.rs == 29 and i.op in self.ST and 0 <= i.imm < min(64, F) and i.rt != 28:
                slots = max(slots, i.imm // 8 + 1)
        self.out_slots = slots
        self.labels = set(img.inner.get(self.addr, ()))
        for k, i in enumerate(insns):
            if is_control(i) and k + 1 >= len(insns):
                raise Unsupported('control transfer without delay slot')
            if is_branch(i) and self.addr <= btarget(i) < self.end:
                self.labels.add(btarget(i))
        body = []
        k = 0
        while k < len(insns):
            i = insns[k]
            if i.addr in self.labels:
                self.const = {}
                body.append('L_%x:;' % i.addr)
            if not is_control(i):
                s = self.stmt(i)
                if s:
                    body.append('    ' + s)
                k += 1
                continue
            d = insns[k + 1]
            if is_control(d):
                raise Unsupported('control transfer in a delay slot')
            saved = dict(self.const)
            if is_branch(i):
                c = self.cond(i)
                always = i.op in (0x04, 0x14) and i.rs == i.rt
                ds = self.stmt(d) or ''
                g = self.goto(btarget(i))
                if always:
                    body.append('    %s %s' % (ds, g))
                elif is_likely(i):
                    body.append('    if (%s) { %s %s }' % (c, ds, g))
                    self.const = saved             # the delay slot did not run on the fall-through path
                else:
                    body.append('    { int c_ = %s; %s if (c_) %s }' % (c, ds, g))
            elif is_bal(i):
                if i.rs != 0:
                    raise Unsupported('conditional branch and link')
                t = btarget(i)
                if t not in img.fstart:
                    raise Unsupported('bal into a function')
                ds = self.stmt(d) or ''
                body.append('    %s %s' % (ds, self.call(img.addr_c(t))))
                self.const = {}
            elif i.op == 0 and i.fn == 9:        # jalr
                ds = self.stmt(d) or ''
                body.append('    { uint64_t t_ = %s; %s %s }' % (self.R(i.rs), ds, self.call('t_')))
                self.const = {}
            elif i.op == 0 and i.fn == 8:        # jr
                ds = self.stmt(d) or ''
                if i.rs == 31:
                    body.append('    %s %s' % (ds, self.ret()))
                else:
                    cases = ''.join(' case 0x%xull: goto L_%x;' % (t, t) for t in sorted(img.inner.get(self.addr, ())))
                    body.append('    { uint64_t t_ = %s; %s switch (t_) {%s default: %s } }' % (
                        self.R(i.rs), ds, cases, self.tail('t_')))
            else:
                raise Unsupported('j/jal')
            k += 2
            if d.addr in self.labels:            # something branches to the delay slot itself
                self.const = {}
                s = self.stmt(d)
                body.append('    goto N_%x; L_%x:; %s N_%x:;' % (d.addr, d.addr, s or '', d.addr))
        body.append('    ' + self.ret())
        out = [PROTO % self.name, '{']
        out.append('    uint64_t ' + ', '.join('r%d = 0' % n for n in range(1, 32) if n not in (28, 29)) + ', lo = 0, hi = 0;')
        out.append('    (void)lo; (void)hi; ' + ' '.join('r%d = a%d;' % (4 + k, k) for k in range(NARGS)))
        nst = VA_SLOTS if self.va else self.nstack
        if nst:
            out.append('    uint64_t ' + ', '.join('s%d' % k for k in range(nst)) + ';')
            out.append('    { va_list ap_; va_start(ap_, a7); ' + ' '.join('s%d = va_arg(ap_, uint64_t);' % k for k in range(nst)) + ' va_end(ap_); }')
        if self.dyn:
            out.append('    uint8_t fbuf_[%d] __attribute__((aligned(16))); uint8_t *frame = fbuf_ + %d; uint64_t r29 = (uint64_t)(uintptr_t)frame;' % (
                DYN_RESERVE + max(self.fsize, 16), DYN_RESERVE))
        else:
            out.append('    uint8_t frame[%d] __attribute__((aligned(16))); (void)frame;' % max(self.fsize, 16))
        if self.va:
            out.append('    ' + ' '.join('*(uint64_t *)(frame + %d) = s%d;' % (F + 8 * k, k) for k in range(VA_SLOTS)))
        if self.fp:
            out.append('    uint64_t f[32] = {0}; int fcc[8] = {0}; (void)fcc; ' +
                       ' '.join('f[%d] = lift_fpa[%d];' % (12 + k, k) for k in range(8)))
        text = '\n'.join(out + body + ['}'])
        missing = [t for t in self.labels if ('L_%x:;' % t) not in text]
        if missing:
            raise Unsupported('branch into a delay slot or between instructions')
        return text


def Insn2(i):
    """The load/store view of a coprocessor memory instruction (same base and offset fields)."""
    return i


def swap_data(img):
    """Big-endian to little-endian for the bytes that carry no relocation.  The unit is what the code was seen
    to access at that address (8, 4, 2 bytes, or single bytes); strings stay; everything else is taken as
    32-bit words, 64-bit in .lit8."""
    e = img.elf
    blob = bytearray(img.hi - img.data_lo)
    for s in img.datasecs:
        if s['type'] == 8:
            continue
        b = bytearray(e.data[s['off']:s['off'] + s['size']])
        n = len(b)
        strs = nm_swap.string_mask(b, aligned_start=True) if s['name'] in ('.data', '.rodata', '.srdata', '.sdata') else bytearray(n)
        base = s['addr']
        i = 0
        while i < n:
            a = base + i
            if a in img.rel and i + 8 <= n:
                i += 8
                continue
            w = img.width.get(a, ())
            unit = 8 if (8 in w or s['name'] == '.lit8') else 4 if 4 in w else 2 if 2 in w else 1 if 1 in w else 4
            unit = min(unit, 8 - (a % 8) if a % 8 else 8)
            if unit == 4 and a % 4:
                unit = 1
            if unit > 1 and i + unit <= n and not any(strs[i:i + unit]) and not any((a + k) in img.rel for k in range(1, unit)):
                b[i:i + unit] = b[i:i + unit][::-1]
                i += unit
            else:
                i += 1
        blob[base - img.data_lo:base - img.data_lo + n] = b
    return blob


def emit_data(img, blob, w):
    e = img.elf
    name = img.name
    w.write('\t.data\n\t.balign 4096\n\t.globl __img_%s\n\t.hidden __img_%s\n__img_%s:\n' % (name, name, name))
    off, total = 0, len(blob)
    while off < total:
        a = img.data_lo + off
        if a in img.rel and off + 8 <= total:
            v = e.dword(a)
            k = img.rel[a]
            sym = e.dynsyms[k] if k < len(e.dynsyms) else None
            if sym is not None and k >= e.gotsym and not sym['shndx'] and sym['name']:
                # the word holds the address the symbol had when the object was quickstarted
                add = (v - e.dword(e.pltgot + 8 * (e.local_gotno + k - e.gotsym))) & 0xffffffffffffffff
                if add & (1 << 63):
                    add -= 1 << 64
                w.write('\t.quad %s + %d\n' % (sym['name'], add))
            else:
                w.write('\t.quad %s\n' % img.addr_s(v))
            off += 8
            continue
        n = 0
        while off + n < total and (img.data_lo + off + n) not in img.rel and n < 16:
            n += 1
        w.write('\t.byte %s\n' % ','.join(str(x) for x in blob[off:off + n]))
        off += n
    for s in e.dynsyms:
        if not s['name'] or not s['shndx'] or s['name'].startswith('.'):
            continue
        if s['type'] == 2 and s['value'] in img.fstart:
            continue
        if img.data_lo <= s['value'] < img.hi:
            w.write('\t.globl %s\n\t.set %s, __img_%s + %d\n\t.type %s,@object\n' % (
                s['name'], s['name'], name, s['value'] - img.data_lo, s['name']))
            if s['size']:
                w.write('\t.size %s,%d\n' % (s['name'], s['size']))
    w.write('\t.section .note.GNU-stack,"",@progbits\n')


def main():
    args = sys.argv[1:]
    path, out = args[0], Path(args[1])
    name = args[args.index('--name') + 1] if '--name' in args else re.sub(r'[^A-Za-z0-9]', '_', Path(path).name)
    out.mkdir(parents=True, exist_ok=True)
    img = Image(path, name)
    bodies, failed = [], []
    for addr, size in img.funcs:
        try:
            bodies.append(Lifter(img, addr, size).lift())
        except Unsupported as ex:
            failed.append((img.fstart[addr], str(ex)))
            bodies.append(PROTO % img.fstart[addr] + ' { abort(); } /* %s */' % ex)
    e = img.elf
    with open(out / 'code.c', 'w') as w:
        w.write('#include <stdint.h>\n#include <stdarg.h>\n#include <math.h>\n')
        w.write('extern void abort(void);\n')
        w.write('static inline float FS(uint64_t v) { union { uint32_t u; float f; } x; x.u = (uint32_t)v; return x.f; }\n')
        w.write('static inline double FD(uint64_t v) { union { uint64_t u; double d; } x; x.u = v; return x.d; }\n')
        w.write('static inline uint64_t SF(uint64_t o, float v) { union { uint32_t u; float f; } x; x.f = v; return x.u; }\n')
        w.write('static inline uint64_t DF(double v) { union { uint64_t u; double d; } x; x.d = v; return x.u; }\n')
        w.write('__thread uint64_t lift_fpa[8] __attribute__((weak)); __thread uint64_t lift_fpr __attribute__((weak));\n')
        w.write('extern char __img_%s[] __attribute__((visibility("hidden")));\n#define IMG __img_%s\n' % (name, name))
        for n_, c in sorted(img.syms.items(), key=lambda kv: int(kv[1][1:])):
            w.write('extern char %s[] __asm__("%s");\n' % (c, n_))
        for a, _ in img.funcs:
            w.write(PROTO % img.fstart[a] + ';\n')
        for b in bodies:
            w.write(b + '\n\n')
        for s in e.dynsyms:
            if s['name'] and s['shndx'] and s['type'] == 2 and s['value'] in img.fstart and re.fullmatch(r'[A-Za-z_$][\w$]*', s['name']):
                w.write('extern __typeof(%s) %s __attribute__((alias("%s")));\n' % (img.fstart[s['value']], s['name'], img.fstart[s['value']]))
        ini, fin = e.dyn.get(12), e.dyn.get(13)
        if ini in img.fstart:
            w.write('static void __attribute__((constructor)) lift_init_%s(void) { %s(0, 0, 0, 0, 0, 0, 0, 0); }\n' % (name, img.fstart[ini]))
        if fin in img.fstart:
            w.write('static void __attribute__((destructor)) lift_fini_%s(void) { %s(0, 0, 0, 0, 0, 0, 0, 0); }\n' % (name, img.fstart[fin]))
    blob = swap_data(img)
    with open(out / 'data.s', 'w') as w:
        emit_data(img, blob, w)
    undef = sorted({s['name'] for s in e.dynsyms if s['name'] and not s['shndx']})
    (out / 'needed.json').write_text(json.dumps({'needed': e.needed, 'undef': undef, 'failed': failed}, indent=1))
    print('functions=%d lifted=%d unsupported=%d  needed=%s' % (len(img.funcs), len(img.funcs) - len(failed), len(failed), e.needed))
    for r, n in Counter(f[1] for f in failed).most_common(12):
        print('  %5d  %s' % (n, r))


if __name__ == '__main__':
    main()
