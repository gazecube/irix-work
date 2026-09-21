#!/usr/bin/env python3
"""Core-stack decoder front end for the translated IRIX N32 libraries.

Keep this file limited to instruction semantics required by the static Xt/Motif/
SGI core closure. Runtime/application-only opcode additions belong in
translate_n32_regimm.py so they do not invalidate the expensive core build.
"""
import translate_n32 as _base

_base_decode = _base.decode


def decode(addr, insn):
    op = insn >> 26
    rt = (insn >> 16) & 31
    rs = (insn >> 21) & 31

    # Correct BLTZAL/BGEZAL and branch-likely link variants. The base decoder
    # historically handled only BGEZAL and could lose $ra on likely branches.
    if op == 0x01 and rt in (0x10, 0x11, 0x12, 0x13):
        imm = _base.sx16(insn & 0xffff)
        dst = (addr + 4 + (imm << 2)) & 0xffffffff
        nonnegative = rt in (0x11, 0x13)
        likely = rt in (0x12, 0x13)
        cond = '(int64_t)%s >= 0' % _base.reg(rs) if nonnegative else '(int64_t)%s < 0' % _base.reg(rs)
        lines = [
            '%s = %s;' % (_base.reg(31), _base.sx32_expr('0x%08xu' % (addr + 8))),
            'if (%s) {' % cond,
            '    pending_kind = IRIX_PENDING_LINK;',
            '    pending_target = 0x%08xu;' % dst,
            '    pending_return = 0x%08xu;' % (addr + 8),
            '}'
        ]
        if likely:
            lines += ['else {', '    pc += 4;', '}']
        return lines, 'control'

    return _base_decode(addr, insn)


# Make _base.emit/main use the core-corrected decoder too.
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
