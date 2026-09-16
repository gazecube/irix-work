#!/usr/bin/env python3
"""Runtime/application decoder front end for translated IRIX N32 code.

Core-stack-only semantics live in translate_n32_core.py. Keep runtime DSO and
application opcode additions here so extending desktop coverage does not dirty
the expensive static core build.
"""
import translate_n32_core as _core

_core_decode = _core.decode


def decode(addr, insn):
    op = insn >> 26
    rt = (insn >> 16) & 31
    rs = (insn >> 21) & 31
    fn = insn & 63

    # LWU: load a 32-bit word and zero-extend it into the 64-bit N32 GPR.
    if op == 0x27:
        imm = _core.sx16(insn & 0xffff)
        return [
            '%s = (uint64_t)irix_guest_read32be(rt, %s);' %
            (_core.reg(rt), _core.mem_addr(rs, imm))
        ], None

    # MIPS III 64-bit HI/LO arithmetic used by the N32 runtime DSOs.
    if op == 0x00 and fn == 0x1c:  # DMULT
        return [
            '{ __int128 p = (__int128)(int64_t)%s * (__int128)(int64_t)%s;' % (_core.reg(rs), _core.reg(rt)),
            '  cpu->lo = (uint64_t)p;',
            '  cpu->hi = (uint64_t)(((__uint128_t)p) >> 64); }'
        ], None
    if op == 0x00 and fn == 0x1d:  # DMULTU
        return [
            '{ __uint128_t p = (__uint128_t)(uint64_t)%s * (__uint128_t)(uint64_t)%s;' % (_core.reg(rs), _core.reg(rt)),
            '  cpu->lo = (uint64_t)p;',
            '  cpu->hi = (uint64_t)(p >> 64); }'
        ], None
    if op == 0x00 and fn == 0x1e:  # DDIV
        return [
            'if ((int64_t)%s == 0) {' % _core.reg(rt),
            '    cpu->lo = ((int64_t)%s < 0) ? 1 : UINT64_C(0xffffffffffffffff);' % _core.reg(rs),
            '    cpu->hi = (uint64_t)%s;' % _core.reg(rs),
            '} else if ((uint64_t)%s == UINT64_C(0x8000000000000000) && (uint64_t)%s == UINT64_C(0xffffffffffffffff)) {' % (_core.reg(rs), _core.reg(rt)),
            '    cpu->lo = UINT64_C(0x8000000000000000); cpu->hi = 0;',
            '} else {',
            '    cpu->lo = (uint64_t)((int64_t)%s / (int64_t)%s);' % (_core.reg(rs), _core.reg(rt)),
            '    cpu->hi = (uint64_t)((int64_t)%s %% (int64_t)%s);' % (_core.reg(rs), _core.reg(rt)),
            '}'
        ], None
    if op == 0x00 and fn == 0x1f:  # DDIVU
        return [
            'if ((uint64_t)%s == 0) {' % _core.reg(rt),
            '    cpu->lo = UINT64_C(0xffffffffffffffff);',
            '    cpu->hi = (uint64_t)%s;' % _core.reg(rs),
            '} else {',
            '    cpu->lo = (uint64_t)%s / (uint64_t)%s;' % (_core.reg(rs), _core.reg(rt)),
            '    cpu->hi = (uint64_t)%s %% (uint64_t)%s;' % (_core.reg(rs), _core.reg(rt)),
            '}'
        ], None

    return _core_decode(addr, insn)


# Make the inherited emitter use the runtime/application decoder.
_core._base.decode = decode
Elf32BE = _core.Elf32BE
emit = _core.emit
c_name = _core.c_name
reg = _core.reg
mem_addr = _core.mem_addr
sx16 = _core.sx16
sx32_expr = _core.sx32_expr


def main():
    return _core.main()


if __name__ == '__main__':
    raise SystemExit(main())
