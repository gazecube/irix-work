#!/usr/bin/env python3
"""Runtime/application N32 decoder extensions.

The static core uses translate_n32_core.py. This layer adds instruction classes
first observed in IRIX runtime DSOs/applications so desktop coverage can grow
without invalidating the 8k-function core build.
"""
import translate_n32_core as _core
import translate_n32 as _base

_core_decode = _core.decode


def _set_fcc(expr):
    return [
        'if (%s) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23);' % expr
    ], None


def _fp_round(fmt, funct, fs, fd):
    get = 'irix_guest_fpr_get_s(cpu, %d)' % fs if fmt == 0x10 else 'irix_guest_fpr_get_d(cpu, %d)' % fs
    suff = 'f' if fmt == 0x10 else ''
    if funct in (0x08, 0x09, 0x0a, 0x0b):
        fn = {0x08: 'llrint', 0x09: 'trunc', 0x0a: 'ceil', 0x0b: 'floor'}[funct] + suff
        return ['cpu->fpr[%d] = (uint64_t)(int64_t)%s(%s);' % (fd, fn, get)], None
    if funct in (0x0c, 0x0d, 0x0e, 0x0f):
        fn = {0x0c: 'lrint', 0x0d: 'trunc', 0x0e: 'ceil', 0x0f: 'floor'}[funct] + suff
        return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)(int32_t)%s(%s);' % (fd, fd, fn, get)], None
    if funct == 0x24:
        return ['cpu->fpr[%d] = (cpu->fpr[%d] & UINT64_C(0xffffffff00000000)) | (uint32_t)(int32_t)lrint%s(%s);' % (fd, fd, suff, get)], None
    if funct == 0x25:
        return ['cpu->fpr[%d] = (uint64_t)(int64_t)llrint%s(%s);' % (fd, suff, get)], None
    return None


def decode(addr, insn):
    op = insn >> 26
    rt = (insn >> 16) & 31
    rs = (insn >> 21) & 31
    rd = (insn >> 11) & 31
    sa = (insn >> 6) & 31
    fn = insn & 63
    imm = _base.sx16(insn & 0xffff)

    if op == 0x00:
        if fn == 0x0a:
            return ['if (%s == 0) %s = %s;' % (_base.reg(rt), _base.reg(rd), _base.reg(rs))], None
        if fn == 0x0b:
            return ['if (%s != 0) %s = %s;' % (_base.reg(rt), _base.reg(rd), _base.reg(rs))], None
        if fn == 0x0f:
            return ['/* sync */'], None
        if fn == 0x14:
            return ['%s = (uint64_t)%s << ((uint64_t)%s & 63u);' % (_base.reg(rd), _base.reg(rt), _base.reg(rs))], None
        if fn == 0x16:
            return ['%s = (uint64_t)%s >> ((uint64_t)%s & 63u);' % (_base.reg(rd), _base.reg(rt), _base.reg(rs))], None
        if fn == 0x17:
            return ['%s = (uint64_t)((int64_t)%s >> ((uint64_t)%s & 63u));' % (_base.reg(rd), _base.reg(rt), _base.reg(rs))], None
        if fn == 0x1c:
            return ['{ __int128 p = (__int128)(int64_t)%s * (__int128)(int64_t)%s;' % (_base.reg(rs), _base.reg(rt)), '  cpu->lo = (uint64_t)p;', '  cpu->hi = (uint64_t)(((__uint128_t)p) >> 64); }'], None
        if fn == 0x1d:
            return ['{ __uint128_t p = (__uint128_t)(uint64_t)%s * (__uint128_t)(uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)), '  cpu->lo = (uint64_t)p;', '  cpu->hi = (uint64_t)(p >> 64); }'], None
        if fn == 0x1e:
            return ['if ((int64_t)%s == 0) {' % _base.reg(rt), '    cpu->lo = ((int64_t)%s < 0) ? 1 : UINT64_C(0xffffffffffffffff);' % _base.reg(rs), '    cpu->hi = (uint64_t)%s;' % _base.reg(rs), '} else if ((uint64_t)%s == UINT64_C(0x8000000000000000) && (uint64_t)%s == UINT64_C(0xffffffffffffffff)) {' % (_base.reg(rs), _base.reg(rt)), '    cpu->lo = UINT64_C(0x8000000000000000); cpu->hi = 0;', '} else {', '    cpu->lo = (uint64_t)((int64_t)%s / (int64_t)%s);' % (_base.reg(rs), _base.reg(rt)), '    cpu->hi = (uint64_t)((int64_t)%s %% (int64_t)%s);' % (_base.reg(rs), _base.reg(rt)), '}'], None
        if fn == 0x1f:
            return ['if ((uint64_t)%s == 0) {' % _base.reg(rt), '    cpu->lo = UINT64_C(0xffffffffffffffff);', '    cpu->hi = (uint64_t)%s;' % _base.reg(rs), '} else {', '    cpu->lo = (uint64_t)%s / (uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)), '    cpu->hi = (uint64_t)%s %% (uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)), '}'], None
        if fn in (0x2c, 0x2d):
            return ['%s = (uint64_t)%s + (uint64_t)%s;' % (_base.reg(rd), _base.reg(rs), _base.reg(rt))], None
        if fn in (0x2e, 0x2f):
            return ['%s = (uint64_t)%s - (uint64_t)%s;' % (_base.reg(rd), _base.reg(rs), _base.reg(rt))], None
        if fn in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36):
            cond = {0x30: '(int64_t)%s >= (int64_t)%s', 0x31: '(uint64_t)%s >= (uint64_t)%s', 0x32: '(int64_t)%s < (int64_t)%s', 0x33: '(uint64_t)%s < (uint64_t)%s', 0x34: '%s == %s', 0x36: '%s != %s'}[fn] % (_base.reg(rs), _base.reg(rt))
            code = (insn >> 6) & 0x3ff
            return ['if (%s) { irix_guest_trap(rt, cpu, %du); return; }' % (cond, code)], None

    if op == 0x01 and rt in (0x10, 0x11, 0x12, 0x13):
        dst = (addr + 4 + (imm << 2)) & 0xffffffff
        nonnegative = rt in (0x11, 0x13)
        likely = rt in (0x12, 0x13)
        cond = '(int64_t)%s >= 0' % _base.reg(rs) if nonnegative else '(int64_t)%s < 0' % _base.reg(rs)
        lines = ['%s = %s;' % (_base.reg(31), _base.sx32_expr('0x%08xu' % (addr + 8))), 'if (%s) {' % cond, '    pending_kind = IRIX_PENDING_JUMP;', '    pending_target = 0x%08xu;' % dst, '}']
        if likely:
            lines += ['else {', '    pc += 4;', '}']
        return lines, 'control'

    if op == 0x18:
        return ['%s = (uint64_t)%s + (uint64_t)(int64_t)(int16_t)0x%04xu;' % (_base.reg(rt), _base.reg(rs), insn & 0xffff)], None
    if op == 0x27:
        return ['%s = (uint64_t)irix_guest_read32be(rt, %s);' % (_base.reg(rt), _base.mem_addr(rs, imm))], None
    if op in (0x2f, 0x33):
        return ['/* cache/pref hint */'], None
    if op == 0x30:
        return ['%s = (uint64_t)(int64_t)(int32_t)irix_guest_read32be(rt, %s);' % (_base.reg(rt), _base.mem_addr(rs, imm))], None
    if op == 0x34:
        return ['%s = irix_guest_read64be(rt, %s);' % (_base.reg(rt), _base.mem_addr(rs, imm))], None
    if op == 0x38:
        return ['irix_guest_write32be(rt, %s, (uint32_t)%s);' % (_base.mem_addr(rs, imm), _base.reg(rt)), '%s = 1;' % _base.reg(rt)], None
    if op == 0x3c:
        return ['irix_guest_write64be(rt, %s, %s);' % (_base.mem_addr(rs, imm), _base.reg(rt)), '%s = 1;' % _base.reg(rt)], None

    if op == 0x11:
        fmt = rs
        ft = rt
        fs = rd
        fd = sa
        funct = fn
        if fmt == 0x02:
            value = 'cpu->fcr31' if fs == 31 else '0u'
            return ['%s = (uint64_t)(int64_t)(int32_t)(uint32_t)%s;' % (_base.reg(ft), value)], None
        if fmt == 0x06:
            if fs == 31:
                return ['cpu->fcr31 = (uint32_t)%s;' % _base.reg(ft)], None
            return ['/* ctc1 to unmodeled control register */'], None
        if fmt in (0x10, 0x11):
            rounded = _fp_round(fmt, funct, fs, fd)
            if rounded is not None:
                return rounded
            if 0x30 <= funct <= 0x3f:
                a = 'irix_guest_fpr_get_s(cpu, %d)' % fs if fmt == 0x10 else 'irix_guest_fpr_get_d(cpu, %d)' % fs
                b = 'irix_guest_fpr_get_s(cpu, %d)' % ft if fmt == 0x10 else 'irix_guest_fpr_get_d(cpu, %d)' % ft
                unordered = '(isnan(%s) || isnan(%s))' % (a, b)
                low = funct & 7
                parts = []
                if low & 1: parts.append(unordered)
                if low & 2: parts.append('(%s == %s)' % (a, b))
                if low & 4: parts.append('(%s < %s)' % (a, b))
                return _set_fcc(' || '.join(parts) if parts else '0')

    return _core_decode(addr, insn)


_base.decode = decode
Elf32BE = _base.Elf32BE
emit = _base.emit
c_name = _base.c_name
reg = _base.reg
mem_addr = _base.mem_addr
sx16 = _base.sx16
sx32_expr = _base.sx32_expr


def main():
    return _base.main()


if __name__ == '__main__':
    raise SystemExit(main())
