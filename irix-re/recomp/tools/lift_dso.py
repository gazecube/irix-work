#!/usr/bin/env python3
"""Lift a linked IRIX N32 shared library (or executable) to native C + assembler.

    lift_dso.py LIB.so OUTDIR [--name NAME]

The input is a stripped, linked ELF: no per-object relocations.  What the lifter uses instead:
  * code addresses come from the GOT: `lw rt, imm($gp)` loads a link-time constant (local entry) or a dynamic
    symbol (global entry); everything derived from it by addiu/move is folded by the C compiler;
  * the allocated image is emitted as ONE blob (__img_NAME) at its link-time layout, so every in-image address is
    `__img + (addr - base)`; REL32 words in the blob become symbolic `.long` expressions;
  * function boundaries: exported symbols, .debug_frame pieces outside exported functions, and GOT / REL32 targets;
    an object rearranged after linking (cord) has a .MIPS.Xlate table, which gives every procedure's start and maps
    the DWARF addresses to where the code is now;
  * jump tables: runs of REL32 words that point into the same function.
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
import lift_lib as LL                                   # noqa: E402
import lift_n32 as M                                    # noqa: E402
from lift_n32 import Unsupported, lhs, R               # noqa: E402
from elf_n32 import Elf                                 # noqa: E402
import nm_swap                                          # noqa: E402

R_MIPS_REL32 = 3


class Adapter:
    """What FuncLifter expects of an object file, over a linked image."""
    def __init__(self, elf, name):
        self.elf, self.name, self.gp0 = elf, name, 0
        self.sec = [dict(s, no=s['no']) for s in elf.sec]
        self.data = elf.data
        self.syms = []
        self.image = {}


class DsoInfo:
    def __init__(self, elf, name):
        self.o = Adapter(elf, name)
        self.relocs = {}
        self.funcs = {}
        self.narrow = {}
        self.tag = name

    def symname(self, s):
        return s['name']

    def func_at(self, shndx, off):
        return None


class Image:
    """Discovery and naming over a linked ELF."""
    def __init__(self, path, name):
        self.elf = e = Elf(path)
        self.name = name
        self.text = [s for s in e.sec if s['name'] in ('.text', '.init', '.fini') and s['size']]
        self.lo = min(s['addr'] for s in self.text)
        self.hi = max(s['addr'] + s['size'] for s in self.text)
        self.img_lo = e.base
        self.img_hi = max(s['addr'] + s['size'] for s in e.allocated())
        self.exports = {}                     # addr -> name (functions)
        self.dataexp = {}                     # addr -> (name, size)
        for s in e.dynsyms:
            if not s['name'] or not s['shndx']:
                continue
            if s['type'] == 2:
                self.exports.setdefault(s['value'], s['name'])
            elif s['type'] in (0, 1) and not self.in_text(s['value']):
                self.dataexp.setdefault(s['value'], (s['name'], s['size']))
        self.gpwidth = defaultdict(set)       # small-data address -> access widths seen in the code
        self.rel = [(o, sym, typ) for o, sym, typ in e.rel_dyn() if typ == R_MIPS_REL32]
        self.relmap = {o: sym for o, sym, _ in self.rel}
        self.is_exec = int.from_bytes(e.data[16:18], 'big') == 2
        self.find_functions()

    def in_text(self, a):
        return any(s['addr'] <= a < s['addr'] + s['size'] for s in self.text)

    def text_sec(self, a):
        for s in self.text:
            if s['addr'] <= a < s['addr'] + s['size']:
                return s
        return None

    def find_functions(self):
        e = self.elf
        exp = sorted((s['value'], s['size']) for s in e.dynsyms
                     if s['type'] == 2 and s['shndx'] and self.in_text(s['value']))
        inexp = lambda a: any(v <= a < v + z for v, z in exp if z)
        starts = {v for v, _ in exp}
        for a, _ in e.frame_functions():
            if self.in_text(a) and not inexp(a):
                starts.add(a)
        for k in range(e.local_gotno):
            v = e.word(e.pltgot + 4 * k)
            if self.in_text(v) and not inexp(v):
                starts.add(v)
        for tag in (12, 13):
            if e.dyn.get(tag):
                starts.add(e.dyn[tag])
        # text rearranged after linking: every moved range is one procedure
        for new, _, _ in e.xlate_table():
            if self.in_text(new):
                starts.add(new)
        if self.is_exec:
            # functions without an unwind record: a `jr ra` and its delay slot followed by a frame setup (addiu sp,sp,-N)
            # or a gp load (lui $gp) starts a function, unless it lies inside a function that has its own record
            extra = set()
            for sec in self.text:
                for a in range(sec['addr'] + 8, sec['addr'] + sec['size'], 4):
                    if a in starts or e.word(a - 8) != 0x03e00008:
                        continue
                    w = e.word(a)
                    if (w >> 16) == 0x27bd and (w & 0x8000) or (w >> 16) == 0x3c1c:
                        if not inexp(a):                 # the unwind record of a neighbour may span several functions
                            extra.add(a)
            sys.stderr.write('executable: %d function starts found by prologue\n' % len(extra))
            starts |= extra
        self.find_tables()
        base = sorted(starts)
        import bisect
        def owner(a):
            return base[bisect.bisect_right(base, a) - 1] if a >= base[0] else None
        framed = [(a, a + z) for a, z in e.frame_functions() if z]       # functions that have their own unwind record
        interior = lambda t: any(a < t < b for a, b in framed)
        newruns = []
        for run in self.runs:
            # adjacent jump tables of different functions form one run: split it where the owner changes
            subs, cur = [], [run[0]]
            for item in run[1:]:
                if owner(item[1]) == owner(cur[-1][1]):
                    cur.append(item)
                else:
                    subs.append(cur)
                    cur = [item]
            subs.append(cur)
            for sub in subs:
                tg = [t for _, t in sub]
                newruns.append(sub)
                if len(tg) >= 2 and not any(t in starts for t in tg):
                    continue                   # jump table (all targets inside one function)
                for t in tg:
                    if not inexp(t) and not interior(t):
                        starts.add(t)
        self.runs = newruns
        self.run_of = {}
        for run in self.runs:
            for o, _ in run:
                self.run_of[o] = run
        for tag in (12, 13):                  # DT_INIT, DT_FINI
            if e.dyn.get(tag):
                starts.add(e.dyn[tag])
        def in_delay_slot(a):
            # the word before `a` is a branch or jump: `a` is its delay slot, which no function can start at
            if not self.in_text(a - 4):
                return False
            w = e.word(a - 4)
            op = w >> 26
            return (op in (2, 3, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or op == 1 or
                    (op == 0 and (w & 0x3f) in (8, 9)) or (op == 0x11 and ((w >> 21) & 31) == 8))
        if self.is_exec:
            dropped = {a for a in starts if in_delay_slot(a) and a not in dict.fromkeys(v for v, _ in exp)}
            sys.stderr.write('executable: %d function starts dropped (inside a delay slot)\n' % len(dropped))
            starts -= dropped
            starts = self.merge_split_functions(starts, exp)
        table = {new for new, _, _ in e.xlate_table() if self.in_text(new)}
        if table:
            # the table lists every procedure: a GOT page entry or a data word pointing into one is not a start
            keep = table | {v for v, _ in exp} | {e.dyn[t] for t in (12, 13) if e.dyn.get(t)}
            starts = {a for a in starts if a in keep or not any(n <= a < n + z for n, _, z in e.xlate_table())}
        starts = sorted(starts)
        self.funcs = []                       # (addr, size, cname, section)
        for k, a in enumerate(starts):
            sec = self.text_sec(a)
            end = sec['addr'] + sec['size']
            nxt = starts[k + 1] if k + 1 < len(starts) else end
            nxt = min(nxt, end)
            self.funcs.append((a, nxt - a, 'f_%08x' % a, sec))
        self.fstart = {a: n for a, _, n, _ in self.funcs}
        self.franges = [(a, a + z) for a, z, _, _ in self.funcs]

    def merge_split_functions(self, starts, exp):
        """A function whose code the compiler split into several pieces (separate unwind records) has branches from one
        piece into the interior of another.  Merge such pieces into one function, unless a later piece is also entered
        from outside (a call, a data pointer, an export), in which case merging would break that entry."""
        e = self.elf
        import bisect
        order = sorted(starts)
        def owner_idx(a):
            k = bisect.bisect_right(order, a) - 1
            return k if k >= 0 else None
        entered = {v for v, _ in exp}               # entry points referenced from outside the function that holds them
        for tag in (12, 13):
            if e.dyn.get(tag):
                entered.add(e.dyn[tag])
        for k in range(e.local_gotno):
            entered.add(e.word(e.pltgot + 4 * k))
        for sec in e.allocated():
            if sec['name'] in ('.text', '.init', '.fini') or sec['type'] == 8:
                continue
            for a in range(sec['addr'], sec['addr'] + sec['size'] - 3, 4):
                v = e.word(a)
                if self.in_text(v):
                    entered.add(v)
        pairs = []
        for sec in self.text:
            for a in range(sec['addr'], sec['addr'] + sec['size'] - 3, 4):
                w = e.word(a)
                op = w >> 26
                tgt = None
                if op == 3:
                    entered.add(((a + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2))
                    continue
                if op == 2:
                    tgt = ((a + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2)
                elif op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 1 and ((w >> 16) & 31) not in (0x10, 0x11, 0x12, 0x13)) \
                        or (op == 0x11 and ((w >> 21) & 31) == 8):
                    imm = w & 0xffff
                    if imm & 0x8000:
                        imm -= 0x10000
                    tgt = a + 4 + (imm << 2)
                if tgt is None or not self.in_text(tgt):
                    continue
                ia, ib = owner_idx(a), owner_idx(tgt)
                if ia is None or ib is None or ia == ib or tgt == order[ib]:
                    continue
                pairs.append((min(ia, ib), max(ia, ib)))
        merged = set()
        for lo, hi in sorted(set(pairs)):
            inner = order[lo + 1:hi + 1]
            if any(x in entered for x in inner):
                continue
            merged.update(inner)
        sys.stderr.write('executable: %d function starts merged into the function before them\n' % len(merged))
        return starts - merged

    def func_containing(self, a):
        for fa, z, n, s in self.funcs:
            if fa <= a < fa + z:
                return fa
        return None

    def find_tables(self):
        """Runs of consecutive REL32 words whose targets are text: start address -> list of target addresses."""
        e = self.elf
        words = sorted((o, e.word(o)) for o, _, _ in self.rel if self.in_text(e.word(o)))
        if self.is_exec:
            # no relocation records in an executable: look for words holding text addresses in the data sections
            seen = {o for o, _ in words}
            for sec in e.allocated():
                if sec['name'] in ('.text', '.init', '.fini') or sec['type'] == 8:
                    continue
                for a in range(sec['addr'], sec['addr'] + sec['size'] - 3, 4):
                    if a not in seen and self.in_text(e.word(a)):
                        words.append((a, e.word(a)))
            words.sort()
        self.runs = []
        cur = []
        for o, v in words:
            if cur and o == cur[-1][0] + 4:
                cur.append((o, v))
            else:
                if cur:
                    self.runs.append(cur)
                cur = [(o, v)]
        if cur:
            self.runs.append(cur)
        self.run_of = {}
        for run in self.runs:
            for o, _ in run:
                self.run_of[o] = run

    # -- addresses as C expressions ----------------------------------------------------------
    def addr_c(self, syms, a, page=False):
        if a in self.fstart:
            return '(%s + 0)' % syms.addr(self.fstart[a])
        if self.in_text(a) and a & 0xffff:
            raise Unsupported('address %08x is inside a function' % a)
        # a GOT page entry is an address near its target and may lie just past either end of the image
        if self.img_lo <= a < self.img_hi or (page and self.img_lo - 0x10000 <= a < self.img_hi + 0x10000):
            return '(%s + %d)' % (syms.addr('__img_' + self.name), a - self.img_lo)
        return '0x%08xu' % a

    def addr_s(self, a):
        if a in self.fstart:
            return self.fstart[a]
        if self.in_text(a) and a & 0xffff:
            return '0 /* %08x inside a function */' % a
        if self.img_lo <= a < self.img_hi:
            return '__img_%s + %d' % (self.name, a - self.img_lo)
        return '%d' % a


class DsoFuncLifter(LL.FuncLifter):
    def __init__(self, img, info, sec, addr, size, syms):
        self.img = img
        self.addr0 = addr
        self.sec_addr = sec['addr']
        super().__init__(info, sec['no'], addr - sec['addr'], size, img.fstart[addr], syms, {})
        self.const = {}
        self.tblreg = {}
        self.kreg = {}          # registers holding a known constant from lui/addiu
        self.gpreg = set()      # registers equal to $gp
        for j in self.insns:
            if j.op == 0x27:
                j.op = 0x23
            if j.op in (0x37, 0x3f) and j.rt == 29:
                raise Unsupported('64-bit sp save at %08x' % j.addr)

    # arguments to FuncLifter.lift(): jal allowed here
    allow_jal = True

    def jump_target(self, i):
        # j/jal targets keep the top four bits of the (absolute) address of the delay slot; ours are section offsets
        return ((((self.gaddr(i.addr) + 4) & 0xf0000000) | (i.target << 2)) - self.sec_addr) & 0xffffffff

    def is_tail_target(self, t):
        return not (self.start <= t < self.end) and self.gaddr(t) in self.img.fstart

    def tail_stmt(self, t):
        if not self.is_tail_target(t):
            raise Unsupported('branch leaves the function at %08x' % t)
        return '{ %sreturn ((uint32_t (*)())(uintptr_t)%s)(%s); }' % (
            self.fp_pre(), self.img.addr_c(self.syms, self.gaddr(t)), self.tail_args())

    def gaddr(self, off):
        return self.sec_addr + off

    def stmt(self, i):
        e = self.img.elf
        op = i.op
        if i.w == 0:
            return None
        # $gp copies: the linker's gp-disp pair `lui rA,H ; addiu rA,rA,L ; addu rB,$t9,rA` (rB need not be $gp), and the
        # large-GOT form `lui rX,0 ; addu rX,rX,$gp ; lw rY,off(rX)`.  A register known to equal $gp is used as $gp.
        if i.rs in self.gpreg and i.rs != 28 and op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2b,
                                                       0x09, 0x19, 0x31, 0x35, 0x39, 0x3d):
            i.rs = 28
        d0 = self.dest(i)
        newk, newgp = None, False
        if op == 0x0f:
            newk = ((i.imm & 0xffff) << 16) & 0xffffffff
        elif op == 0x09 and i.rs in self.kreg and d0 is not None:
            newk = (self.kreg[i.rs] + i.imm) & 0xffffffff
        elif op == 0 and i.fn in (0x21, 0x2d) and d0 is not None:
            for ra, rb in ((i.rs, i.rt), (i.rt, i.rs)):
                if rb in self.kreg:
                    k = self.kreg[rb]
                    if (ra == 28 and k == 0) or (ra == 25 and ((self.addr0 + k) & 0xffffffff) == (e.gp & 0xffffffff)):
                        newgp = True
        if d0 is not None:
            self.kreg.pop(d0, None)
            self.gpreg.discard(d0)
            if newk is not None:
                self.kreg[d0] = newk
            if newgp:
                self.gpreg.add(d0)
        # address of a gp-relative object (small data): addiu rt,$gp,imm
        if op in (0x09, 0x19) and i.rs == 28 and i.rt not in (0, 28):
            gpa0 = (e.gp + i.imm) & 0xffffffff
            self.const[i.rt] = gpa0
            return '%s = %s;' % (lhs(i.rt), self.img.addr_c(self.syms, gpa0))
        # GOT load
        if op == 0x27 and i.rs != 28:
            i.op = op = 0x23
        gpa = (e.gp + i.imm) & 0xffffffff
        if i.rs == 28 and op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b) and not (
                op == 0x23 and e.pltgot <= gpa < e.pltgot + 4 * (e.local_gotno + len(e.dynsyms) - e.gotsym)):
            T = {0x20: 'int8_t', 0x21: 'int16_t', 0x23: 'uint32_t', 0x24: 'uint8_t', 0x25: 'uint16_t',
                 0x28: 'uint8_t', 0x29: 'uint16_t', 0x2b: 'uint32_t'}[op]
            # the width this small-data object is accessed with decides how its initial bytes are laid out
            self.img.gpwidth[gpa].add({0x20: 1, 0x24: 1, 0x28: 1, 0x21: 2, 0x25: 2, 0x29: 2, 0x23: 4, 0x2b: 4}[op])
            ref = '*(%s *)%s' % (T, self.img.addr_c(self.syms, gpa))
            if op < 0x28:
                return '%s = (uint32_t)(%s)%s;' % (lhs(i.rt), 'int32_t' if op in (0x20, 0x21) else 'uint32_t', ref) if i.rt else None
            return '%s = (%s)%s;' % (ref, T, R(i.rt))
        if op == 0x23 and i.rs == 28 and i.rt != 28:
            idx = (i.imm + 0x7ff0) // 4
            kind, v = e.got_entry(idx)
            if kind == 'local':
                self.const[i.rt] = v
                self.note_tbl(i.rt, v)
                return '%s = %s;' % (lhs(i.rt), self.img.addr_c(self.syms, v, page=True))
            name = v['name']
            self.const.pop(i.rt, None)
            if v['shndx']:
                a = v['value']
                if v['type'] == 2 and a in self.img.fstart:
                    return '%s = %s;' % (lhs(i.rt), self.img.addr_c(self.syms, a))
                self.const[i.rt] = a
                return '%s = %s;' % (lhs(i.rt), self.img.addr_c(self.syms, a))
            return '%s = %s;' % (lhs(i.rt), self.syms.addr(name or ('__undef%d' % v['no'])))
        # constant folding bookkeeping for jump-table detection
        d = self.dest(i)
        st = None
        if op == 0x0f and ((i.imm & 0xffff) << 16) >= (self.img.img_lo & 0xffff0000) and ((i.imm & 0xffff) << 16) < self.img.img_hi:
            self.const[i.rt] = (i.imm & 0xffff) << 16          # upper half of an image address (non-PIC code)
        if op in (0x09, 0x0d) and i.rs in self.const and i.rs != 29 and i.rt:
            newv = (self.const[i.rs] + i.imm) & 0xffffffff if op == 0x09 else (self.const[i.rs] | (i.imm & 0xffff))
            if newv in self.img.fstart:
                # page entry + low half forming a function address: use the lifted function, not an image offset
                self.const[i.rt] = newv
                return '%s = %s;' % (lhs(i.rt), self.img.addr_c(self.syms, newv))
        if op == 0x09 and i.rs in self.const and i.rs != 29:
            self.const[i.rt] = (self.const[i.rs] + i.imm) & 0xffffffff
            self.note_tbl(i.rt, self.const[i.rt])
        elif op == 0 and i.fn in (0x21, 0x2d) and i.w and ((i.rt == 0 and i.rs in self.const) or (i.rs == 0 and i.rt in self.const)):
            self.const[i.rd] = self.const[i.rs if i.rt == 0 else i.rt]
        elif d is not None and d in self.const:
            self.const.pop(d, None)
        # table registers: addu keeps the base, lw becomes a label-array read
        if op == 0 and i.fn in (0x21, 0x2d) and i.w:
            a, b = i.rs, i.rt
            if a in self.tblreg and b not in self.tblreg:
                self.tblreg[i.rd] = self.tblreg[a]
            elif b in self.tblreg and a not in self.tblreg:
                self.tblreg[i.rd] = self.tblreg[b]
            else:
                self.tblreg.pop(i.rd, None)
        elif d is not None and d in self.tblreg and op != 0x09:
            self.tblreg.pop(d, None)
        if op == 0x23 and i.rs in self.tblreg and i.rs != 28:
            return self.jt_load_dso(i)
        if op in (0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2b, 0x23, 0x37, 0x3f) and i.rs == 28:
            raise Unsupported('gp-relative access %s at %08x' % (hex(op), i.addr))
        st = super().stmt(i)
        if st and i.rs == 28 and op in (0x31, 0x35, 0x39, 0x3d):         # lwc1/ldc1/swc1/sdc1 off($gp): small-data floats
            gpa1 = (e.gp + i.imm) & 0xffffffff
            old = '(r28 + %d)' % i.imm
            if old not in st:
                raise Unsupported('gp-relative fp access at %08x' % i.addr)
            st = st.replace(old, '(%s)' % self.img.addr_c(self.syms, gpa1))
        return st

    def note_tbl(self, reg, v):
        run = self.img.run_of.get(v)
        if run is None:
            self.tblreg.pop(reg, None)
            return
        fa = self.addr0
        inside = [t for _, t in run if fa <= t < fa + self.size]
        if len(inside) >= 2 and len(inside) == len(run) or (inside and run[0][1] in inside):
            self.tblreg[reg] = run[0][0]
        else:
            self.tblreg.pop(reg, None)

    def jt_load_dso(self, i):
        start = self.tblreg[i.rs]
        run = self.img.run_of[start]
        fa = self.addr0
        labels = []
        for _, t in run:
            if not (fa <= t < fa + self.size):
                break
            off = t - self.sec_addr
            labels.append('&&L_%08x' % off)
            self.jt_labels.add(off)
        n = len(self.jt_arrays)
        self.jt_arrays.append('static const void *const jt%d[] = { %s };' % (n, ', '.join(labels)))
        return '%s = (uint32_t)(uintptr_t)jt%d[(((uint32_t)(%s + %d) - %s) >> 2)]; jr_table = 1;' % (
            lhs(i.rt), n, R(i.rs), i.imm, self.img.addr_c(self.syms, start))

    def control(self, i, d, body, targets, insns, k):
        if i.op == 1 and i.rs == 0 and i.rt in (0x11, 0x13):     # bal / balll: direct call
            tgt = self.gaddr(i.addr) + 4 + (i.imm << 2)
            s = self.safe_stmt(d)
            if s:
                body.append('    ' + s)
            body.append('    %sr2 = ((uint32_t (*)())(uintptr_t)%s)(%s);%s' % (self.fp_pre(), self.img.addr_c(self.syms, tgt), self.call_args(), self.fp_post()))
            return
        if i.op == 0x03:                       # jal: direct call inside the image
            tgt = ((i.addr + 4 + self.sec_addr * 0) & 0xf0000000) | (i.target << 2)
            tgt = (((self.gaddr(i.addr) + 4) & 0xf0000000) | (i.target << 2))
            s = self.safe_stmt(d)
            if s:
                body.append('    ' + s)
            body.append('    %sr2 = ((uint32_t (*)())(uintptr_t)%s)(%s);%s' % (self.fp_pre(), self.img.addr_c(self.syms, tgt), self.call_args(), self.fp_post()))
            return
        return super().control(i, d, body, targets, insns, k)


def swap_image(img):
    """Big-endian -> native little-endian words for the data sections (default word swap with protections)."""
    e = img.elf
    out = {}
    for s in e.allocated():
        if s['name'] in ('.text', '.init', '.fini') or s['type'] == 8:
            continue
        b = bytearray(e.data[s['off']:s['off'] + s['size']])
        n = len(b)
        name = s['name']
        unit = 8 if name == '.lit8' else 4
        strs = nm_swap.string_mask(b, aligned_start=True) if not (s['flags'] & 1) or name in ('.data', '.rodata') else bytearray(n)
        i = 0
        while i + unit <= n:
            if not any(strs[i:i + unit]):
                a = s['addr'] + i
                w = [img.gpwidth.get(a + k, ()) for k in range(unit)] if unit == 4 else []
                if unit == 4 and any(w) and 4 not in w[0]:
                    # accessed at byte or half-word width: those stay in place (swap per half-word, none per byte)
                    k = 0
                    while k < 4:
                        if 2 in w[k] and k % 2 == 0:
                            b[i + k:i + k + 2] = b[i + k:i + k + 2][::-1]
                            k += 2
                        else:
                            k += 1
                else:
                    b[i:i + unit] = b[i:i + unit][::-1]
            i += unit
        out[s['no']] = b
    return out


def emit_blob(img, swapped, w):
    e = img.elf
    base, hi = img.img_lo, img.img_hi
    image = bytearray(hi - base)
    isbss = bytearray(hi - base)
    for s in e.allocated():
        off = s['addr'] - base
        if s['name'] in ('.text', '.init', '.fini'):
            continue
        if s['type'] == 8:
            continue
        image[off:off + s['size']] = swapped[s['no']]
    relat = {o: sym for o, sym in img.relmap.items()}
    # An executable has no relocation records for its data pointers (it was linked at a fixed address): any aligned
    # word that holds an address inside the image is taken for a pointer and re-pointed at the lifted label.
    fixup = set()
    if int.from_bytes(e.data[16:18], 'big') == 2:
        for s in e.allocated():
            if s['name'] in ('.text', '.init', '.fini') or s['type'] == 8:
                continue
            for a in range(s['addr'], s['addr'] + s['size'] - 3, 4):
                if a in relat or a % 4:
                    continue
                v = int.from_bytes(image[a - base:a - base + 4], 'little')
                if base <= v < hi:
                    fixup.add(a)
        sys.stderr.write('executable: %d data words re-pointed at image labels\n' % len(fixup))
    w.write('\t.data\n\t.balign 4096\n\t.globl __img_%s\n\t.hidden __img_%s\n__img_%s:\n' % (img.name, img.name, img.name))
    off = 0
    total = hi - base
    while off < total:
        a = base + off
        if a in relat and off + 4 <= total:
            v = e.word(a)
            sym = e.dynsyms[relat[a]] if relat[a] < len(e.dynsyms) else None
            if sym is not None and relat[a] >= e.gotsym and not sym['shndx'] and sym['name']:
                # a quickstarted object holds the symbol's expected address in the word: the addend is what is left
                # (the expected address is the symbol's quickstart GOT entry)
                exp = e.word(e.pltgot + 4 * (e.local_gotno + relat[a] - e.gotsym))
                add = (v - exp) & 0xffffffff
                if add & 0x80000000:
                    add -= 1 << 32
                w.write('\t.long %s + %d\n' % (sym['name'], add))
            else:
                w.write('\t.long %s\n' % img.addr_s(v))
            off += 4
            continue
        if a in fixup and off + 4 <= total:
            w.write('\t.long %s\n' % img.addr_s(int.from_bytes(image[off:off + 4], 'little')))
            off += 4
            continue
        chunk = []
        while off + len(chunk) < total and (base + off + len(chunk)) not in relat and (base + off + len(chunk)) not in fixup and len(chunk) < 16:
            chunk.append(image[off + len(chunk)])
        w.write('\t.byte %s\n' % ','.join(str(x) for x in chunk))
        off += len(chunk)
    # exported symbols
    for s in e.dynsyms:
        if not s['name'] or not s['shndx'] or s['name'].startswith('.'):
            continue
        if s['type'] == 2 and s['value'] in img.fstart:
            continue                      # exported functions are aliases in code.c, next to their definitions
        w.write('\t.globl %s\n' % s['name'])
        if img.in_text(s['value']):
            w.write('\t.set %s, %s\n' % (s['name'], img.addr_s(s['value'])))
        else:
            w.write('\t.set %s, __img_%s + %d\n\t.type %s,@object\n' % (s['name'], img.name, s['value'] - base, s['name']))
            if s['size']:
                w.write('\t.size %s,%d\n' % (s['name'], s['size']))


def main():
    args = sys.argv[1:]
    path, out = args[0], Path(args[1])
    name = args[args.index('--name') + 1] if '--name' in args else re.sub(r'[^A-Za-z0-9]', '_', Path(path).name)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'lift_rt.h').write_text((here / 'lift_rt.h').read_text())
    img = Image(path, name)
    info = DsoInfo(img.elf, name)
    syms = LL.Syms()
    report = Counter()
    failed = []
    bodies = []
    for addr, size, cname, sec in img.funcs:
        try:
            fl = DsoFuncLifter(img, info, sec, addr, size, syms)
            bodies.append(fl.lift())
            report['lifted'] += 1
        except Unsupported as ex:
            report['unsupported'] += 1
            failed.append((cname, str(ex)))
            bodies.append('__attribute__((noinline)) uint32_t %s() { extern void abort(void); abort(); return 0; } /* %s */' % (cname, str(ex).replace('*/', '')))
        except Exception as ex:      # noqa
            report['error'] += 1
            failed.append((cname, 'ERROR %r' % ex))
            bodies.append('__attribute__((noinline)) uint32_t %s() { extern void abort(void); abort(); return 0; }' % cname)
    with open(out / 'code.c', 'w') as w:
        w.write('#include "lift_rt.h"\n')
        w.write('uint64_t lift_fpa[8] __attribute__((weak)); uint64_t lift_fpr __attribute__((weak));\n')
        w.write(syms.decls())
        for c, (a, z, cn, s) in zip(bodies, img.funcs):
            w.write(c + '\n\n')
        # exported functions: the dynamic symbol is an alias of the lifted function
        for sym in img.elf.dynsyms:
            if sym['name'] and sym['shndx'] and sym['type'] == 2 and sym['value'] in img.fstart and not sym['name'].startswith('.'):
                w.write('extern uint32_t %s() __attribute__((alias("%s")));\n' % (sym['name'], img.fstart[sym['value']]))
        # DT_INIT / DT_FINI
        ini = img.elf.dyn.get(12)
        if ini in img.fstart:
            w.write('extern uint32_t %s();\nstatic void __attribute__((constructor)) lift_init_%s(void) { %s(); }\n' % (img.fstart[ini], name, img.fstart[ini]))
        fin = img.elf.dyn.get(13)
        if fin in img.fstart:
            w.write('static void __attribute__((destructor)) lift_fini_%s(void) { %s(); }\n' % (name, img.fstart[fin]))
    swapped = swap_image(img)
    with open(out / 'data.s', 'w') as w:
        emit_blob(img, swapped, w)
    # undefined dynamic symbols (for the link line)
    undef = sorted({s['name'] for s in img.elf.dynsyms if s['name'] and not s['shndx']})
    (out / 'needed.json').write_text(json.dumps({'needed': img.elf.needed, 'undef': undef, 'failed': failed}, indent=1))
    print('functions=%d lifted=%d unsupported=%d error=%d  needed=%s' % (len(img.funcs), report['lifted'], report['unsupported'], report['error'], img.elf.needed))
    reasons = Counter(re.sub(r'[0-9a-f]{8}', '', f[1]) for f in failed)
    for r, n in reasons.most_common(12):
        print('  %5d  %s' % (n, r))


if __name__ == '__main__':
    main()
