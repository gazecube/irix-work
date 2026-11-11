#!/usr/bin/env python3
"""Lift a linked IRIX N64 shared library to native x86_64 C + assembler.

    lift_n64.py LIB.so OUTDIR [--name NAME] [--types FILE] [--like A=B] [--protos FILE] [--wrapped FILE]
                              [--lifted OTHER.so] [--hook FUNCTION=NATIVE]

    --types    field layouts of the exported data (n64_types.py)
    --protos   prototypes of the imported functions (n64_protos.py): native functions get narrow results extended,
               floating point passed their way, and the objects their pointer arguments name prepared and marked
    --wrapped  names the libc layer provides itself; it looks after their arguments
    --lifted   another IRIX library that is lifted as well: its functions are lifted functions, not native ones
    --hook     call NATIVE(a0) on entry to the exported FUNCTION

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

Mixed-width access.  Big-endian code reads and writes the same bytes at different widths (the low half of a long
through a 32-bit load of its last four bytes, a bit field through the word that also holds a short and a char,
structures copied eight bytes at a time).  Every load and store of lifted code goes through lift64_mem.h, which
keeps one shadow byte per 8-byte cell saying how the cell was last written and answers a mismatching access the
way the big-endian machine would; see that file.  Frames are cleared on entry, the initial data gets the states
it was converted with, and the libc layer maps the shadow, clears it for freed memory and copies it in memcpy.
Frames are cleared again on return: native frames use the same stack and pass pointers into it.
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


# functions of the libc / SGI Xlib layer that no host header declares
SHIM_PROTOS = {n: {'ret': 'i4', 'args': ['p']} for n in (
    '_XQstrcmp', '_XrmBufferToQuark', 'SgiGetNumLockModifiers', '__filbuf', '__semgetc', '__flsbuf', '__semputc',
    'BSDclosedir', 'setoserror', 'oserror')}
SHIM_PROTOS['__dcis'] = {'ret': 'cis', 'args': ['d']}     # libm: cos in $f0 and sin in $f2 of the double in $f12
# long double of the MIPSpro compilers is a pair of doubles (high part, low part): arguments in $f12/$f13 and
# $f14/$f15, result in $f0/$f2.  The libc helpers for it are computed in the host's long double.
for _n, _o in (('add', '+'), ('sub', '-'), ('mul', '*'), ('div', '/')):
    SHIM_PROTOS['__q_' + _n] = {'ret': 'quad', 'op': _o, 'args': []}
for _n, _o in (('eq', '=='), ('ge', '>='), ('gt', '>'), ('le', '<='), ('lt', '<')):
    SHIM_PROTOS['__q_' + _n] = {'ret': 'qcmp', 'op': _o, 'args': []}
SHIM_PROTOS['atold'] = {'ret': 'qstr', 'args': []}
QUAD_ARGS = "long double a_ = (long double)FD(f[12]) + FD(f[13]), b_ = (long double)FD(f[14]) + FD(f[15]);"
QUAD_RET = "double h_ = (double)r_; f[0] = DF(h_); f[2] = DF((double)(r_ - h_));"
CTYPE = {'i4': 'int32_t', 'u4': 'uint32_t', 'i2': 'int16_t', 'u2': 'uint16_t', 'i1': 'int8_t', 'u1': 'uint8_t'}

# CoreClassPart and XtResource in LP64, for class records that no header declares
CORE_CLASS = {
    'size': 224,
    'fields': [(0, 8), (8, 8), (16, 4), (24, 8), (32, 8), (48, 8), (56, 8), (64, 8), (72, 8), (80, 4), (88, 8), (96, 4), (104, 4)] +
              [(o, 8) for o in range(112, 224, 8)],
    'follow': [{'ptr': 88, 'count': 96, 'count_size': 4, 'elem_size': 48,
                'fields': [(0, 8), (8, 8), (16, 8), (24, 4), (28, 4), (32, 8), (40, 8)]}],
    'ext': [216],
}


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


def jtarget(i):
    """Target of j / jal: 26 bits of word address inside the 256 MB region of the delay slot."""
    return ((i.addr + 4) & ~0x0fffffff) | ((i.w & 0x03ffffff) << 2)


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
        self.arrays = defaultdict(set)        # (address of a field in row 0, row size) -> access widths
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
        # A program is linked at a fixed address, so its data pointers carry no relocations.  Every aligned word
        # of the initialised data that holds an address of the program is taken for a pointer.
        if e.etype == 2:
            for sec in data:
                if sec['type'] == 8:              # no contents (bss)
                    continue
                for a in range((sec['addr'] + 7) & ~7, sec['addr'] + sec['size'] - 7, 8):
                    v = e.dword(a)
                    if self.data_lo <= v < self.hi or (v % 4 == 0 and self.in_text(v)):
                        self.rel.setdefault(a, 0)
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
        self.protos = dict(SHIM_PROTOS)       # imported function -> {'ret': class, 'args': [classes]}
        self.marks = {}                       # field layout -> name of its lm_mark table
        # The GOT itself is data too: code built with large-GOT addressing loads its entries through a computed
        # address.  Every slot becomes a pointer word (key -1 - index); the two reserved slots stay as they are.
        for idx in range(2, (self.got_hi - self.got_lo) // 8):
            self.rel.setdefault(self.got_lo + 8 * idx, -1 - idx)
        self.class_recs = []                  # typed class records: (address, size, follow list)
        self.hooks = {}                       # function address -> native function called on entry with a0
        self.typed = {}                       # data address -> size of the scalar field that starts there
        self.covered = set()                  # 8-byte-aligned chunks that lie inside typed data

    def mark_table(self, fields):
        """Name of the field-width table for lm_mark (emitted once per distinct layout)."""
        key = tuple(tuple(f) for f in fields)
        if key not in self.marks:
            self.marks[key] = 'lmf_%d' % len(self.marks)
        return self.marks[key]

    def load_types(self, path, like=()):
        """Field layouts from n64_types.py: exported records, and the counted tables they point to.  `like` gives
        records that no header declares the layout of a declared one (NAME=OTHER)."""
        e = self.elf
        types = json.loads(Path(path).read_text())
        for pair in like:
            a, b = pair.split('=')
            types[a] = types[b]
        n = 0
        todo = []                             # class records reached through a superclass or class pointer

        def mark(base, size, fields):
            for a in range(base - base % 8, base + size, 8):
                self.covered.add(a)
            for off, sz in fields:
                self.typed[base + off] = sz
        for sym in e.dynsyms:
            t = types.get(sym['name'])
            if not t and sym['name'].endswith('ClassRec') and sym['size'] >= 224:
                t = CORE_CLASS                    # a class record the headers do not declare: its Core part at least
            if not t or sym['type'] != 1 or not sym['shndx'] or not (self.data_lo <= sym['value'] < self.hi):
                continue
            base = sym['value']
            if sym['size'] == 8 and sym['name'].endswith('Class') and base in self.rel:
                todo.append(e.dword(base))        # widgetClass and friends: a pointer to a class record
            mark(base, min(t['size'], sym['size'] or t['size']), [f for f in t['fields'] if not sym['size'] or f[0] < sym['size']])
            n += 1
            if sym['name'].endswith('ClassRec'):
                self.class_recs.append((base, min(t['size'], sym['size'] or t['size']), t['follow']))
            if sym['name'].endswith('ClassRec') and base in self.rel:
                todo.append(e.dword(base))        # the superclass
            for f in t['follow']:
                tgt = e.dword(base + f['ptr'])
                cnt = int.from_bytes(e.bytes_at(base + f['count'], f['count_size']), 'big')
                if self.data_lo <= tgt < self.hi and 0 < cnt < 4096:
                    for k in range(cnt):
                        mark(tgt + k * f['elem_size'], f['elem_size'], f['fields'])
            # class extension records all start with: next_extension, record_type (quark), version (long),
            # record_size (Cardinal); the rest differs per class and stays untyped
            for off in t.get('ext', ()):
                tgt = e.dword(base + off)
                for _ in range(8):
                    if not (self.data_lo <= tgt < self.hi) or tgt in self.typed:
                        break
                    mark(tgt, 32, [(0, 8), (8, 4), (16, 8), (24, 4)])
                    tgt = e.dword(tgt)
        # class records that are not exported: their Core part, their resources, their extension, their superclass
        for sym in e.dynsyms:
            if sym['type'] == 1 and sym['shndx'] and sym['size'] == 8 and sym['name'].endswith('Class') and sym['value'] in self.rel:
                todo.append(e.dword(sym['value']))
        t = CORE_CLASS
        while todo:
            base = todo.pop()
            if not (self.data_lo <= base < self.hi) or base in self.typed:
                continue
            mark(base, t['size'], t['fields'])
            self.class_recs.append((base, t['size'], t['follow']))
            n += 1
            f = t['follow'][0]
            tgt = e.dword(base + f['ptr'])
            cnt = int.from_bytes(e.bytes_at(base + f['count'], 4), 'big')
            if self.data_lo <= tgt < self.hi and 0 < cnt < 4096:
                for k in range(cnt):
                    mark(tgt + k * f['elem_size'], f['elem_size'], f['fields'])
            tgt = e.dword(base + 216)
            for _ in range(8):
                if not (self.data_lo <= tgt < self.hi) or tgt in self.typed:
                    break
                mark(tgt, 32, [(0, 8), (8, 4), (16, 8), (24, 4)])
                tgt = e.dword(tgt)
            if base in self.rel:
                todo.append(e.dword(base))
        return n

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
        # Any other address of the object, code included, is the image address moved by the same distance.  A GOT
        # page entry inside the code then still reaches the data after it when an offset is added at run time, and
        # a code address formed that way is translated when it is called (XL).
        if self.tlo - 0x10000 <= a < self.hi + 0x10000:
            return '(uint64_t)(uintptr_t)(IMG + %d)' % (a - self.data_lo)
        return '0x%xull' % a

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
        self.symreg = {}                       # reg -> name of the imported symbol whose address it holds
        self.shifted = {}                      # reg -> shift count of the dsll/sll that made it (an index times a row size)
        self.rowbase = {}                      # reg -> (table address, row size): a row of a static table
        self.tagged = {i.rt for i in self.insns if i.op in (0x37, 0x23, 0x27, 0x21, 0x25)} - {0, 28, 29}   # loaded from memory somewhere
        self.fixed = {}                        # callee-saved reg -> the one GOT load that defines it
        self.fixedsym = {}
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
        self.symreg.pop(reg, None)
        self.shifted.pop(reg, None)
        self.rowbase.pop(reg, None)
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
        return self.const.get(reg) or self.fixed.get(reg)

    def symbol_in(self, reg):
        return self.symreg.get(reg) or self.fixedsym.get(reg)

    def W(self, reg, expr, k=None):
        """Assignment to a register (writes to $zero, $gp and $sp are dropped)."""
        self.setk(reg, k)
        if reg in (0, 28):
            return None
        if reg == 29 and not self.dyn:
            raise Unsupported('write to sp')
        return 'r%d = %s;%s' % (reg, expr, ' t%d = 0;' % reg if reg in self.tagged else '')

    def T(self, reg):
        return 't%d' % reg if reg in self.tagged else '0'

    def args(self):
        return ['r%d' % n for n in range(4, 12)] + [self.outarg(k) for k in range(self.out_slots)]

    def typed_call(self, target, proto):
        """A call to a native function that takes or returns floating point: built with its own C type, the
        arguments taken from the N64 positions (integer register k or FP register 12 + k)."""
        ct = {'f': 'float', 'd': 'double', 'v': 'void'}
        types, vals = [], []
        for k, a in enumerate(proto['args']):
            if a == '...' or k >= NARGS:
                return None
            if a in ('f', 'd'):
                types.append(ct[a])
                vals.append(('FS(f[%d])' if a == 'f' else 'FD(f[%d])') % (12 + k))
            else:
                types.append('uint64_t')
                vals.append('r%d' % (4 + k))
        r = proto['ret']
        fn = '((%s (*)(%s))(uintptr_t)(%s))(%s)' % (ct.get(r, 'uint64_t'), ', '.join(types) or 'void', target, ', '.join(vals))
        if r == 'd':
            return 'f[0] = DF(%s);' % fn
        if r == 'f':
            return 'f[0] = SF(0, %s);' % fn
        if r == 'v':
            return '%s;' % fn
        return 'r2 = %s;%s' % (fn, self.narrow(r))

    def narrow(self, r):
        """A native function returns a narrow integer with the rest of the register undefined."""
        if r not in CTYPE:
            return ''
        # 32-bit values are kept sign-extended whatever their C type; unsigned char and short are zero-extended
        return ' r2 = (uint64_t)(int64_t)(int32_t)r2;' if r in ('i4', 'u4') else ' r2 = (uint64_t)(%s)(%s)r2;' % (
            'int64_t' if r[0] == 'i' else 'uint64_t', CTYPE[r])

    def call(self, target, name=None):
        return self.call1(target, name) + (' t2 = 0;' if 2 in self.tagged else '')

    def call1(self, target, name=None):
        proto = self.img.protos.get(name) if name else None
        if proto and proto.get('ptr'):
            pre, post = [], []
            for k, size, fields, cnt in proto['ptr']:
                # an object without a field list (a union such as XEvent) is left alone: the libc layer wraps the
                # functions that fill one in
                if k >= 8 + self.out_slots or not fields or cnt >= 8 + self.out_slots:
                    continue
                arg = lambda j: 'r%d' % (4 + j) if j < 8 else self.outarg(j - 8)
                tab = self.img.mark_table(fields)
                pre.append('uintptr_t p%d_ = (uintptr_t)%s; uint64_t n%d_ = %s; lm_conv(p%d_, %s, %d, n%d_);' % (
                    k, arg(k), k, '(uint32_t)%s' % arg(cnt) if cnt >= 0 else '1', k, tab, size, k))
                post.append('lm_mark(p%d_, %s, %d, n%d_);' % (k, tab, size, k))
            return '{ %s %s %s }' % (' '.join(pre), self.call2(target, proto), ' '.join(post))
        return self.call2(target, proto)

    def call2(self, target, proto):
        if proto and proto['ret'] == 'quad':
            return '{ %s long double r_ = a_ %s b_; %s }' % (QUAD_ARGS, proto['op'], QUAD_RET)
        if proto and proto['ret'] == 'qcmp':
            return '{ %s r2 = a_ %s b_; }' % (QUAD_ARGS, proto['op'])
        if proto and proto['ret'] == 'qstr':
            return '{ long double r_ = strtold((const char *)(uintptr_t)r4, 0); %s }' % QUAD_RET
        if proto and proto['ret'] == 'cis':
            return '{ double s_, c_; sincos(FD(f[12]), &s_, &c_); f[0] = DF(c_); f[2] = DF(s_); }'
        if proto and self.fp and (proto['ret'] in ('f', 'd') or 'f' in proto['args'] or 'd' in proto['args']):
            st = self.typed_call(target, proto)
            if st:
                return st
        pre = ' '.join('lift_fpa[%d] = f[%d];' % (k, 12 + k) for k in range(8)) + ' ' if self.fp else ''
        post = ' f[0] = lift_fpr;' if self.fp else ''
        return '%sr2 = %s(%s);%s%s' % (pre, CALLT % target, ', '.join(self.args()), post, self.narrow(proto['ret']) if proto else '')

    def tail(self, target, name=None):
        proto = self.img.protos.get(name) if name else None
        if proto and (proto.get('ptr') or proto['ret'] in CTYPE or proto['ret'] in ('f', 'd') or 'f' in proto['args'] or 'd' in proto['args']):
            return '{ %s %s }' % (self.call(target, name), self.ret())
        pre = ' '.join('lift_fpa[%d] = f[%d];' % (k, 12 + k) for k in range(8)) + ' ' if self.fp else ''
        return '{ %sreturn %s(%s); }' % (pre, CALLT % target, ', '.join(self.args()))

    def outarg(self, k):
        return '*(uint64_t *)(uintptr_t)(r29 + %d)' % (8 * k) if self.dyn else '*(uint64_t *)(frame + %d)' % (8 * k)

    def ret(self):
        return ('lift_fpr = f[0]; ' if self.fp else '') + 'return r2;'

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
        if i.rs in self.rowbase:
            base, row = self.rowbase[i.rs]
            if row >= 8 and 0 <= i.imm:
                self.img.arrays[(base + i.imm, row)].add(width)
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
            if within + width == 8:            # an int argument rewritten in its lane: the slot holds that value now
                return 's%d = %s;' % (slot, S32(self.R(i.rt)) if width == 4 else '(uint64_t)(%s)%s' % (ctype, self.R(i.rt)))
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
            a = '(uintptr_t)%s' % a
            tg = '&t%d' % i.rt if i.rt in self.tagged else '&tx'
            if width == 1:
                v = 'LD1(%s)' % a
            else:
                v = 'LD%d(%s, %s)' % (width, a, tg)
            st = self.W(i.rt, '(uint64_t)(int64_t)(%s)%s' % (ctype, v) if ctype[0] == 'i' else '(uint64_t)%s' % v)
            if st and width > 1:
                st = st.replace(' t%d = 0;' % i.rt, '')      # the load set the tag
            return st
        ctype, width = self.ST[op]
        if i.rt == 28 and i.rs == 29:
            return None                        # saving $gp: it is a constant here
        if i.rs == 29 and width < 8 and 0 <= i.imm < 64 and i.imm // 8 in self.outargs and i.imm % 8 + width == 8:
            v = self.R(i.rt)
            return '*(uint64_t *)(frame + %d) = %s; SH(frame + %d) = ST_Q;' % (
                i.imm - i.imm % 8, S32(v) if width == 4 else '(uint64_t)(%s)%s' % (ctype, v), i.imm - i.imm % 8)
        a = self.ea(i, width)
        if a is None:
            return self.incoming(i, ctype, width, True)
        if width == 1:
            return 'ST1((uintptr_t)%s, (uint8_t)%s);' % (a, self.R(i.rt))
        return 'ST%d((uintptr_t)%s, (%s)%s, %s);' % (width, a, ctype, self.R(i.rt), self.T(i.rt))

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
        st = self.W(i.rt, '(uint64_t)(uintptr_t)%s' % self.img.sym(name), k)
        if not v['shndx'] and i.rt not in (0, 28, 29):
            self.symreg[i.rt] = name
        return st

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
                name = self.symbol_in(src)
                st = W(rd, R(src) if fn != 0x21 else S32(R(src)), self.K(src) if fn != 0x21 else None)
                if name and fn != 0x21 and rd not in (0, 28, 29):
                    self.symreg[rd] = name
                if st and fn != 0x21 and rd in self.tagged and src in self.tagged and rd != src:
                    st = st.replace(' t%d = 0;' % rd, ' t%d = t%d;' % (rd, src))
                return st
            if fn in (0x2d, 0x2f) and rd == 29 and rs == 29 and getattr(self, 'reg_frame', None) == rt:
                return None                    # frame set-up or tear-down through a register
            if fn == 0x2d and ka and kb and (ka[1] or kb[1]):            # daddu of two known values: an address
                v = (ka[0] + kb[0]) & 0xffffffffffffffff
                return W(rd, self.img.addr_c(v), (v, True))
            if fn in (0x2d, 0x21):                                       # table address + index * row size
                row = None
                for x, y, kx in ((rs, rt, ka), (rt, rs, kb)):
                    if kx and kx[1] and y in self.shifted and self.img.data_lo <= kx[0] < self.img.hi:
                        row = (kx[0], 1 << self.shifted[y])
                    elif x in self.rowbase and y not in self.rowbase and not (self.K(y) and self.K(y)[1]):
                        row = row or self.rowbase[x]
                if row:
                    st = W(rd, '%s + %s' % (a, b) if fn == 0x2d else S32('(uint32_t)%s + (uint32_t)%s' % (a, b)))
                    if rd not in (0, 28, 29):
                        self.rowbase[rd] = row
                    return st
            if fn in (0x38, 0x00) and sa and rd not in (0, 28, 29):      # dsll / sll: remember the scale
                st = W(rd, '%s << %d' % (b, sa) if fn == 0x38 else S32('(uint32_t)%s << %d' % (b, sa)))
                self.shifted[rd] = sa
                return st
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
                st = W(rd, t[fn])
                if st and fn in (0x24, 0x25, 0x26, 0x27) and rd in self.tagged and (rs in self.tagged or rt in self.tagged):
                    st = st.replace(' t%d = 0;' % rd, ' t%d = lm_bits(%s, %s);' % (rd, self.T(rs), self.T(rt)))
                return st
            if fn == 0x0a:
                self.setk(rd, None)
                return 'if (%s == 0) r%d = %s;%s' % (b, rd, a, ' t%d = 0;' % rd if rd in self.tagged else '') if rd else None
            if fn == 0x0b:
                self.setk(rd, None)
                return 'if (%s != 0) r%d = %s;%s' % (b, rd, a, ' t%d = 0;' % rd if rd in self.tagged else '') if rd else None
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
            return self.bitop(i, W(rt, '%s & 0x%xull' % (R(rs), i.immu)))
        if op == 0x0d:
            k = self.K(rs)
            return self.bitop(i, W(rt, '%s | 0x%xull' % (R(rs), i.immu), (k[0] | i.immu, False) if k and not k[1] else None))
        if op == 0x0e:
            return self.bitop(i, W(rt, '%s ^ 0x%xull' % (R(rs), i.immu)))
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

    def bitop(self, i, st):
        """andi / ori / xori of a value that came from memory keep its layout tag (a bit field being updated)."""
        if st and i.rt in self.tagged and i.rs in self.tagged and i.rt != i.rs:
            st = st.replace(' t%d = 0;' % i.rt, ' t%d = t%d;' % (i.rt, i.rs))
        elif st and i.rt in self.tagged and i.rt == i.rs:
            st = st.replace(' t%d = 0;' % i.rt, '')
        return st

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
            ft = i.rt
            if a is None:                      # a floating-point argument the caller passed on the stack
                off = i.imm - self.frame
                slot, within = off // 8, off % 8
                if off < 0 or within + width > 8:
                    raise Unsupported('fp access of an incoming slot')
                self.nstack = max(self.nstack, slot + 1)
                sh = (8 - within - width) * 8
                if op == 0x35:
                    return 'f[%d] = s%d;' % (ft, slot)
                if op == 0x31:
                    return 'f[%d] = (uint32_t)(s%d >> %d);' % (ft, slot, sh)
                if op == 0x3d:
                    return 's%d = f[%d];' % (slot, ft)
                raise Unsupported('fp access of an incoming slot')
            a = '(uintptr_t)%s' % a
            if op == 0x31:
                return 'f[%d] = LD4(%s, &tx);' % (ft, a)
            if op == 0x35:
                return 'f[%d] = LD8(%s, &tx);' % (ft, a)
            if op == 0x39:
                return 'ST4(%s, (uint32_t)f[%d], 0);' % (a, ft)
            return 'ST8(%s, f[%d], 0);' % (a, ft)
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
        # Outgoing stack arguments are the slots at the bottom of the frame that the function only stores to: it
        # never loads them back and never takes their address.  A narrow store there (an int written to the last
        # four bytes of its slot, the big-endian lane) is widened to the whole slot, which is what the callee reads.
        stored, used = set(), set()
        lim = min(64, F)
        for i in insns:
            if i.rs != 29 or not 0 <= i.imm < lim:
                continue
            if i.op in self.ST and i.rt != 28:
                stored.add(i.imm // 8)
            elif i.op in self.LD or i.op in (0x31, 0x35, 0x39, 0x3d, 0x1a, 0x1b, 0x2c, 0x2d):
                used.add(i.imm // 8)
            elif i.op in (0x19, 0x09) and i.rt != 29:
                used.update(range(i.imm // 8, 8))
        self.outargs = set() if self.dyn else stored - used
        self.out_slots = max(stored) + 1 if stored else 0
        # A callee-saved register that the function sets once, from the GOT, holds that address for the whole
        # function (a function pointer or a table base kept across a loop); reloading it from the frame on the way
        # out does not count.
        defs = defaultdict(list)
        for i in insns:
            if is_control(i) or not i.w:
                continue
            if i.op in self.LD or i.op in (0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x18, 0x19, 0x1a):
                dst = i.rt
            elif i.op == 0 and i.fn not in (0x08, 0x09, 0x11, 0x13, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f) and i.fn < 0x30 or \
                    (i.op == 0 and i.fn >= 0x38):
                dst = i.rd
            elif i.op == 0x11 and i.rs in (0, 1, 2):
                dst = i.rt
            else:
                continue
            if dst in (16, 17, 18, 19, 20, 21, 22, 23, 30) and not (i.op == 0x37 and i.rs == 29):
                defs[dst].append(i)
        e = img.elf
        for reg, dl in defs.items():
            i = dl[0]
            if len(dl) != 1 or i.op != 0x37 or i.rs != 28 or not (img.got_lo <= e.gp + i.imm < img.got_hi):
                continue
            kind, v = e.got_entry((e.gp + i.imm - img.got_lo) // 8)
            if kind == 'local':
                self.fixed[reg] = (v, True)
            elif not v['shndx'] and v['name']:
                self.fixedsym[reg] = v['name']
            elif v['shndx'] and not img.in_text(v['value']):
                self.fixed[reg] = (v['value'], True)
        self.defs = defs
        self.labels = set(img.inner.get(self.addr, ()))
        for k, i in enumerate(insns):
            if is_control(i) and k + 1 >= len(insns):
                raise Unsupported('control transfer without delay slot')
            if is_branch(i) and self.addr <= btarget(i) < self.end:
                self.labels.add(btarget(i))
            if i.op == 2 and self.addr <= jtarget(i) < self.end:
                self.labels.add(jtarget(i))
        body = []
        k = 0
        # PIC entry convention: $t9 holds the function's own address, from which a function without a frame
        # computes the GOT pointer into any register
        self.const = {} if self.addr in self.labels else {25: (self.addr, True)}
        entry_done = self.addr in self.labels
        while k < len(insns):
            i = insns[k]
            if i.addr in self.labels:
                if not entry_done:
                    # the code before the first label runs once, in order: what it leaves in a callee-saved
                    # register that nothing later writes holds for the rest of the function
                    entry_done = True
                    for reg in (16, 17, 18, 19, 20, 21, 22, 23, 30):
                        if all(x.addr < i.addr for x in self.defs.get(reg, ())):
                            if reg in self.const:
                                self.fixed[reg] = self.const[reg]
                            if reg in self.symreg:
                                self.fixedsym[reg] = self.symreg[reg]
                self.const = {}
                self.symreg = {}
                self.shifted = {}
                self.rowbase = {}
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
                self.symreg = {}
            elif i.op == 0 and i.fn == 9:        # jalr
                name = self.symbol_in(i.rs)
                kk = self.K(i.rs)
                tv = self.R(i.rs) if name or (kk and kk[1] and kk[0] in img.fstart) else 'XL(%s)' % self.R(i.rs)
                ds = self.stmt(d) or ''
                body.append('    { uint64_t t_ = %s; %s %s }' % (tv, ds, self.call('t_', name)))
                self.const = {}
                self.symreg = {}
            elif i.op == 0 and i.fn == 8:        # jr
                ds = self.stmt(d) or ''
                if i.rs == 31:
                    body.append('    %s %s' % (ds, self.ret()))
                else:
                    name = self.symbol_in(i.rs)
                    cases = ''.join(' case 0x%xull: goto L_%x;' % (t, t) for t in sorted(img.inner.get(self.addr, ())))
                    body.append('    { uint64_t t_ = %s; %s switch (XO(t_)) {%s default: %s } }' % (
                        self.R(i.rs), ds, cases, self.tail('t_' if name else 'XL(t_)', name)))
            elif i.op == 3:                      # jal: programs call their own functions this way
                t = jtarget(i)
                if t not in img.fstart:
                    raise Unsupported('jal into a function')
                ds = self.stmt(d) or ''
                body.append('    %s %s' % (ds, self.call(img.addr_c(t))))
                self.const = {}
                self.symreg = {}
            else:                                # j
                ds = self.stmt(d) or ''
                body.append('    %s %s' % (ds, self.goto(jtarget(i))))
            k += 2
            if d.addr in self.labels:            # something branches to the delay slot itself
                self.const = {}
                self.symreg = {}
                s = self.stmt(d)
                body.append('    goto N_%x; L_%x:; %s N_%x:;' % (d.addr, d.addr, s or '', d.addr))
        body.append('    ' + self.ret())
        out = [PROTO % self.name, '{']
        out.append('    uint64_t ' + ', '.join('r%d = 0' % n for n in range(1, 32) if n not in (28, 29)) + ', lo = 0, hi = 0;')
        out.append('    (void)lo; (void)hi; ' + ' '.join('r%d = a%d;' % (4 + k, k) for k in range(NARGS)))
        if self.addr in self.img.hooks:
            out.append('    %s(a0);' % self.img.hooks[self.addr])
        nst = VA_SLOTS if self.va else self.nstack
        if nst:
            out.append('    uint64_t ' + ', '.join('s%d' % k for k in range(nst)) + ';')
            out.append('    { va_list ap_; va_start(ap_, a7); ' + ' '.join('s%d = va_arg(ap_, uint64_t);' % k for k in range(nst)) + ' va_end(ap_); }')
        if self.dyn:
            out.append('    uint8_t fbuf_[%d] __attribute__((aligned(16))); uint8_t *frame = fbuf_ + %d; uint64_t r29 = (uint64_t)(uintptr_t)frame;' % (
                DYN_RESERVE + max(self.fsize, 16), DYN_RESERVE))
        else:
            out.append('    uint8_t frame[%d] __attribute__((aligned(16))); (void)frame;' % max(self.fsize, 16))
        out.append('    __builtin_memset(&SH(frame), 0, %d);' % ((max(self.fsize, 16) + 7) // 8))
        # and no states left behind: native frames use the same stack and hand out pointers into it
        if self.dyn:
            out.append('    struct lift_fr fr_ __attribute__((cleanup(lift_fr_done))) = {fbuf_, %d}; (void)fr_;' % ((DYN_RESERVE + max(self.fsize, 16) + 7) // 8))
        else:
            out.append('    struct lift_fr fr_ __attribute__((cleanup(lift_fr_done))) = {frame, %d}; (void)fr_;' % ((max(self.fsize, 16) + 7) // 8))
        out.append('    lift_tag tx = 0' + ''.join(', t%d = 0' % n for n in sorted(self.tagged)) + '; (void)tx;')
        if self.va:
            out.append('    ' + ' '.join('*(uint64_t *)(frame + %d) = s%d; SH(frame + %d) = ST_Q;' % (F + 8 * k, k, F + 8 * k) for k in range(VA_SLOTS)))
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
    img.states = bytearray((img.hi - img.data_lo + 7) // 8)       # shadow byte of every cell of the initial data

    def state(a, unit):
        c, o = (a - img.data_lo) // 8, (a - img.data_lo) % 8
        h = o // 4
        if unit == 8:
            img.states[c] = 0x0f
        elif unit == 4:
            img.states[c] = (img.states[c] & ~(3 << (2 * h)) & ~(3 << (4 + 2 * h))) | (1 << (2 * h))
        else:                                 # narrower units: a 16-bit value, or single bytes
            img.states[c] = (img.states[c] & ~(3 << (2 * h))) | (2 << (2 * h)) | ((0x10 << (o // 2)) if unit == 2 else 0)
    # Tables indexed by row: the field seen in the code repeats every row.  The table ends where something else is
    # known to start: an exported symbol, typed data, or the end of the section.
    bounds = sorted({s_['value'] for s_ in e.dynsyms if s_['shndx'] and img.data_lo <= s_['value'] < img.hi} |
                    {s_['addr'] + s_['size'] for s_ in img.datasecs})
    import bisect
    for (a0, row), ws in sorted(img.arrays.items()):
        if not (img.data_lo <= a0 < img.hi):
            continue
        end = bounds[bisect.bisect_right(bounds, a0)] if bisect.bisect_right(bounds, a0) < len(bounds) else img.hi
        a = a0
        while a < end and a not in img.typed and (a - a % 8) not in img.covered and (a - a0) // row < 8192:
            if not img.width.get(a) or a == a0:
                img.width[a] |= ws
            a += row
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
                state(a, 8)
                i += 8
                continue
            w = img.width.get(a, ())
            if a in img.typed:
                unit = img.typed[a]
            elif (a - a % 8) in img.covered:
                unit = 1                          # inside a typed record, not the start of a field wider than a byte
            elif a % 8 == 0 and i + 8 <= n and b[i:i + 4] == b'\xff\xff\xff\xff' and b[i + 4] & 0x80 and not w:
                unit = 8                          # a small negative 64-bit value (FAST_QUARKS names are stored like this)
            else:
                unit = 8 if (8 in w or s['name'] == '.lit8') else 4 if 4 in w else 2 if 2 in w else 1 if 1 in w else 4
                if not w and unit == 4 and any(img.width.get(a + k) for k in (1, 2, 3)):
                    unit = 1                      # something narrower starts inside this word: not a 32-bit value
            unit = min(unit, 8 - (a % 8) if a % 8 else 8)
            if unit == 4 and a % 4:
                unit = 1
            if unit > 1 and i + unit <= n and not any(strs[i:i + unit]) and not any((a + k) in img.rel for k in range(1, unit)):
                b[i:i + unit] = b[i:i + unit][::-1]
                state(a, unit if a % unit == 0 else 1)
                i += unit
            else:
                state(a, 1)
                i += 1
        blob[base - img.data_lo:base - img.data_lo + n] = b
    return blob


def emit_data(img, blob, w):
    e = img.elf
    name = img.name
    # Code addresses of the original are image-relative too (addr_c): the range below the image stands for the
    # original code.  It holds a jump to the lifted function at the place of every function start, so a function
    # address that the code forms at run time (GOT page plus offset) is a real, callable address for everyone,
    # native code included.  One section, so the distance to the image is exact; the libc layer's loader hook is
    # not needed, the constructor in code.c makes the jump area read-only.
    lead = -(img.data_lo - img.tlo) % 4096
    pad = lead + img.data_lo - img.tlo
    w.write('\t.section .lift_%s,"awx",@progbits\n\t.balign 4096\n\t.globl __tramp_%s\n\t.hidden __tramp_%s\n__tramp_%s:\n' % (name, name, name, name))
    for a, _ in img.funcs:
        w.write('\t.org %d, 0xcc\n\tjmp %s\n' % (lead + a - img.tlo, img.fstart[a]))
    w.write('\t.org %d, 0xcc\n\t.globl __img_%s\n\t.hidden __img_%s\n__img_%s:\n' % (pad, name, name, name))
    off, total = 0, len(blob)
    while off < total:
        a = img.data_lo + off
        if a in img.rel and off + 8 <= total:
            v = e.dword(a)
            k = img.rel[a]
            if k < 0:                              # a GOT slot
                kind, g = e.got_entry(-1 - k)
                if kind == 'global' and not g['shndx']:
                    w.write('\t.quad %s\n' % (g['name'] or '0'))
                else:
                    t = g if kind == 'local' else g['value']
                    if t in img.fstart:
                        w.write('\t.quad %s\n' % img.fstart[t])
                    elif img.tlo - 0x10000 <= t < img.hi + 0x10000:
                        w.write('\t.quad __img_%s + %d\n' % (name, t - img.data_lo))
                    else:
                        w.write('\t.quad 0x%x\n' % t)
                off += 8
                continue
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
    # __elf_size_NAME and __elf_vaddr_NAME: the IRIX linker's symbols for the section NAME (the C++ start-up code
    # walks __cplinit with them).  An object without that section has an empty one.
    for s in e.dynsyms:
        m = re.fullmatch(r'__elf_(size|vaddr)_(\w+)', s['name'] or '')
        if not m or s['shndx']:
            continue
        sec = e.byname.get(m.group(2))
        if m.group(1) == 'size':
            w.write('\t.globl %s\n\t.hidden %s\n\t.set %s, %d\n' % (s['name'], s['name'], s['name'], sec['size'] if sec else 0))
        elif sec and img.data_lo <= sec['addr'] < img.hi:
            w.write('\t.globl %s\n\t.hidden %s\n\t.set %s, __img_%s + %d\n' % (s['name'], s['name'], s['name'], name, sec['addr'] - img.data_lo))
    w.write('\t.section .note.GNU-stack,"",@progbits\n')


def main():
    args = sys.argv[1:]
    path, out = args[0], Path(args[1])
    name = args[args.index('--name') + 1] if '--name' in args else re.sub(r'[^A-Za-z0-9]', '_', Path(path).name)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'lift64_mem.h').write_text((here / 'lift64_mem.h').read_text())
    img = Image(path, name)
    if '--protos' in args:
        img.protos.update(json.loads(Path(args[args.index('--protos') + 1]).read_text()))
    for i, a in enumerate(args):
        if a == '--lifted':                   # --lifted LIB.so: an IRIX library that is lifted too; its functions are
            for x in Elf(args[i + 1]).dynsyms:    # called like any lifted function, not like native ones
                if x['shndx'] and x['name']:
                    img.protos.pop(x['name'], None)
    if '--wrapped' in args:
        # functions the libc layer provides itself take IRIX layouts and look after the memory states themselves
        for n in Path(args[args.index('--wrapped') + 1]).read_text().split():
            img.protos.get(n, {}).pop('ptr', None)
    for i, a in enumerate(args):
        if a == '--hook':                     # --hook FUNCTION=NATIVE: call NATIVE(a0) on entry to the exported FUNCTION
            fn, native = args[i + 1].split('=')
            hit = [x['value'] for x in img.elf.dynsyms if x['name'] == fn and x['shndx']]
            if not hit or hit[0] not in img.fstart:
                sys.exit('--hook: %s is not a function of this object' % fn)
            img.hooks[hit[0]] = native
    if '--types' in args:
        print('typed records: %d' % img.load_types(args[args.index('--types') + 1], [args[i + 1] for i, a in enumerate(args) if a == '--like']))
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
        w.write('extern void sincos(double, double *, double *);\n')
        w.write('extern long double strtold(const char *, char **);\n')
        w.write('static inline float FS(uint64_t v) { union { uint32_t u; float f; } x; x.u = (uint32_t)v; return x.f; }\n')
        w.write('static inline double FD(uint64_t v) { union { uint64_t u; double d; } x; x.u = v; return x.d; }\n')
        w.write('static inline uint64_t SF(uint64_t o, float v) { union { uint32_t u; float f; } x; x.f = v; return x.u; }\n')
        w.write('static inline uint64_t DF(double v) { union { uint64_t u; double d; } x; x.d = v; return x.u; }\n')
        w.write('__thread uint64_t lift_fpa[8] __attribute__((weak)); __thread uint64_t lift_fpr __attribute__((weak));\n')
        w.write('extern char __img_%s[] __attribute__((visibility("hidden")));\n#define IMG __img_%s\n' % (name, name))
        w.write('#include "lift64_mem.h"\n')
        for n_, c in sorted(img.syms.items(), key=lambda kv: int(kv[1][1:])):
            w.write('extern char %s[] __asm__("%s");\n' % (c, n_))
        for a, _ in img.funcs:
            w.write(PROTO % img.fstart[a] + ';\n')
        # an original code address that reaches a call (a pointer the lifter could not turn into a label)
        w.write('static const struct { uint64_t a; void *f; } xl_tab[] = {\n')
        for a, _ in img.funcs:
            w.write('    { 0x%xull, %s },\n' % (a, img.fstart[a]))
        w.write('};\n')
        w.write('static __attribute__((noinline)) uint64_t xl_find(uint64_t t)\n{\n'
                '    unsigned lo = 0, hi = sizeof xl_tab / sizeof xl_tab[0];\n'
                '    while (lo < hi) { unsigned m = (lo + hi) / 2; if (xl_tab[m].a == t) return (uint64_t)(uintptr_t)xl_tab[m].f;'
                ' if (xl_tab[m].a < t) lo = m + 1; else hi = m; }\n    abort();\n}\n')
        w.write('/* a code address formed from an image-relative page: as an address of the original object */\n'
                'static inline uint64_t XO(uint64_t t) { uint64_t o = t - (uint64_t)(uintptr_t)IMG + 0x%xull; return o - 0x%xull < 0x%xull ? o : t; }\n' % (
                    img.data_lo, img.tlo, img.thi - img.tlo))
        w.write('static inline uint64_t XL(uint64_t t) { return t - 0x%xull < 0x%xull ? xl_find(t) : t; }\n' % (img.tlo, img.thi - img.tlo))
        w.write('extern char __tramp_%s[] __attribute__((visibility("hidden")));\n' % name)
        w.write('extern int mprotect(void *, unsigned long, int);\n'
                'static void __attribute__((constructor(102))) lift_tramp_%s(void) { mprotect(__tramp_%s, (unsigned long)(IMG - __tramp_%s), 5); }\n' % (name, name, name))
        # the typed class records, for native classes derived from them (lift_native_class in the libc layer)
        rows = []
        for k, (base, size, follow) in enumerate(sorted(img.class_recs)):
            fl = ', '.join('{%d, %d, %d, %d, %s}' % (f['ptr'], f['count'], f['count_size'], f['elem_size'],
                                                      img.mark_table([x for x in f['fields'] if x[1] > 1])) for f in follow)
            rows.append((k, base, size, len(follow), fl))
        for h in sorted(set(img.hooks.values())):
            w.write('extern void %s(uint64_t);\n' % h)
        for key, nm in img.marks.items():
            w.write('static const uint16_t %s[] = {%s 0xffff};\n' % (nm, ''.join('%d,%d, ' % f for f in key)))
        for k, base, size, nf, fl in rows:
            if nf:
                w.write('static const struct lift_follow lcf_%d[] = {%s};\n' % (k, fl))
        w.write('static const struct lift_class lc_tab[] = {\n')
        for k, base, size, nf, fl in rows:
            w.write('    {(const char *)(uintptr_t)%s, %d, %s, %d},\n' % (img.addr_c(base), size, 'lcf_%d' % k if nf else '0', nf))
        w.write('    {0, 0, 0, 0}\n};\n')
        w.write('extern void lift_class_register(const struct lift_class *, unsigned) __attribute__((weak));\n'
                'static void __attribute__((constructor(103))) lift_classes_%s(void) { if (lift_class_register) lift_class_register(lc_tab, %d); }\n' % (name, len(rows)))
        for b in bodies:
            w.write(b + '\n\n')
        for s in e.dynsyms:
            if s['name'] not in ('_init', '_fini') and s['name'] and s['shndx'] and s['type'] == 2 and s['value'] in img.fstart and re.fullmatch(r'[A-Za-z_$][\w$]*', s['name']):
                w.write('extern __typeof(%s) %s __attribute__((alias("%s")));\n' % (img.fstart[s['value']], s['name'], img.fstart[s['value']]))
        ini, fin = e.dyn.get(12), e.dyn.get(13)
        if ini in img.fstart:
            w.write('static void __attribute__((constructor)) lift_init_%s(void) { %s(0, 0, 0, 0, 0, 0, 0, 0); }\n' % (name, img.fstart[ini]))
        if fin in img.fstart:
            w.write('static void __attribute__((destructor)) lift_fini_%s(void) { %s(0, 0, 0, 0, 0, 0, 0, 0); }\n' % (name, img.fstart[fin]))
    blob = swap_data(img)
    with open(out / 'data.s', 'w') as w:
        emit_data(img, blob, w)
    with open(out / 'code.c', 'a') as w:
        # the shadow of the initial data, in place before any lifted code runs
        w.write('static const uint8_t sh_init[] = {%s};\n' % ','.join(str(x) for x in img.states))
        w.write('static void __attribute__((constructor(102))) lift_shadow_%s(void) { __builtin_memcpy(&SH(IMG), sh_init, sizeof sh_init); }\n' % name)
    undef = sorted({s['name'] for s in e.dynsyms if s['name'] and not s['shndx']})
    (out / 'needed.json').write_text(json.dumps({'needed': e.needed, 'undef': undef, 'failed': failed}, indent=1))
    print('functions=%d lifted=%d unsupported=%d  needed=%s' % (len(img.funcs), len(img.funcs) - len(failed), len(failed), e.needed))
    for r, n in Counter(f[1] for f in failed).most_common(12):
        print('  %5d  %s' % (n, r))


if __name__ == '__main__':
    main()
