# Native libraries from source

Builds native i386 and x86_64 libraries from the UnixWare 7 source tree
(X11R5 Xt, OSF/Motif 1.2) instead of translating the IRIX binaries. The source
tree is not copied here; point UW7_SRC at it (default ~/uw7-src).

    native-libs/build-xt.sh -m32      # build/xt32/libXt.so.6
    native-libs/build-xt.sh -m64      # build/xt64/libXt.so.6

Status (2026-10-18)
- Xt: all 51 files compile for -m32 and -m64 with glibc (only fd.h needed a patch).
- -m32: tests/xt-subclass-test.c (a native Core subclass; initialize, expose, destroy) passes.
- -m64: links but crashes in that test (X11R5 assumes long/pointer == 32 bits in places) -
  needs an LP64 port.
- Not yet covered: Motif (lib/Xm), SGI extensions present in the IRIX libraries
  (libXt SGIScheme, SGI look in libXm, libSgm).
