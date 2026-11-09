#!/usr/bin/env python3
"""Lift N32 MIPS functions to native C: registers become locals, no CPU struct.

A function becomes a plain cdecl C function.  Its parameters come from liveness: the argument
registers a0..a7 it reads before writing, then the incoming 8-byte stack slots it reads (each
a 32-bit word at slot+4, as N32 passes ints).  The frame is a local byte array.  Memory is native
(little-endian) and addresses are real pointers.

This module is the instruction-level core.  `lift(words, start, name, ...)` takes the raw
big-endian instruction words of one function and returns C source plus its signature.
Milestone 1: integer leaf functions (no calls, no jump tables, no FP).
"""
import re
import sys

REGN = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 't4', 't5', 't6', 't7',
        's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
ARGREGS = list(range(4, 12))


class Unsupported(Exception):
    pass


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


class Insn:
    __slots__ = ('addr', 'w', 'op', 'rs', 'rt', 'rd', 'sa', 'fn', 'imm', 'immu', 'target')

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
        self.target = w & 0x3ffffff


# ---- classification -------------------------------------------------------------------------

BRANCH_OPS = {0x04: 'beq', 0x05: 'bne', 0x06: 'blez', 0x07: 'bgtz', 0x14: 'beq', 0x15: 'bne',
              0x16: 'blez', 0x17: 'bgtz'}
LIKELY_OPS = {0x14, 0x15, 0x16, 0x17}


def is_branch(i):
    return i.op in BRANCH_OPS or (i.op == 0x01 and i.rt in (0, 1, 2, 3)) or (i.op == 0x11 and i.rs == 8)


def branch_kind(i):
    if i.op == 0x11:
        return 'bc1t' if i.rt & 1 else 'bc1f'
    if i.op == 0x01:
        return {0: 'bltz', 1: 'bgez', 2: 'bltz', 3: 'bgez'}[i.rt]
    return BRANCH_OPS[i.op]


def is_likely(i):
    return i.op in LIKELY_OPS or (i.op == 0x01 and i.rt in (2, 3)) or (i.op == 0x11 and i.rs == 8 and bool(i.rt & 2))


def is_control(i):
    if i.op == 0 and i.fn in (0x08, 0x09):
        return True
    if i.op in (0x02, 0x03) or is_branch(i):
        return True
    if i.op == 0x01:
        return True
    return False


def branch_target(i):
    return (i.addr + 4 + (i.imm << 2)) & 0xffffffff


def reads_writes(i):
    """(reads, writes) register-number sets of one non-control instruction."""
    op, rs, rt, rd, fn = i.op, i.rs, i.rt, i.rd, i.fn
    if i.w == 0:
        return set(), set()
    if op == 0:
        if fn == 0x01 and rd == 0:        # movf/movt into $zero: 0x00000001 padding words after a function
            return set(), set()
        if fn in (0x1c, 0x1d, 0x1e, 0x1f):  # dmult, dmultu, ddiv, ddivu
            return {rs, rt}, set()
        if fn in (0x00, 0x02, 0x03, 0x38, 0x3a, 0x3b, 0x3c, 0x3e, 0x3f):
            return {rt}, {rd}
        if fn in (0x04, 0x06, 0x07, 0x14, 0x16, 0x17):
            return {rs, rt}, {rd}
        if fn in (0x10, 0x12):
            return set(), {rd}
        if fn in (0x18, 0x19, 0x1a, 0x1b):
            return {rs, rt}, set()
        if fn in (0x21, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2b, 0x2d, 0x2f):
            return {rs, rt}, {rd}
        if fn in (0x0a, 0x0b):
            return {rs, rt, rd}, {rd}
        if fn in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36):
            return {rs, rt}, set()
        raise Unsupported('SPECIAL fn 0x%x at %08x' % (fn, i.addr))
    if op in (0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x19):
        return {rs}, {rt}
    if op == 0x0f:
        return set(), {rt}
    if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x37):
        return {rs}, {rt}
    if op in (0x28, 0x29, 0x2b, 0x3f):
        return {rs, rt}, set()
    raise Unsupported('op 0x%x at %08x' % (op, i.addr))


# ---- expression helpers ---------------------------------------------------------------------

def R(n):
    return '0' if n == 0 else 'r%d' % n


def lhs(n):
    return 'r%d' % n


def ea(i):
    return '(uintptr_t)(uint32_t)(%s + %d)' % (R(i.rs), i.imm) if i.rs else '(uintptr_t)%d' % (i.imm & 0xffffffff)


def alu(i):
    """C statement for one non-memory non-control instruction, or None for a nop."""
    op, rs, rt, rd, sa, fn = i.op, i.rs, i.rt, i.rd, i.sa, i.fn
    if i.w == 0:
        return None
    if op == 0:
        if fn == 0x01 and rd == 0:
            return None
        d = lhs(rd)
        t = {
            0x00: '%s = %s << %d;' % (d, R(rt), sa),
            0x02: '%s = %s >> %d;' % (d, R(rt), sa),
            0x03: '%s = (uint32_t)((int32_t)%s >> %d);' % (d, R(rt), sa),
            0x04: '%s = %s << (%s & 31u);' % (d, R(rt), R(rs)),
            0x06: '%s = %s >> (%s & 31u);' % (d, R(rt), R(rs)),
            0x07: '%s = (uint32_t)((int32_t)%s >> (%s & 31u));' % (d, R(rt), R(rs)),
            0x21: '%s = %s + %s;' % (d, R(rs), R(rt)),
            0x23: '%s = %s - %s;' % (d, R(rs), R(rt)),
            0x24: '%s = %s & %s;' % (d, R(rs), R(rt)),
            0x25: '%s = %s | %s;' % (d, R(rs), R(rt)),
            0x26: '%s = %s ^ %s;' % (d, R(rs), R(rt)),
            0x27: '%s = ~(%s | %s);' % (d, R(rs), R(rt)),
            0x2a: '%s = (int32_t)%s < (int32_t)%s;' % (d, R(rs), R(rt)),
            0x2b: '%s = %s < %s;' % (d, R(rs), R(rt)),
            0x2d: '%s = %s + %s;' % (d, R(rs), R(rt)),   # daddu on sign-extended 32-bit values
            0x2f: '%s = %s - %s;' % (d, R(rs), R(rt)),
            0x0a: 'if (%s == 0) %s = %s;' % (R(rt), d, R(rs)),
            0x0b: 'if (%s != 0) %s = %s;' % (R(rt), d, R(rs)),
            0x12: '%s = lo;' % d,
            0x10: '%s = hi;' % d,
            0x18: '{ int64_t p = (int64_t)(int32_t)%s * (int64_t)(int32_t)%s; lo = (uint32_t)p; hi = (uint32_t)((uint64_t)p >> 32); }' % (R(rs), R(rt)),
            0x19: '{ uint64_t p = (uint64_t)%s * (uint64_t)%s; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }' % (R(rs), R(rt)),
            0x1a: 'if (%s) { lo = (uint32_t)((int32_t)%s / (int32_t)%s); hi = (uint32_t)((int32_t)%s %% (int32_t)%s); }' % (R(rt), R(rs), R(rt), R(rs), R(rt)),
            0x1b: 'if (%s) { lo = %s / %s; hi = %s %% %s; }' % (R(rt), R(rs), R(rt), R(rs), R(rt)),
        }
        if fn in t:
            return t[fn]
        raise Unsupported('SPECIAL fn 0x%x at %08x' % (fn, i.addr))
    d = lhs(rt)
    if op in (0x08, 0x09):
        return '%s = %s + %d;' % (d, R(rs), i.imm)
    if op == 0x19:   # daddiu
        return '%s = %s + %d;' % (d, R(rs), i.imm)
    if op == 0x0a:
        return '%s = (int32_t)%s < %d;' % (d, R(rs), i.imm)
    if op == 0x0b:
        return '%s = %s < (uint32_t)%d;' % (d, R(rs), i.imm)
    if op == 0x0c:
        return '%s = %s & 0x%xu;' % (d, R(rs), i.immu)
    if op == 0x0d:
        return '%s = %s | 0x%xu;' % (d, R(rs), i.immu)
    if op == 0x0e:
        return '%s = %s ^ 0x%xu;' % (d, R(rs), i.immu)
    if op == 0x0f:
        return '%s = 0x%08xu;' % (d, (i.immu << 16) & 0xffffffff)
    raise Unsupported('op 0x%x at %08x' % (op, i.addr))


def mem(i, frame_size):
    """Statement for a load or store; stack-relative accesses use the frame or an incoming slot."""
    op = i.op
    names = {0x20: ('int8_t', 'ld'), 0x24: ('uint8_t', 'ld'), 0x21: ('int16_t', 'ld'), 0x25: ('uint16_t', 'ld'),
             0x23: ('uint32_t', 'ld'), 0x28: ('uint8_t', 'st'), 0x29: ('uint16_t', 'st'), 0x2b: ('uint32_t', 'st')}
    if op not in names:
        raise Unsupported('memory op 0x%x at %08x' % (op, i.addr))
    ctype, kind = names[op]
    if i.rs == 29:
        if i.imm < frame_size:
            addr = '(frame + %d)' % i.imm
        else:
            return incoming(i, frame_size, ctype, kind)
    else:
        addr = '(%s)' % ea(i)
    if kind == 'ld':
        return '%s = (uint32_t)(int32_t)*(%s *)%s;' % (lhs(i.rt), ctype, addr) if ctype.startswith('int') else \
               '%s = *(%s *)%s;' % (lhs(i.rt), ctype, addr)
    return '*(%s *)%s = (%s)%s;' % (ctype, addr, ctype, R(i.rt))


def incoming(i, frame_size, ctype, kind):
    off = i.imm - frame_size
    slot, within = off // 8, off % 8
    if within < 4:
        raise Unsupported('access to the high half of incoming slot at %08x' % i.addr)
    if kind == 'st':
        # the callee owns its incoming slots: a word store replaces the argument
        if ctype == 'uint32_t' and within == 4:
            return 's%d = %s;' % (slot, R(i.rt))
        raise Unsupported('store to an incoming argument slot at %08x' % i.addr)
    # little-endian word at slot+4: a 32-bit arg; narrow reads take the low bytes of that word
    if ctype == 'uint32_t':
        if within != 4:
            raise Unsupported('misaligned incoming word at %08x' % i.addr)
        return '%s = s%d;' % (lhs(i.rt), slot)
    width = {'int8_t': 1, 'uint8_t': 1, 'int16_t': 2, 'uint16_t': 2}[ctype]
    big_end_off = 8 - width
    if within != 4 and within != big_end_off:
        # the BE lane of a narrow arg is the last bytes of the slot; accept either spelling
        raise Unsupported('odd narrow incoming access at %08x' % i.addr)
    cast = '(%s)' % ctype
    return '%s = (uint32_t)(int32_t)%ss%d;' % (lhs(i.rt), cast, slot) if ctype.startswith('int') else \
           '%s = %ss%d;' % (lhs(i.rt), cast, slot)


# ---- the function lifter --------------------------------------------------------------------

def lift(words, start, name):
    insns = [Insn(start + 4 * k, w) for k, w in enumerate(words)]
    byaddr = {i.addr: i for i in insns}
    end = start + 4 * len(insns)

    # frame size from the leading addiu sp,sp,-N
    frame = 0
    for i in insns[:6]:
        if i.op == 0x09 and i.rs == 29 and i.rt == 29 and i.imm < 0:
            frame = -i.imm
            break

    # delay-slot membership and branch targets
    delay_of = {}
    targets = set()
    for k, i in enumerate(insns):
        if is_control(i):
            if k + 1 >= len(insns):
                raise Unsupported('control transfer without a delay slot at %08x' % i.addr)
            delay_of[i.addr] = insns[k + 1]
            if is_branch(i):
                targets.add(branch_target(i))
            elif i.op == 0x02:
                targets.add(((i.addr + 4) & 0xf0000000) | (i.target << 2))
            elif i.op == 0x03 or (i.op == 0 and i.fn == 0x09):
                raise Unsupported('calls are not lifted yet (%08x)' % i.addr)
            elif i.op == 0 and i.fn == 0x08 and i.rs != 31:
                raise Unsupported('computed jump at %08x' % i.addr)
    for t in targets:
        if not (start <= t < end):
            raise Unsupported('branch out of the function at %08x' % t)

    delay_addrs = {d.addr for d in delay_of.values()}

    # liveness of argument registers: read before a must-write along every path
    # (conservative union via a forward walk with defined-sets per label)
    used_args = set()
    stack_slots = set()
    defined_at = {start: frozenset([0, 28, 29, 31] + list(range(0)))}
    # walk straight through; branches propagate the defined set to their target (intersection)
    work = [start]
    seen = {}
    while work:
        a = work.pop()
        defs = set(defined_at[a])
        while True:
            if a in seen and seen[a] <= defs and a != start:
                break
            seen[a] = frozenset(defs)
            i = byaddr[a]
            ctl = is_control(i)
            if ctl:
                d = delay_of[a]
                r1, w1 = (set(), set()) if is_control(d) else reads_writes(d)
                cr = set()
                if is_branch(i):
                    cr = {i.rs, i.rt} if branch_kind(i) in ('beq', 'bne') else {i.rs}
                elif i.op == 0x01:
                    cr = {i.rs}
                elif i.op == 0 and i.fn == 0x08:
                    cr = {i.rs}
                for r in cr | r1:
                    if r in ARGREGS and r not in defs:
                        used_args.add(r)
                defs |= w1
                if is_branch(i):
                    t = branch_target(i)
                    prev = defined_at.get(t)
                    nd = frozenset(defs) if prev is None else prev & frozenset(defs)
                    if prev is None or nd != prev:
                        defined_at[t] = nd
                        work.append(t)
                    likely = is_likely(i)
                    nxt = a + 8
                    if nxt < end:
                        defined_at.setdefault(nxt, frozenset(defs))
                        if defined_at[nxt] != frozenset(defs):
                            defined_at[nxt] = defined_at[nxt] & frozenset(defs)
                        a = nxt
                        defs = set(defined_at[a])
                        continue
                    break
                elif i.op == 0x02:
                    t = ((i.addr + 4) & 0xf0000000) | (i.target << 2)
                    prev = defined_at.get(t)
                    nd = frozenset(defs) if prev is None else prev & frozenset(defs)
                    if prev is None or nd != prev:
                        defined_at[t] = nd
                        work.append(t)
                    break
                else:  # jr $ra
                    break
            else:
                r, w = reads_writes(i)
                if i.op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b) and i.rs == 29 and i.imm >= frame:
                    off = i.imm - frame
                    stack_slots.add(off // 8)
                for x in r:
                    if x in ARGREGS and x not in defs:
                        used_args.add(x)
                defs |= w
                a += 4
                if a >= end:
                    break
                if a in defined_at:
                    defined_at[a] = defined_at[a] & frozenset(defs)
                    defs = set(defined_at[a])
                else:
                    defined_at[a] = frozenset(defs)
    nstack = (max(stack_slots) + 1) if stack_slots else 0
    if nstack:
        nargs = 8
    else:
        nargs = (max(used_args) - 3) if used_args else 0
    params = ['uint32_t a%d' % k for k in range(nargs)] + ['uint32_t s%d' % k for k in range(nstack)]
    # arg registers are r4..r11; alias them to the parameters
    out = []
    out.append('uint32_t %s(%s)' % (name, ', '.join(params) if params else 'void'))
    out.append('{')
    out.append('    uint32_t ' + ', '.join('r%d = 0' % n for n in range(1, 32) if n != 29) + ', lo = 0, hi = 0;')
    for k in range(nargs):
        out.append('    r%d = a%d;' % (4 + k, k))
    if frame:
        out.append('    uint8_t frame[%d] __attribute__((aligned(16)));' % frame)
        out.append('    (void)frame;')
    out.append('    (void)lo; (void)hi;')

    def stmt(i):
        if i.op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b):
            return mem(i, frame)
        if i.op == 0x09 and i.rs == 29 and i.rt == 29:
            return None   # frame set-up and tear-down
        if i.rt == 29 or (i.op == 0 and i.rd == 29):
            raise Unsupported('write to sp at %08x' % i.addr)
        return alu(i)

    def label(a):
        return 'L_%08x' % a

    def cond_expr(i):
        k = branch_kind(i)
        if k == 'beq':
            return '%s == %s' % (R(i.rs), R(i.rt))
        if k == 'bne':
            return '%s != %s' % (R(i.rs), R(i.rt))
        if k == 'blez':
            return '(int32_t)%s <= 0' % R(i.rs)
        if k == 'bgtz':
            return '(int32_t)%s > 0' % R(i.rs)
        if k == 'bltz':
            return '(int32_t)%s < 0' % R(i.rs)
        if k == 'bgez':
            return '(int32_t)%s >= 0' % R(i.rs)
        raise Unsupported('branch kind at %08x' % i.addr)

    def emit_delay(d):
        s = stmt(d)
        if s:
            out.append('    ' + s)

    k = 0
    while k < len(insns):
        i = insns[k]
        if i.addr in targets:
            out.append('%s:;' % label(i.addr))
        out.append('    /* %08x: %08x */' % (i.addr, i.w))
        if is_control(i):
            d = delay_of[i.addr]
            if is_branch(i):
                likely = is_likely(i)
                out.append('    { int c = %s;' % cond_expr(i))
                if likely:
                    out.append('      if (c) {')
                    s = stmt(d)
                    if s:
                        out.append('        ' + s)
                    out.append('        goto %s; }' % label(branch_target(i)))
                else:
                    s = stmt(d)
                    if s:
                        out.append('      ' + s)
                    out.append('      if (c) goto %s;' % label(branch_target(i)))
                out.append('    }')
                if likely:
                    # not taken: skip the delay slot
                    if k + 2 < len(insns):
                        out.append('    goto %s;' % label(insns[k + 2].addr)) if False else None
                    nxt = insns[k + 2] if k + 2 < len(insns) else None
                    if nxt is not None:
                        targets.add(nxt.addr) if nxt.addr not in targets else None
                        out.append('    goto %s;' % label(nxt.addr))
                elif d.addr in targets:
                    nxt = insns[k + 2] if k + 2 < len(insns) else None
                    if nxt is not None:
                        targets.add(nxt.addr)
                        out.append('    goto %s;' % label(nxt.addr))
            elif i.op == 0x02:
                emit_delay(d)
                out.append('    goto %s;' % label(((i.addr + 4) & 0xf0000000) | (i.target << 2)))
            elif i.op == 0 and i.fn == 0x08:
                emit_delay(d)
                out.append('    return r2;')
            else:
                raise Unsupported('control at %08x' % i.addr)
            # the delay slot itself, for paths that jump to it
            k += 1
            dd = insns[k]
            if dd.addr in targets and not (is_branch(i) and False):
                out.append('%s:;' % label(dd.addr))
                emit_delay(dd)
            k += 1
            continue
        s = stmt(i)
        if s:
            out.append('    ' + s)
        k += 1
    out.append('    return r2;')
    out.append('}')
    # labels that were first requested after emission (likely-branch fall-through): add them
    text = '\n'.join(out)
    for t in sorted(targets):
        if ('%s:;' % label(t)) not in text and ('goto %s;' % label(t)) in text:
            # find the comment line for that address and insert the label before it
            text = text.replace('    /* %08x: ' % t, '%s:;\n    /* %08x: ' % (label(t), t), 1)
    return text, params


if __name__ == '__main__':
    raise SystemExit('use lift_n32 as a module; see lift_test.py')
