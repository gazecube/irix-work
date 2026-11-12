/* Numbers of the IRIX system calls in the irix-on-gentoo kernel: one contiguous block from 1000
 * (~/Projects/irix-on-gentoo, patches 0009, 0011, 0015, 0016 use 1000-1019).  The libc layer issues the calls below
 * whether or not the running kernel has them yet; without them they fail with ENOSYS.  A kernel patch that adds one
 * must use the number given here, or change it here as well. */
#ifndef IRIX_SYSCALLS_H
#define IRIX_SYSCALLS_H
#define IRIX_NR_syssgi		1020
#define IRIX_NR_sysmp		1021
#define IRIX_NR_schedctl	1022
#define IRIX_NR_prctl		1023
#endif
