#!/usr/bin/env python3
"""REGIMM-corrected front end for translate_n32.

IRIX uses BLTZAL/BGEZAL and their branch-likely forms in compiler-generated
local helper calls. The original translator handled only BGEZAL and lost the
link assignment when rewriting a likely branch. Patch the shared decoder here
so all production generators can use the corrected semantics without carrying
a second full translator implementation.
"""
import translate_n32 as _base

_base_decode = _base.decode


def decode(addr, insn):
    op = insn >> 26
    rt = (insn >> 16) & 31
    rs = (insn >> 21) & 31
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
