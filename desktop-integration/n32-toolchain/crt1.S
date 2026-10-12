/* Minimal IRIX N32 startup, equivalent in effect to SGI's crt1.o. */
        .set    noreorder
        .text
        .globl  __start
        .ent    __start
__start:
        bal     1f
        nop
1:
        .cpload $31
        move    $18, $31            # (unused) keep ra
        lw      $4, 0($29)          # argc
        addiu   $5, $29, 4          # argv
        addiu   $1, $0, -16
        and     $29, $29, $1
        addiu   $29, $29, -32
        sll     $2, $4, 2
        addu    $6, $5, $2
        addiu   $6, $6, 4           # envp = argv + argc + 1
        lw      $7, %got(__Argc)($28)
        sw      $4, 0($7)
        lw      $7, %got(__Argv)($28)
        sw      $5, 0($7)
        lw      $7, %got(_environ)($28)
        lw      $2, 0($7)
        bnez    $2, 2f
        nop
        sw      $6, 0($7)
2:
        sw      $4, 16($29)         # save argc/argv/envp across the call
        sw      $5, 20($29)
        sw      $6, 24($29)
        lw      $25, %call16(__readenv_sigfpe)($28)
        jalr    $25
        nop
        lw      $4, 16($29)
        lw      $5, 20($29)
        lw      $6, 24($29)
        lw      $25, %call16(main)($28)
        jalr    $25
        nop
        move    $4, $2
        lw      $25, %call16(exit)($28)
        jalr    $25
        nop
        break   0
        .end    __start
        .data
        .globl  __Argc
__Argc: .word   0
        .globl  __Argv
__Argv: .word   0
