#!/usr/bin/env python3
"""REGIMM-corrected front end for translate_n32.

IRIX uses BLTZAL/BGEZAL and their branch-likely forms in compiler-generated
local helper calls. The original translator handled only BGEZAL and lost the
link assignment when rewriting a likely branch. Patch the shared decoder here
so all production generators can use the corrected semantics without carrying
a second full translator implementation.

This front end also carries instruction classes first observed in the runtime
DSO/application corpus rather than the static core archives.
"""
import translate_n32 as _base

_base_decode = _base.decode


def decode(addr, insn):
    op = insn >> 26
    rt = (insn >> 16) & 31
    rs = (insn >> 21) & 31
    fn = insn & 63

    if op == 0x01 and rt in (0x10, 0x11, 0x12, 0x13):
        imm = _base.sx16(insn & 0xffff)
        dst = (addr + 4 + (imm << 2)) & 0xffffffff
        nonnegative = rt in (0x11, 0x13)
        likely = rt in (0x12, 0x13)
        cond = '(int64_t)%s >= 0' % _base.reg(rs) if nonnegative else '(int64_t)%s < 0' % _base.reg(rs)
        lines = [
            '%s = %s;' % (_base.reg(31), _base.sx32_expr('0x%08xu' % (addr + 8))),
            'if (%s) {' % cond,
            '    pending_kind = IRIX_PENDING_JUMP;',
            '    pending_target = 0x%08xu;' % dst,
            '}'
        ]
        if likely:
            lines += ['else {', '    pc += 4;', '}']
        return lines, 'control'

    # LWU: load a 32-bit word and zero-extend it into the 64-bit N32 GPR.
    if op == 0x27:
        imm = _base.sx16(insn & 0xffff)
        return [
            '%s = (uint64_t)irix_guest_read32be(rt, %s);' %
            (_base.reg(rt), _base.mem_addr(rs, imm))
        ], None

    # MIPS III 64-bit HI/LO arithmetic used by the N32 runtime DSOs.
    if op == 0x00 and fn == 0x1c:  # DMULT
        return [
            '{ __int128 p = (__int128)(int64_t)%s * (__int128)(int64_t)%s;' % (_base.reg(rs), _base.reg(rt)),
            '  cpu->lo = (uint64_t)p;',
            '  cpu->hi = (uint64_t)(((__uint128_t)p) >> 64); }'
        ], None
    if op == 0x00 and fn == 0x1d:  # DMULTU
        return [
            '{ __uint128_t p = (__uint128_t)(uint64_t)%s * (__uint128_t)(uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)),
            '  cpu->lo = (uint64_t)p;',
            '  cpu->hi = (uint64_t)(p >> 64); }'
        ], None
    if op == 0x00 and fn == 0x1e:  # DDIV
        return [
            'if ((int64_t)%s == 0) {' % _base.reg(rt),
            '    cpu->lo = ((int64_t)%s < 0) ? 1 : UINT64_C(0xffffffffffffffff);' % _base.reg(rs),
            '    cpu->hi = (uint64_t)%s;' % _base.reg(rs),
            '} else if ((uint64_t)%s == UINT64_C(0x8000000000000000) && (uint64_t)%s == UINT64_C(0xffffffffffffffff)) {' % (_base.reg(rs), _base.reg(rt)),
            '    cpu->lo = UINT64_C(0x8000000000000000); cpu->hi = 0;',
            '} else {',
            '    cpu->lo = (uint64_t)((int64_t)%s / (int64_t)%s);' % (_base.reg(rs), _base.reg(rt)),
            '    cpu->hi = (uint64_t)((int64_t)%s %% (int64_t)%s);' % (_base.reg(rs), _base.reg(rt)),
            '}'
        ], None
    if op == 0x00 and fn == 0x1f:  # DDIVU
        return [
            'if ((uint64_t)%s == 0) {' % _base.reg(rt),
            '    cpu->lo = UINT64_C(0xffffffffffffffff);',
            '    cpu->hi = (uint64_t)%s;' % _base.reg(rs),
            '} else {',
            '    cpu->lo = (uint64_t)%s / (uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)),
            '    cpu->hi = (uint64_t)%s %% (uint64_t)%s;' % (_base.reg(rs), _base.reg(rt)),
            '}'
        ], None

    return _base_decode(addr, insn)


# Make _base.emit/main use the corrected decoder too.
_base.decode = decode
Elf32BE = _base.Elf32BE
emit = _base.emit
c_name = _base.c_name
reg = _base.reg
sx16 = _base.sx16
sx32_expr = _base.sx32_expr


def main():
    return _base.main()


if __name__ == '__main__':
    raise SystemExit(main())
