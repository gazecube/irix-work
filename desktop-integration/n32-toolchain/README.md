# Building IRIX N32 programs on the host

`irixcc.sh` makes clang behave like MIPSpro `cc -n32 -mips3`, using the IRIX
headers in `IRIX-TREE` / `holyshit`.  `crt1.S` replaces SGI's `crt1.o` (lld
cannot link its `.MIPS.events` relocations).  Link with:

    ld.lld -m elf32btsmipn32 --allow-shlib-undefined \
        --dynamic-linker=/usr/lib32/libc.so.1 -e __start \
        --image-base=0x10000000 -z max-page-size=0x1000 \
        -z separate-loadable-segments --no-rosegment \
        -o prog crt1.o prog.o IRIX-TREE/lib32/libc.so.1 [libs...]

Run the result under the interpreter backend:

    IRIX_EXEC_BACKEND=interp IRIX_GUEST_EXEC_PATH=/path/in/tree \
        irix-guest-exec /path/in/tree

Kernel-visible structs (ksa.h, sysinfo.h, sysmp.h, ...) must come from
`irix-657m-src/irix/kern/sys` (overlay include dir), not the installed copies.
Debug aids: IRIX_TRACE_SYSCALLS=1 prints every guest syscall and the exit stack.
