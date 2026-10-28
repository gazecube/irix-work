/*
 * Force-included for the x86_64 build of the UnixWare Motif 1.2 sources.
 * The sources call malloc/calloc/realloc and several Motif helpers without a
 * declaration; the compiler then assumes "int" and truncates returned pointers on
 * LP64.  Declaring them here fixes every call site at once.
 */
#ifndef _XM_LP64_DECLS_H
#define _XM_LP64_DECLS_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Display.h>
#include <Xm/AtomMgr.h>
#endif
