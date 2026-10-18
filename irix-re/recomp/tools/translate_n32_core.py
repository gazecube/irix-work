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
    fn = insn & 63

    # N32 keeps 64-bit GPRs and HI/LO state.  These transfers are emitted by
    # native MIPS III objects (including the Microline build) and must preserve
    # the full register value.
    if op == 0 and fn == 0x11:  # MTHI
        return ['cpu->hi = %s;' % _base.reg(rs)], None
    if op == 0 and fn == 0x13:  # MTLO
        return ['cpu->lo = %s;' % _base.reg(rs)], None

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

    # MIPS III forms clang emits that the SGI compiler did not: 64-bit
    # variable shifts and add/subtract, and the remaining COP1 compare
    # conditions (existing eq/lt/le encodings stay with the base decoder).
    if op == 0 and fn in (0x14, 0x16, 0x17, 0x2c, 0x2d, 0x2e, 0x2f):
        rd = (insn >> 11) & 31
        exprs = {
            0x14: '(uint64_t)%s << (%s & 63u)' % (_base.reg(rt), _base.reg(rs)),
            0x16: '(uint64_t)%s >> (%s & 63u)' % (_base.reg(rt), _base.reg(rs)),
            0x17: '(uint64_t)((int64_t)%s >> (%s & 63u))' % (_base.reg(rt), _base.reg(rs)),
            0x2c: '%s + %s' % (_base.reg(rs), _base.reg(rt)),
            0x2d: '%s + %s' % (_base.reg(rs), _base.reg(rt)),
            0x2e: '%s - %s' % (_base.reg(rs), _base.reg(rt)),
            0x2f: '%s - %s' % (_base.reg(rs), _base.reg(rt)),
        }
        return ['%s = %s;' % (_base.reg(rd), exprs[fn])], None
    if op == 0x11 and rs in (0x10, 0x11) and fn >= 0x30 and fn not in (0x32, 0x3c, 0x3e):
        ft = rt
        fs = (insn >> 11) & 31
        cond = fn & 15
        get = 'irix_guest_fpr_get_s' if rs == 0x10 else 'irix_guest_fpr_get_d'
        ty = 'float' if rs == 0x10 else 'double'
        return ['{ %s a = %s(cpu, %d), b = %s(cpu, %d); int un = (a != a) || (b != b);' % (ty, get, fs, get, ft),
                '  int hit = (((%d) & 4) && !un && a < b) || (((%d) & 2) && !un && a == b) || (((%d) & 1) && un);' % (cond, cond, cond),
                '  if (hit) cpu->fcr31 |= (1u << 23); else cpu->fcr31 &= ~(1u << 23); }'], None

    return _base_decode(addr, insn)


# Make _base.emit/main use the core-corrected decoder too.
_base.decode = decode
Elf32BE = _base.Elf32BE
emit = _base.emit
emit_range = _base.emit_range
c_name = _base.c_name
reg = _base.reg
mem_addr = _base.mem_addr
sx16 = _base.sx16
sx32_expr = _base.sx32_expr
static_control_info = _base.static_control_info


def main():
    return _base.main()


if __name__ == '__main__':
    raise SystemExit(main())
