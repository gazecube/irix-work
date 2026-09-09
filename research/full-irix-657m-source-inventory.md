# IRIX 6.5.7m source tree full inventory

Date: 2026-09-08
Source: calmsacibis995/irix-657m-src
Source root tree: 822f4fcb99524a2fa03747acc4c5946eeb63ea3c
Status: first exhaustive structural/semantic pass complete

## Scope and method

This pass treats the source tree itself as an archaeology target. The rule for this inventory is that no top-level source area is silently ignored. Every root entry and every first-class build subtree is accounted for. Large subtrees are classified at module/directory level, and obscure areas are retained for later deep inspection rather than discarded.

The authoritative SGI Makefiles are used wherever possible because they identify the directories SGI actually built, exported, installed, and turned into product images. Git tree listings are used to catch directories that are present but not in a default build.

Classification vocabulary:

- USERLAND: commands and execution-environment programs
- LIBRARY: runtime or development library
- KERNEL: IRIX kernel proper
- FILESYSTEM: kernel filesystem implementation
- DRIVER/HARDWARE: hardware-specific kernel or standalone code
- FIRMWARE: PROM/ARCS/controller firmware
- BUILD/HOST TOOL: build-time generator, converter, parser, packager, etc.
- INSTALL/IMAGE: ISM, IDB, distribution, miniroot, installation or release-image machinery
- DOCUMENTATION: manuals, books, release notes, internal engineering documentation
- TEST/DIAGNOSTIC: tests, diagnostics, instrumentation, stress or debug tools
- DATA/ASSET: generated/static data or artwork
- UNKNOWN/REVIEW: retained in the inventory and requiring a later source-level semantic pass

Portability vocabulary:

- EASY: mostly libc/POSIX source, likely small modern-host changes
- MINOR: old compiler/build assumptions or modest API differences
- COMPAT: IRIX APIs should be isolated behind a compatibility layer
- MAJOR: substantial kernel/platform behavior must be supplied by the host
- HARDWARE: useful primarily for preservation, emulation, firmware work, or hardware-specific compatibility

## Repository root - complete

The repository root contains exactly:

- `README.md` - repository description/release context
- `007-3897-006.pdf` - SGI document artifact
- `eoe/` - execution environment / userland / libraries / docs / distribution images
- `irix/` - kernel, kernel-coupled commands/libraries, headers, manuals and IRIX images
- `stand/` - standalone software: ARCS/PROM plus x86 system-controller firmware and standalone image machinery

There are no other root source trees in this revision.

## eoe/ - complete top-level map

Direct children:

- `Makefile`
- `books/`
- `build/`
- `cmd/`
- `docs/`
- `include/`
- `lib/`
- `man/`
- `pages/`
- `relnotes/`
- `sa/`
- `targets/`

The top-level EOE Makefile is a complete ISM build pipeline. It defines `headers`, `exports`, `install`, `rawidb`, `ism`, and `images`. EOE is therefore not just a command-source collection; it is a substantial source distribution for building IRIX execution-environment software and release images.

### eoe/build - INSTALL/IMAGE

The EOE image build explicitly produces:

- `eoe_root`
- `eoe_toolroot`
- `eoe_eoe`
- `eoe_noship`
- `dba`
- `pcp_eoe_root`

This uses SGI ISM/IDB/spec machinery. Preserve it as a historical release-engineering subsystem.

### eoe/cmd - USERLAND, BUILD/HOST TOOL, ADMIN, NETWORK, STORAGE

The authoritative `eoe/cmd/Makefile` build list is:

`CMWsession abicmd acct acctcvt adm alpq apitools attr attrinit autopush awk banner basename bc bdiff bfs bison bloatview bootp bru bsd bstream bufview cal calendar cat cdplayer check chkconfig cksum chacl chcap chlabel chroot chmod chown chproj cmp cmplrs coffcheck col comm cpio cron crypt csh csplit cu cut date dd deroff dev df dhcp_server diff dircmp dirname diskpatch dkstat dlook dplace dvhtool du ecc echo ed egrep env efs evctr expand expr exstr failover factor false fam fcman fdetach fgrep file find flex fmtmsg fp fsr fstyp ftimer fuser fpmode fx gencat getabi getconf getopt gettapedev gettxt getty gr_osview grep grpck groups grelnotes gzip head id idbg init initpkg Install ioconfig ipc iconv iconv_comp gnum4 join kbdload kbdpipe kbdset kill killall ksh latenscope lex line link listen locale localedef lockstat log login logname ls Mail m4 maclbldevs mail mailx make makekey man mesg message_system messages mediad miser mkboottape mkdepend mkdir mkfifo mkfs mkmsgs mknod ml more mountcd mountcdda mountdos mounthfs mpadmin mt mv netsnoop newform newgrp newlabel newproj news newsess nfsunhang nice nl nohup npri ns nsort nvram numa oawk od odiff osview pack par passmgmt passwd patch pathchk pawk pax pcp perl5 pg paste pfmt pmake ppp preg printf pr priocntl priotest proclaim profiler prtvtoc ps pwck pwconv pwd rcs react recover regcmp regress regview relnotes renice rfind rm rmdir rmt rpcbind rtmon runon sa saf sat sccs script scsiadminswap scsicontrol sdiff sed osendmail setmnt setsym sh sid sleep slip sort srchtxt spell split stacker stat strings stty su suattr sulogin sum sun swap sync sysinfo sysmeter systune sadmin sss tabs tail tar tcsh tee test time tlink touch tr true tset tsix tsort tty top topology topwait uadmin ul uname unexpand unifdef units uniq unlink unpack utmpadmin uucp vi wakeupat wall wc wchrtbl whereis who whodo whoami write xactl xargs xdiff xfs yacc`.

Important port groups:

EASY/MINOR historical UNIX baseline:
`awk banner basename bc cat cksum chmod chown cmp comm cpio csh csplit cut date dd diff dirname du echo ed egrep env expand expr false fgrep file find grep head id ksh ls m4 mkdir mkfifo more mv nice nl nohup od paste printf pwd rm rmdir sed sh sleep sort split strings stty sum sync tail tar tcsh tee test time touch tr true tty uname unexpand unifdef units uniq unlink vi wc whereis who whoami xargs`.

COMPAT SGI-flavored system tools:
`chkconfig dplace dvhtool fx getabi gr_osview hinv ioconfig idbg lockstat mpadmin netsnoop npri nvram numa osview priocntl prtvtoc rtmon runon sysinfo sysmeter systune topology`.

Storage/filesystem/media administration includes EFS, XFS, disk/volume, SCSI, CD/HFS/DOS mounting, `fx`, `fsr`, `fstyp`, `mkfs`, `dvhtool`, `prtvtoc`, and related tools.

Development/toolchain material includes `awk`, `bison`, `flex`, `lex`, `yacc`, `m4`, `gnum4`, `make`, `pmake`, `mkdepend`, `coffcheck`, `rcs`, `sccs`, `regcmp`, `strings`, and `unifdef`.

Networking includes BOOTP, DHCP server, PPP, SLIP, UUCP, RPC, DLPI-facing tools, packet/network inspection, NFS support and related daemons/utilities.

This subtree alone is sufficient to justify a dedicated `irix-userland` port project.

### eoe/lib - LIBRARY

Exported/build libraries from the authoritative Makefile include:

`libcurses libnsl libadm libattr libcmd libcomp libcrypt libcvt libdb libds libdisk libdl libdm libfam libfrs libgen libiaf libl liblim libmalloc libmdbm libmediaclient libmediad libmutex libnanothread libnls libpcp libpcp_dev libpcp_pmda libpkg libpthread librmt librpcsvc librt librtmon libsatmp libspypt libsun libtserialio libuli libusio libw libxnet liby ns sss`.

Additional build subtrees include:

`libcursesBSD terminfo libudma libplot libsocket nametoaddr libabi libdlook libdplace libdprof`.

This means the tree contains much of the support library layer needed to port the command tree without immediately replacing every SGI dependency with a foreign implementation.

High-value targets include `libcurses`, `libnsl`, `libattr`, `libdisk`, `libfam`, `libpthread`, `librt`, `librtmon`, `libsocket`, `libxnet`, and the dplace/dlook/dprof support libraries.

### eoe/include - HEADERS / ABI REFERENCE

This is a large public-header tree, not a token include directory. Directly observed headers/subtrees include standard/POSIX interfaces and IRIX extensions such as `aio.h`, `arpa/`, `assert.h`, `capability.h`, `clearance.h`, `cpio.h`, `crypt.h`, `ctype.h`, `dbm.h`, `des/`, `dirent.h`, `diskinfo.h`, `diskinvent.h`, `dlfcn.h`, `dpipe.h`, `errno.h`, `fam.h`, `fcntl.h`, `fetchop.h`, `fmtmsg.h`, `fnmatch.h`, plus many more.

Treat this as the ABI/API reference base for userland portability, not merely build debris.

### eoe/books - DOCUMENTATION / ORIGINAL USER MANUAL SOURCES

This is substantial source material for SGI manuals and online help. Direct book trees include:

`CPR_OG Combiner_UG CrayMsg_PG DevDriver_PG Diskless_AG ESP_OV ESP_UG FlatPanel_Help IA_BakSecAcc IA_ConfigOps IA_DiskFiles IA_InstLicns IA_NetwkMail IA_Periphrls IA_Resource IIDsktp_IG IRIX_NetPG NIS_AG ONC3NFS_AG OViewHelp PCP_IRIX PmRunHelp PmTimeHelp REACT_PG TCMW_AG TCMW_UG T_IRIX_Prog UI_Glines WorkGrp_AG XFSMHelp XFS_AG`.

`IIDsktp_IG` is especially relevant to IID archaeology. `DevDriver_PG`, `T_IRIX_Prog`, `XFS_AG`, networking/admin guides, and help-source trees are valuable behavior documentation for reconstruction and compatibility work.

### eoe/docs - DOCUMENTATION / INTERNAL SGI ENGINEERING DOCS

The included README explicitly says this is the IRIX ISM internal document directory, that documents here are for internal SGI use, and that the hierarchy mirrors the source tree so technical writers can find engineering material near the corresponding code. Current top-level children are:

- `frame_templates/`
- `kern/`
- `posix/`

This is a major archaeology source and should be searched before reverse-engineering undocumented kernel/userland behavior.

### eoe/man - DOCUMENTATION

Manual-page source tree. Use as a behavioral oracle for command-line options, exit behavior, configuration formats and administration interfaces.

### eoe/pages - DOCUMENTATION / GENERATED OR PAGE-ORIENTED MATERIAL

Retain as a separate documentation source area. Do not collapse into `man` until formats and generation flow are mapped.

### eoe/relnotes - DOCUMENTATION

Release-note source/material. Useful for version-specific behavior and identifying 6.5.7 changes relative to earlier IRIX revisions.

### eoe/sa - INSTALL/IMAGE

Contains installation/miniroot and software-manager support material:

- `idbs/`
- `inst.README.eoe`
- `inst.init.eoe`
- `proto.mr`
- `swmgr.README.eoe`
- `swmgr.README.html.eoe`

This is part of system-image/install construction, not ordinary userland.

### eoe/targets - INSTALL/IMAGE / BUILD OUTPUT DEFINITIONS

Product/target definitions for EOE build/image generation. Preserve with the ISM build machinery.

## irix/ - complete top-level map

Direct children:

- `Makefile`
- `build/`
- `cmd/`
- `include/`
- `kern/`
- `lib/`
- `man/`
- `targets/`

The top-level IRIX Makefile is another complete ISM pipeline with `headers`, `exports`, `install`, `rawidb`, `ism`, and `images`. Its build order is `include lib cmd kern man build`.

### irix/build - INSTALL/IMAGE

Produces:

- `irix_root`
- `irix_toolroot`
- `irix_eoe`
- `irix_noship`
- `dba`
- `dfs`

It also has overlay handling for `o256p`, demonstrating release/image specialization in-tree.

### irix/cmd - KERNEL/PLATFORM ADMIN USERLAND

The authoritative build list is:

`bsd cached clshm cpr diskless dlpi dprof flash flashio flashmmsc hinv icrash lboot linkstat mkmachfile mkpart mmscd netman onlinediag perfex savecore smt sn0log sn0msc snmp stress sysctlrd tokenring xbstat xfs xlv xfsm nvlog`.

This is much more kernel/platform coupled than `eoe/cmd`.

High-value archaeology/port targets:

- `hinv` - system inventory presentation and SGI inventory interfaces
- `icrash` - crash/kernel analysis
- `lboot` and `mkmachfile` - kernel/system configuration/build machinery
- `savecore` - crash dump handling
- `perfex`, `dprof` - performance/profiling
- `xfs`, `xfsm`, `xlv` - XFS and volume-management administration
- `flash`, `flashio`, `flashmmsc` - firmware flash/update path
- `mmscd`, `sysctlrd`, `sn0msc`, `sn0log`, `xbstat` - high-end system/controller/platform tools
- `clshm` - cluster shared-memory subsystem userland
- `onlinediag`, `stress` - diagnostics/test

### irix/lib - LIBRARY

Authoritative exports:

`libc libbsd libcpr libfetchop libgrio librsvp librestart libtoolroot klib`.

Additional built libraries:

`libirixpmda libperfex`.

A `libclshm` source area is explicitly noted by the Makefile as present but not currently released/default-built. This is an example of why inventory must include non-default source too.

The presence of IRIX libc source is one of the highest-value findings for userland and ABI archaeology.

### irix/kern - KERNEL

Complete direct kernel module map:

- `bsd/`
- `btool/`
- `cell/`
- `fs/`
- `io/`
- `ksys/`
- `master.d/`
- `ml/`
- `mtune/`
- `os/`
- `sgi/`
- `stubs/`
- `sys/`
- kernel build files including `kcommondefs`, `kcommonrules`, `protoklocaldefs`

The kernel Makefile builds `os sgi fs io bsd ml master.d stubs mtune btool`, conditionally includes CELL, and builds product-specific kernels plus miniroot/debug variants. It also includes branch instrumentation support.

#### irix/kern/fs - FILESYSTEM

Complete direct filesystem list:

`autofs cachefs cell cfs dfs efs fdfs fifofs hwgfs lofs namefs nfs pipefs procfs specfs umfs xfs`.

This includes actual EFS and XFS kernel implementations, NFS, procfs, hardware-graph filesystem support, cachefs/autofs, distributed/cell-related filesystems and classic pseudo-filesystems.

#### irix/kern/io - DRIVER/HARDWARE

This is a very large driver/I/O tree. The direct listing already shows 1394/FireWire (including a `1394.DELETED` historical tree), Adaptec/Fibre Channel material, SCSI/disk code, DLPI, DMI, serial, console, datapipe, failover, system-controller interfaces and numerous machine/device drivers. This subtree must be treated as a hardware-driver corpus, not a single module.

#### irix/kern/os - KERNEL CORE

Contains process, address-space, credential/security, accounting, capability/MAC, clock, exec, fork, file-descriptor, locking, VM/page, inventory and other core OS implementation. Directly observed areas include `as/`, `cell/`, `cpr/`, `file/`, `host/` plus core source files for exec/fork/exit, credentials, capabilities, accounting, paging and inventory.

#### irix/kern/ml - MACHINE LAYER

Architecture/machine-dependent implementation and low-level MIPS/platform code. HARDWARE/MAJOR for any non-MIPS kernel-port work.

#### irix/kern/bsd - KERNEL NETWORK/BSD LAYER

BSD-derived networking/kernel interfaces and related compatibility code.

#### irix/kern/cell - KERNEL DISTRIBUTED/CELL

Distributed-kernel/cell infrastructure used on scalable SGI systems. Preserve as a separate architecture area.

#### irix/kern/sgi, ksys, sys

SGI-specific kernel implementation and kernel-facing interface/header trees. These are major references for undocumented IRIX interfaces consumed by userland.

#### irix/kern/master.d and mtune

Kernel configuration/master device definitions and tunable definitions. These are part of the IRIX kernel configuration/build system and directly relate to `lboot`, `systune`, system files and generated kernel configuration.

#### irix/kern/btool

Kernel branch instrumentation support. The top kernel Makefile contains explicit workflows for instrumented `unix.kbtool` builds.

### irix/include - HEADERS / ABI / KERNEL INTERFACE

Kernel/IRIX-specific exported headers. Treat together with `eoe/include` as primary API/ABI evidence.

### irix/man - DOCUMENTATION

Manual pages for kernel/platform-specific tools and interfaces.

### irix/targets - INSTALL/IMAGE / BUILD

Target/product definitions for IRIX ISM/image generation.

## stand/ - complete top-level map

Direct children:

- `Makefile`
- `arcs/`
- `build/`
- `targets/`
- `x86/`

The existence of `stand/x86` is important: the standalone tree is not MIPS-only.

### stand/arcs - FIRMWARE / BUILD/HOST TOOL / DIAGNOSTIC

Complete direct directory list:

`IO4prom IO6prom IP19prom IP20prom IP20util IP21prom IP22audio IP22prom IP22util IP25prom IP26prom IP26util IP27prom IP28prom IP28util IP30prom IP30util IP32prom IPXXprom arcstests dmon elsc everest_sysctlr ide include lib sash scripts symmon tools xbox`.

Build/config files include `ENDIANdefs MIPS64defs MIPSIPAdefs MIPSN32defs MIdefs R1000064defs R400064defs R4000N32defs R4000defs TFP64defs commondefs commonrules netdbxdefs newvers.sh`.

Product PROM source therefore spans multiple generations/machines: EVEREST/IO4, IO6, IP19/IP20/IP21/IP22/IP25/IP26/IP27/IP28/IP30/IP32 and a generic IPXX area.

Other major components:

- `lib/` - standalone libraries including libsk/libsc and machine/product support
- `sash/` - standalone shell
- `ide/` - integrated diagnostics environment
- `symmon/` - standalone symbolic monitor/debugger
- `arcstests/` - ARCS tests
- `dmon/` - diagnostic/debug monitor material
- `elsc/` and `everest_sysctlr/` - controller firmware/support
- `IPxxutil` areas - machine-specific standalone utilities
- `IP22audio/` - standalone audio-related machine support
- `xbox/` - present and retained for deep semantic review; do not omit because it is not in the ordinary product DIRS list

The top ARCS Makefile has product-dependent build graphs and explicitly builds combinations of `tools`, `lib`, `sash`, `ide`, `symmon`, `IO4prom`, `IO6prom`, `IP27prom`, `IP30prom`, `IP32prom`, etc. It contains full workflows for IP27 and IP30 PROM binary generation.

### stand/arcs/scripts - BUILD/HOST TOOL / DATA

Known inventory:

`Makefile bdftohtp.c frompcbm fromspans gconf graytospans.c graytoxbm.c topcbm.c xbmtohtpbm.c xbmtospans.c images/`.

`images/` includes original `indigo.bw` and `reality.bw` artwork.

These tools generate textport fonts/bitmaps, SGI span logos, color PROM bitmap material and standalone configuration tables.

### stand/arcs/tools - BUILD/HOST TOOL / PROM PRODUCTION

Known complete direct tool set:

`convert dataio eawrite elf2coff hwreg myacc promcvt promgen promice squash` plus its Makefile.

The default tool build includes `convert promcvt promgen hwreg myacc`; the remaining directories are still preserved and must be investigated rather than ignored.

Established roles:

- `promgen` - later SGI PROM image assembler, multi-segment image format, checksums/compression/header inspection
- `promcvt` - older EVEREST/IO4 PROM packager
- `convert` - object/image conversion framework used by real IP27/IP30 PROM production
- `myacc` - SGI modular yacc used to generate the IP27 PROM parser without global parser state
- `elf2coff` - ELF/COFF conversion boundary
- `dataio`, `eawrite`, `promice`, `hwreg`, `squash` - retained as first-class standalone engineering/tooling areas requiring their own source-level pass

### stand/x86 - FIRMWARE / CONTROLLER SOFTWARE

Direct children:

- `Makefile`
- `README`
- `commondefs`
- `ffsc/`

The README states that this subtree contains standalone software running on x86-based platforms, such as high-end system controllers.

This is a major finding because the source release already contains SGI standalone x86 code.

### stand/x86/ffsc - FIRMWARE / VxWorks / CONTROLLER UI

Direct material includes:

- `cdi/`
- `gui/`
- `html/`
- `jpeg/`
- `sgi/`
- `test/`
- `wind/`
- `Makefile.sgi`, separate `makefile`
- controller firmware images encoded as `mmscfw.bin.uu` and a pre-1.0 variant
- prototype boot/build definitions and a Windows batch environment file

Its README explicitly warns that this source does not build under IRIX and must instead be built with the VxWorks development environment.

This is SGI x86-based high-end system-controller firmware/software, including GUI/HTML/JPEG components and VxWorks integration. It deserves a dedicated archaeology pass because it may contain reusable x86 portability patterns and controller protocols relevant to high-end SGI systems.

### stand/build and stand/targets - INSTALL/IMAGE / BUILD

Standalone ISM/image construction and target definitions. Preserve as part of the complete standalone firmware release pipeline.

## What the tree actually is

The old shorthand "the leaked IRIX kernel source" is materially incomplete.

This repository contains, at minimum:

1. A substantial historical UNIX/IRIX userland.
2. A large supporting library and public-header corpus, including IRIX libc source.
3. The IRIX kernel with filesystem, networking, machine, driver, scalability and system-configuration source.
4. EFS and XFS kernel implementations plus XFS/EFS administration userland.
5. Full ARCS/PROM source for many SGI product generations.
6. PROM graphics/textport code, diagnostics, sash and symmon.
7. SGI's original standalone host generators and PROM production tools.
8. Complete ISM/IDB/spec image-production machinery for EOE and IRIX.
9. Miniroot/software-manager/install support.
10. User manuals, online help, release notes and internal SGI engineering documentation.
11. Standalone x86/VxWorks system-controller firmware source under `stand/x86/ffsc`.
12. Test, stress, diagnostics, profiling, crash-analysis and instrumentation source.

In other words: this is not merely kernel source. It is a partial but broad SGI operating-system, firmware, documentation and release-engineering source environment.

## Immediate project tracks created by this inventory

### A. IRIX userland port

Start with easy historical utilities, then shared compatibility infrastructure, then SGI-specific tools. Preserve original command behavior and test against real IRIX binaries/systems.

Recommended bootstrap:

`basename dirname echo env false true pwd mkdir rmdir sleep tee touch tty uname uniq wc whoami`

Then:

`ls ps df du find file more grep sed awk sh csh ksh vi tar cpio`

Then SGI-specific:

`hinv chkconfig sysinfo nvram osview gr_osview mpadmin runon numa dplace systune dvhtool fx`.

### B. Original SGI library compatibility

Inventory/build `eoe/lib` and `irix/lib` in dependency order. Prefer preserving SGI libraries/interfaces where practical instead of deleting dependencies from historical commands.

### C. Original documentation oracle

Index `eoe/books`, `eoe/docs`, `eoe/man`, `eoe/relnotes`, `irix/man`, and the root PDF. Search these before inventing behavior in IID, userland or kernel-interface compatibility code.

### D. ARCS firmware development environment

Continue porting original generators and PROM tools, now explicitly treating `stand/arcs` as a firmware SDK/build environment rather than only a firmware source subtree.

### E. x86 controller archaeology

Deep-inspect `stand/x86/ffsc` and its VxWorks build. Establish target hardware, CPU assumptions, protocols, GUI stack, relationship to MMSC/FFSC tooling and any portable x86 platform code.

### F. Release/image archaeology

Preserve and document EOE/IRIX/stand ISM, IDB, specs, targets and miniroot paths. These may eventually allow historically faithful packaging of ported components rather than ad hoc install scripts.

## Rules going forward

- Before reconstructing an IRIX component, search this source tree first.
- Before guessing an undocumented interface, search `eoe/docs`, manuals/books, headers, binaries and source.
- Default to pristine source plus compatibility boundaries.
- Do not delete or ignore non-default-build directories; classify them explicitly.
- Keep firmware, userland, kernel, documentation and release tooling as separate but cross-referenced workstreams.
- For historical commands, preserve IRIX output/options/quirks rather than normalizing them to GNU behavior.
- For hardware-only code, preserve and document it even when no modern-host port is sensible.

## Follow-up deep passes

The structural pass above accounts for every root and first-class build area. The next layer is source-level semantic indexing inside each very large module, especially:

- every `eoe/cmd/*` command: dependencies, APIs, portability score
- every `eoe/lib/*` and `irix/lib/*`: dependencies and ABI role
- every `irix/kern/io/*` driver family
- every `irix/kern/os/*` subsystem
- every `stand/arcs/lib/*`, `ide/*`, `symmon/*`, PROM and utility tree
- every `stand/arcs/tools/*` previously marked for review
- every `stand/x86/ffsc/*` component
- documentation cross-index from source modules to internal docs/manuals

Nothing in those areas is to be dropped from the index merely because it is obsolete, unbuildable, hardware-specific, non-default, or poorly understood.
