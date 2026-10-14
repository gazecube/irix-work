#ifndef IRIXCOMPAT_H
#define IRIXCOMPAT_H
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned int major_t;
typedef unsigned int minor_t;
#define NDPRI 0
#define NDPHIMIN 0
#define schedctl(...) 0
#define mpin(a,b) 0
#define prctl(...) 0
#define PR_RESIDENT 0
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#define ifreq host_ifreq
#include <net/if.h>
#undef ifreq
#undef ifr_name
#define ifr_name ifr_ifrn.ifrn_name
struct ifstats {
    unsigned int ifs_ipackets;
    unsigned int ifs_opackets;
    unsigned short ifs_ierrors;
    unsigned short ifs_oerrors;
    unsigned int ifs_collisions;
};
struct ifreq {
    union { char ifrn_name[16]; } ifr_ifrn;
    union {
        struct sockaddr ifru_addr;
        struct ifstats ifru_stats;
        char ifru_pad[24];
    } ifr_ifru;
};
#define ifr_stats ifr_ifru.ifru_stats
#define ifr_addr ifr_ifru.ifru_addr
#define SIOCGIFSTATS 0x8999
int irix_ioctl(int fd, unsigned long req, ...);
#define ioctl irix_ioctl
#include <signal.h>
typedef struct sigaction sigaction_t;
#define NBPSCTR 512
#include <sys/statfs.h>
typedef void buf_t;
#define FSID_EFS "efs"
#define FSID_NFS "nfs"
#define FSID_XFS "xfs"
#define GETFSIND 1
#define SC_GETFREESWAP 1
#define SC_GETSWAPMAX 2
#define SC_GETSWAPVIRT 3
#define SC_GETSWAPTOT 4
#define SC_GETLSWAPTOT 5
#define SC_GETRESVSWAP 6
int irix_statfs(const char *path, struct statfs *buf, int len, int fstyp);
int irix_fstatfs(int fd, struct statfs *buf, int len, int fstyp);
int sysfs(int opcode, const char *name);
int swapctl(int cmd, void *arg);
#define statfs(a, b, c, d) irix_statfs(a, b, c, d)
#define fstatfs(a, b, c, d) irix_fstatfs(a, b, c, d)
#define K0BASE 0x80000000u
#define EFS_INOPBB 4
#define BBSHIFT 9
#define PR_SADDR 0x40
#define PR_SFDS 0x02
#define PR_NOLIBC 0x02000000

int sysmp(int cmd, ...);
pid_t irix_sproc(void (*entry)(void *), int flags, void *arg);
#define sproc irix_sproc
#define rcmd irix_rcmd

#endif
