# Transition from the guest runtime to native x86 (2026-11-09)

PLAN.md says what a native library has to look like. This file says how the whole system gets
from today (desktop running in the MIPS guest runtime) to that state without a flag day: what
flips, in which order, what has to exist first, and how each step is checked.

Numbers below come from `~/opt/claude-work/x86-transition/inventory.py` run over IRIX-TREE.

## 1. Where things stand

Three ways an IRIX program runs today:

| path | what it is | state |
|---|---|---|
| guest-exec | original MIPS image, IRIX rld, interpreter, libirixre | generic, slow, runs most things (sysmgr since 2026-11-08) |
| bundle runner | all DSOs translated to x86_64, big-endian guest memory, libirixre, host-ABI bridges | what the desktop session uses |
| native | lifted to C (lift_dso.py / lift_lib.py), native little-endian memory, gcc -m32, no libirixre | xwsh links and runs up to a null read; UnixWare Motif corpus passes the oracle via nativemem |

Already native, from source: Xt (xt32, xtsys), Motif 1.2 oracle (xm32, xm64), irix-compat
(init, getty, chkconfig, mount, ...), fam, mediad, the kernel side (array sessions, statvfs,
STREAMS, nice/rlimit calls).

What the tree contains (MIPS ELF objects):

| ABI | executables | DSOs |
|---|---|---|
| N32 | 1465 | 950 |
| O32 | 389 | 813 |
| N64 | 14 | 323 |

Consequence: applications are N32 (and O32). N64 is almost only libraries.

### ABI mapping (decided 2026-11-09): multilib, like IRIX

| IRIX | x86 now | x86 later (optional) | directory |
|---|---|---|---|
| O32 | i386 (-m32) | i386 | /usr/lib |
| N32 | i386 (-m32) | x32 (-mx32) | /usr/lib32 |
| N64 | x86_64 (-m64) | x86_64 | /usr/lib64 |

N32 cannot become plain x86_64: 32-bit pointers and longs are baked into every struct offset.
Its exact x86 counterpart is x32 (x86_64 code, ILP32, 64-bit types aligned to 8, long long in
one register), but x32 is rare: masked on the standard Gentoo amd64 profile, gcc here is built
for m32/m64 only, no x32 glibc or X libraries are installed, and accelerated GL would need an
x32 Mesa. So N32 is built as i386 now (supported by Portage; glibc, libX11, libXt are already
installed as i386) and x32 stays a later rebuild:

- The lifter emits C; -m32 or -mx32 is a compiler flag. Lifter output and the libc layer must
  not assume i386 (no hand-written 32-bit assembly, no assumptions about time_t or off_t size,
  stack-slot layout only through the generated entry stubs).
- The directory layout is the IRIX one from the start. O32-derived and N32-derived libraries
  with the same soname are both i386, so the loader cannot tell them apart by itself: every
  lifted program and library is linked with DT_RUNPATH set to its own ABI's directories
  (/usr/lib32:/lib32 for N32, /usr/lib:/lib for O32). The IRIX-soname stubs for host libraries
  exist once per directory.
- Known costs of i386 for N32: 64-bit values take two registers (slower, irrelevant for desktop
  programs); double and long long align to 4 in host structs, which matters only where lifted
  code shares such a struct with a host library (audited, section 4).
- Kernel #38 has CONFIG_X86_X32_ABI=y so the later switch needs no kernel work.

Programs follow their libraries. The 20 IRIX syscalls must be added to syscall_32.tbl with
compat layouts before any i386 program needs them (kernel task, phase 1).

### What the IRIX sources cover (~/Projects/irix-657m-src, IRIX 6.5.7m)

| part | content | effect on this plan |
|---|---|---|
| irix/kern | the kernel, already the base of the kernel-side ports | sys/xlate.h and the irix5_n32_* / ABI_IRIX5_N32 code (35 files) show how one kernel serves O32, N32 and N64 callers with per-ABI struct layouts. The Linux side copies that shape: each IRIX syscall has an i386 (compat), an x32 and a 64-bit entry |
| irix/lib/libc | libc source: stdio, gen, locale, signal, proc, mp, rld, ... with the export lists (common.exports, versiondefs) | libc.so.1 can be built from source for all three ABIs with only the syscall layer retargeted, and checked symbol for symbol against the export lists |
| eoe/lib (62) | libgen, libw, libmalloc, libmutex, libcurses, libdisk, libdb, libfam, libmediaclient, libnsl, libsocket, libpthread, librpcsvc, libdl, ... | built from source, not lifted |
| eoe/cmd (345), irix/cmd (40) | the base commands | built from source, as irix-compat already does for init, getty, chkconfig |

Not in the tree: X11, Xt, Motif, the desktop, ViewKit, GL, the sysadm stack. Those are host
libraries, UnixWare source, or lifted binaries as before.

Kernel state on 2026-11-09: the 20 IRIX syscalls are in syscall_64.tbl as "common" (so 64-bit
and x32 callers reach them) and absent from syscall_32.tbl (i386 callers do not).

## 2. Rules of the transition

1. **The unit that flips is a process.** A process is entirely guest (big-endian memory, MIPS
   register file) or entirely native. No mixed processes, no facades, no host-ABI wrappers.
2. **A library is native before the first program that needs it flips.** Order follows the
   dependency graph, leaves first.
3. **Both worlds run side by side on one desktop until the last program has flipped.** They only
   meet at process boundaries: X, files, pipes/sockets, signals. Every such boundary that
   carries binary data is listed in section 5 and checked per program.
4. **The guest runtime is the reference, not the product.** It gets fixes that are real bugs
   (ELFMAP reuse, gp restore) or that make it a better oracle (tracing). It gets no new
   bridges and no stand-ins.
5. **One lowering tool.** The lifter (lift_dso.py, lift_lib.py) is the path forward: it works
   on linked DSOs and executables, which is what IRIX ships. nativemem keeps its role as the
   source of types (nm_swap, bitfield and layout profiles, UnixWare DWARF) and as the test
   corpus with known answers. No second translator is maintained.
6. **The host root is the IRIX userland.** Native binaries are installed at the IRIX path
   (/usr/sbin/xwsh). Data, man pages, config and html from the tree live on the host root.
   IRIX-TREE stays as the pristine reference and as the input of the lifter.

## 3. Where each library comes from

| source | libraries | notes |
|---|---|---|
| host library under the IRIX soname | libX11.so.1, libXext, libXmu, libXi, libXpm, libXaw, libm, libGL, libGLU | empty .so with the IRIX soname and DT_NEEDED on the host library; ld.so puts the host symbols in scope |
| IRIX source (irix-657m-src), on glibc | libc.so.1, libgen, libw, libmalloc, libmutex, libdl, libcurses, libdisk, libdb, libfam, libmediaclient, libnsl, libsocket, librpcsvc, libpthread | preferred whenever source exists. libc.so.1 is a layer over glibc, see section 9 |
| other source | libXt (UnixWare R5), Motif 1.2 oracle | - |
| hand reconstruction | libSgm pieces (Dial.c), 4Dwm, fm, toolchest where irix-re/ already has them | only where a reconstruction is already exact |
| lifted from the IRIX binary | libXm (SGI), libSgm, libvk, libC/libCsup, libimd*, libdesktop*, libsa*, libfm, libwebviewer, ... | the bulk |
| rewritten over a host service | libgl (IRIS GL over OpenGL), libaudio (AL over ALSA), libXsgivc, libdmedia parts | the bridge logic that exists in libirixre moves into a real library |

Motif stays as decided: the SGI libXm is lifted; the from-source 1.2 build is the oracle and
the fallback if a hazard cannot be closed.

## 4. What the lifter still has to deliver

These are properties of the output, each with a test, independent of any one program.

| item | today | needed |
|---|---|---|
| exported functions | internal convention (8 integer slots, FP side channel lift_fpa/lift_fpr) | true cdecl for every exported symbol with float/double or 64-bit parameters; prototypes from headers; internal functions keep the fast convention |
| FP side channel | globals | thread-local, so sproc share groups work as threads |
| unsupported functions | 21 abort stubs in xwsh (64-bit incoming slots, branch leaves, dsll) | zero; an abort stub is a build error for a flipped program |
| mixed-width accesses | detector exists (LIFT_DET, NM_SHADOW) | every reported site resolved from types or proven benign; hazard count per library recorded in evidence/ |
| struct layout | not checked | double and long long align to 8 on N32 and O32, 4 on i386: from-source i386 libraries are built with -malign-double, host structs that lifted code touches are audited with nm_layout_diff.py and listed if they differ |
| target | gcc -m32 | -m32 for N32 and O32, -m64 for N64; the 64-bit incoming slot class is handled in the entry stubs; output stays buildable with -mx32 |
| DSO output | one program at a time, shim linked statically, objcopy symbol renames | one .so per IRIX DSO, IRIX soname, exported symbol list equal to the original (checked against the IRIX dynsym) |
| init/fini, C++ static constructors | run by a hand-written main wrapper | emitted as DT_INIT/DT_FINI of the lifted object |
| setjmp/longjmp, signal handlers, alloca | untested | one test each in nativemem/tests |
| O32 | none | a second front end (argument passing, FP register pairs, 32-bit registers). 389 programs, among them the help viewer, iconsmith, showcase, Photoshop |

## 5. Boundaries between guest and native processes

Anything here that is big-endian or IRIX-laid-out on one side breaks when only one side flips.

| boundary | risk | handling |
|---|---|---|
| X protocol | none (Xlib swaps) | - |
| X properties and selections, format 16/32 | none (server swaps) | - |
| X properties and selections, format 8 holding binary structs | byte order and layout | inventory per program from the xproxy log; flip the programs that share one together, or fix the format in the native side as explicit big-endian |
| files written as raw structs (desktop state, compiled FTR database, icon catalog pages, objectserver, .web history.db, message catalogs, locale tables, UIL .uid) | byte order | rule: the on-disk format stays what IRIX wrote (big-endian). The native reader converts at the read/write call for the known formats; catgets and locale come from the shim, not from lifted code |
| pipes/sockets with binary protocols (fam is text; mediad, objectserver, sysadm, rpc/XDR) | byte order outside XDR | same rule, per protocol |
| System V IPC, shared arenas (usinit) between processes | layout and byte order of shared memory | programs sharing an arena flip together |
| exec() | a native program execs a MIPS one and the reverse | see section 6 |
| /proc, ioctl numbers, termio | constants | native side uses IRIX numbers only inside the libc shim |

## 6. How both worlds coexist on the host root

- **Library directories.** As on IRIX: O32-derived in /usr/lib and /lib, N32-derived in
  /usr/lib32 and /lib32 (both i386, kept apart by DT_RUNPATH, see section 1), N64-derived as
  x86_64 in /usr/lib64 (libc.so.1 next to libc.so.6).
- **Blocker to clear first:** the host /usr/lib32 currently holds 37 x86_64 objects that are
  bundle components linked against libirixre (libXm.so.1, libX11.so.1, ...). They sit on the
  paths the native libraries need. They move to a private directory of the runtime
  (/usr/lib/irixre/lib32) and the runners get that directory as their search path.
- **Programs.** A flipped program replaces the launcher at its IRIX path. The MIPS original
  stays in IRIX-TREE. Unflipped programs keep going through irix-run.
- **exec of unflipped programs.** Register the MIPS N32/O32 ELF header with binfmt_misc and
  point it at irix-guest-exec. Then a MIPS original can sit at its real path and any exec(),
  system() or popen() from a native program just works. This is one line of configuration,
  not runtime machinery, and it disappears with the last MIPS binary.
- **Rollback.** Every flip keeps the previous launcher beside it (name.guest), so one program
  can be put back without touching anything else.

## 7. Checking a flip

The guest runtime runs the same program on the same data, so every flip is a comparison:

1. X request stream: irix-re/libSgm/tools/xproxy.py in front of both, same script of input
   (xdotool on a private server), diff of the decoded drawing requests. This is how SgDial was
   made exact; it works for whole programs.
2. File and syscall behaviour: IRIX_TRACE_SYSCALLS (now with paths) against strace of the
   native build, compared as (call, path, result).
3. Screenshots of the private display at fixed points, compared pixel for pixel.
4. Hazard detector build (LIFT_DET) run through the same script: no unresolved site reached.
5. Layout check of every struct that crosses into a host or from-source library.

A program counts as flipped when 1 to 4 are clean for its script and it has run in the user's
session for a week without a report.

## 8. Order of work

Coverage numbers: how many of the 1465 N32 programs have their whole DT_NEEDED closure
available after each step.

| phase | delivers | programs covered | exit test |
|---|---|---|---|
| 0 | lifter and shim committed on both disks; inventory and comparison rig (section 7) scripted; /usr/lib32 cleared of runtime objects; binfmt_misc entry; i386 libGL and ALSA installed through Portage | - | rig compares guest xclock with guest xclock and reports no difference; an i386 program linked with RUNPATH /usr/lib32 finds the IRIX-soname libX11 stub there |
| 1 | libc.so.1 (IRIX source over the Linux syscall layer, see decision 6) and the eoe libraries from source; base commands from eoe/cmd built natively instead of lifted; lifter items of section 4 that a C program needs; xwsh finished | 575 | xwsh native is the session's terminal; hinv, ls-class tools from the tree run native |
| 2 | X family under IRIX sonames (host libs), libgen, libm | 745 | R5 clients from the tree (xclock, xterm class) native, compared |
| 3 | libC.so.2 and libCsup lifted; C++ constructors, virtual calls, new/delete | 798 | a C++ Xlib program from the tree, compared |
| 4 | libXt from source installed as libXt.so; libXm lifted from the SGI binary against it; hazard list for libXm closed | 833 | a plain Motif program (Xm only) compared; native subclass test passes against the lifted libXm |
| 5 | libSgm, libvk, libXpm, libimd*, libdesktop*, libhelpmsg, libfam client | 838 + desktop | toolchest, then iconcatalog, then fm, then 4Dwm flip one at a time |
| 6 | libgl over OpenGL, libaudio over ALSA, libdmedia, libfm (font manager), libil/libifl | 1020 | colour chooser (IRIS GL field), soundplayer-class tools |
| 7 | libsa*, libwebviewer, sysadm stack | rest of N32 | sysmgr native (no libCsup/libwebviewer clash: ld.so places them) |
| 8 | O32 front end and O32 libc/Xm chain | 389 O32 programs | help viewer, iconsmith |
| 9 | x86_64 libraries from N64 (libc shim, Xt LP64, Xm, Sgm, vk) and the 14 N64 programs | libraries mostly | a native 64-bit Motif program links and subclasses |
| 10 | remove bundle runners, irix-run, libirixre from the session; guest-exec stays as a tool | - | session starts with no libirixre mapped in any process |

Phases 1 to 4 are a straight line. From phase 5 on, library work and program flips can
overlap, and 6 and 7 are independent of each other.

## 9. Decisions this plan assumes (say so if any is wrong)

1. Multilib like IRIX in layout; N32 and O32 build as i386 now, N64 as x86_64; x32 for N32 is
   a later rebuild, not a prerequisite (section 1; user decisions 2026-11-09).
2. The lifter is the single lowering tool; nativemem supplies types and tests (rule 5).
3. On-disk and on-wire formats stay big-endian IRIX formats; the native side converts
   (section 5). The alternative, converting all data once at install time, breaks every
   program that has not flipped yet.
4. Native libraries take the real IRIX paths; runtime objects move out of the way (section 6).
5. binfmt_misc for unflipped MIPS programs is acceptable as the one piece of glue.
6. **glibc underneath (user decision 2026-11-09).** libc.so.1 is IRIX-source code layered on
   glibc where layouts or behaviour differ (stdio and FILE, locale and ctype, catgets, pwd,
   utmpx, termio, signals) and forwards everything else. Reasons: every glibc program on the
   system keeps working, and IRIX programs can load the host X and GL libraries in-process, so
   they get hardware acceleration. Known cost: not the most faithful; two stdio implementations
   in one process, so a FILE pointer never crosses between IRIX code and a host library.

## 10. N64 libXm to x86_64 (decided 2026-11-09, work starts that evening)

Why: N64 is LP64 like x86_64, so struct layouts of the lifted library equal a native 64-bit build, and a
64-bit Motif with the SGI look can share a process with 64-bit host libraries (Qt for a Prism front end,
host GL).  The tree has the whole N64 Motif stack under usr/lib64: libXm (2987 KB), libXt, libX11, libXmu,
libXpm, libgen, libSgm, libvk, libC, libCsup.

Survey of usr/lib64/libXm.so.1 (read-only, before any code):
- ELF64, big-endian, mips3 (flags 0x20000002), PIC, prelinked (MIPS_BASE_ADDRESS 0xc760000), two LOAD segments.
- Relocations: 3316 R_MIPS_REL32 and nothing else, in .rel.dyn entries of the ELF64 MIPS form (three types per
  entry; only the first is used here).  Same single relocation kind as the N32 library, so the data side of
  the lifter carries over once the 64-bit entry format is read.
- Same section family as N32: .MIPS.stubs, .got with 38 local entries, 1089 exported functions, DWARF line and
  frame sections, and a .MIPS.Xlate section (version 6 here, N32 is version 5): the table reader has to be
  extended before function starts are right, exactly as for the N32 library.
- Needs libXt, libX11, libc, libgen: the same host-side set as the N32 build.

What the lifter needs (new front end, not a patch): an ELF64 reader (headers, program headers, dynamic and
symbol entries are 64-bit), 64-bit register semantics (registers are uint64_t, pointers are 64-bit: no hi/lo
pair tracking, ld/sd/daddu are plain), N64 calling convention (eight argument registers, 64-bit values in
one), the version 6 xlate table, and output built with gcc -m64.  Mixed-width hazards in data stay the same
kind of problem but with LP64 layouts.
