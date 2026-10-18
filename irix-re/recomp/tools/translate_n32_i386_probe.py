#!/usr/bin/env python3
"""Emit i386 assembly for a deliberately small, fail-closed MIPS subset."""
import argparse
from pathlib import Path


def low(register, target="%eax"):
    return (f"    xorl {target}, {target}" if register == 0 else
            f"    movl {register * 8}(%ecx), {target}")


def emit_instruction(word):
    if word == 0:  # sll $zero, $zero, 0 (nop)
        return ["    # nop"]
    op = word >> 26
    rs = (word >> 21) & 31
    rt = (word >> 16) & 31
    rd = (word >> 11) & 31
    funct = word & 63
    imm = word & 0xffff
    if imm & 0x8000:
        imm -= 0x10000

    if op == 0x09:  # ADDIU: sign-extend the wrapped 32-bit result.
        dst = rt
        operation = f"addl ${imm}, %eax"
    elif op == 0x0f:  # LUI: sign-extend the 32-bit result to N32 GPR width.
        dst = rt
        value = (word & 0xffff) << 16
        return [
            f"    movl ${value}, %eax",
            "    cdq",
            f"    movl %eax, {dst * 8}(%ecx)",
            f"    movl %edx, {dst * 8 + 4}(%ecx)",
        ] if dst else [f"    # 0x{word:08x}: write to $zero is discarded"]
    elif op == 0x0d:  # ORI: zero-extend immediate, preserve upper GPR word.
        dst = rt
        if dst == 0:
            return [f"    # 0x{word:08x}: write to $zero is discarded"]
        return [
            low(rs),
            f"    orl ${word & 0xffff}, %eax",
            ("    xorl %edx, %edx" if rs == 0 else
             f"    movl {rs * 8 + 4}(%ecx), %edx"),
            f"    movl %eax, {dst * 8}(%ecx)",
            f"    movl %edx, {dst * 8 + 4}(%ecx)",
        ]
    elif op == 0 and funct in (0x21, 0x23):  # ADDU / SUBU
        dst = rd
        mnemonic = "addl" if funct == 0x21 else "subl"
        operation = f"{mnemonic} {rt * 8}(%ecx), %eax"
    elif op in (0x23, 0x24, 0x28, 0x2b):  # LW, LBU, SB, SW
        lines = [
            low(rs, "%edx"),
            f"    addl ${imm}, %edx",
        ]
        if op == 0x23:  # LW: guest big-endian word, sign-extended to N32 GPR.
            lines += [
                "    movl (%ebx,%edx), %eax",
                "    bswap %eax",
                "    cdq",
            ]
            if rt:
                lines += [
                    f"    movl %eax, {rt * 8}(%ecx)",
                    f"    movl %edx, {rt * 8 + 4}(%ecx)",
                ]
        elif op == 0x24:  # LBU: byte load, zero-extended.
            lines += [
                "    movzbl (%ebx,%edx), %eax",
            ]
            if rt:
                lines += [
                    "    xorl %edx, %edx",
                    f"    movl %eax, {rt * 8}(%ecx)",
                    f"    movl %edx, {rt * 8 + 4}(%ecx)",
                ]
        elif op == 0x2b:  # SW: reverse host little-endian storage to guest order.
            lines += [
                low(rt),
                "    bswap %eax",
                "    movl %eax, (%ebx,%edx)",
            ]
        else:  # SB
            lines += [
                low(rt),
                "    movb %al, (%ebx,%edx)",
            ]
        return lines
    else:
        raise ValueError(f"unsupported probe instruction 0x{word:08x}")

    if dst == 0:
        return [f"    # 0x{word:08x}: write to $zero is discarded"]
    if op == 0x09:
        return [
            low(rs),
            operation,
            "    cdq",
            f"    movl %eax, {dst * 8}(%ecx)",
            f"    movl %edx, {dst * 8 + 4}(%ecx)",
        ]
    return [
        low(rs),
        operation,
        "    cdq",
        f"    movl %eax, {dst * 8}(%ecx)",
        f"    movl %edx, {dst * 8 + 4}(%ecx)",
    ]


def emit(words):
    lines = [
        "    .text",
        "    .globl n32_native_probe",
        "    .type n32_native_probe, @function",
        "n32_native_probe:",
        "    pushl %ebx",
        "    movl 8(%esp), %ecx",
        "    movl 12(%esp), %ebx",
    ]
    i = 0
    terminated = False
    while i < len(words):
        word = words[i]
        op = word >> 26
        rs = (word >> 21) & 31
        rt = (word >> 16) & 31
        lines.append(f".L_{i}:")
        if op in (0x04, 0x05):  # BEQ / BNE with one architectural delay slot.
            if i + 1 >= len(words):
                raise ValueError(f"branch at word {i} has no delay slot")
            disp = word & 0xffff
            if disp & 0x8000:
                disp -= 0x10000
            target = i + 1 + disp
            if target < 0 or target >= len(words) or target == i + 1:
                raise ValueError(f"branch at word {i} has unsupported target {target}")
            delay = words[i + 1]
            if is_control(delay):
                raise ValueError(f"branch at word {i} has a control instruction in its delay slot")
            taken = f".L_taken_{i}"
            fallthrough = f".L_fallthrough_{i}"
            lines.extend([
                low(rs),
                *( [f"    xorl {rt * 8}(%ecx), %eax"] if rt else [] ),
                ("    xorl %edx, %edx" if rs == 0 else
                 f"    movl {rs * 8 + 4}(%ecx), %edx"),
                *( [f"    xorl {rt * 8 + 4}(%ecx), %edx"] if rt else [] ),
                "    orl %edx, %eax",
                "    testl %eax, %eax",
                f"    {'jz' if op == 0x04 else 'jnz'} {taken}",
            ])
            lines.extend(emit_instruction(delay))
            lines.extend([f"    jmp {fallthrough}", f"{taken}:"])
            lines.extend(emit_instruction(delay))
            lines.extend([f"    jmp .L_{target}", f"{fallthrough}:"])
            i += 2
            continue
        if is_return(word):
            if i + 1 >= len(words):
                raise ValueError(f"return at word {i} has no delay slot")
            delay = words[i + 1]
            if is_control(delay):
                raise ValueError(f"return at word {i} has a control instruction in its delay slot")
            lines.extend(emit_instruction(delay))
            lines.extend(["    xorl %eax, %eax", "    popl %ebx", "    ret"])
            if i + 2 != len(words):
                raise ValueError("instructions follow terminal jr $ra")
            i += 2
            terminated = True
            break
        if is_control(word):
            raise ValueError(f"unsupported control instruction 0x{word:08x} at word {i}")
        lines.extend(emit_instruction(word))
        i += 1
    if not terminated:
        lines.extend(["    xorl %eax, %eax", "    popl %ebx", "    ret"])
    lines.extend([
        "    .size n32_native_probe, .-n32_native_probe",
        '    .section .note.GNU-stack,"",@progbits',
        "",
    ])
    return "\n".join(lines)


def is_return(word):
    return word == 0x03e00008  # jr $ra


def is_control(word):
    op = word >> 26
    return is_return(word) or op in (0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                     0x14, 0x15, 0x16, 0x17)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("words", nargs="+", type=lambda value: int(value, 0))
    args = parser.parse_args()
    try:
        text = emit(args.words)
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == "__main__":
    main()
