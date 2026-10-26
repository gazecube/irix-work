/* IRIX <sys/capability.h>: 64-bit capability masks with IRIX bit positions,
 * implemented over Linux capabilities by libirixcompat (src/irix_compat.c). */
#ifndef _IRIX_SYS_CAPABILITY_H
#define _IRIX_SYS_CAPABILITY_H
#include <stddef.h>
#include <stdint.h>

#define CAP_SYS_DISABLED	0
#define CAP_SYS_SUPERUSER	1
#define CAP_SYS_NO_SUPERUSER	2

#define CAP_NOT_A_CID		0LL
#define CAP_CHOWN		(0x01LL << 1)
#define CAP_DAC_WRITE		(0x01LL << 2)
#define CAP_DAC_READ_SEARCH	(0x01LL << 3)
#define CAP_FOWNER		(0x01LL << 4)
#define CAP_DAC_OVERRIDE	(CAP_DAC_WRITE|CAP_DAC_READ_SEARCH|CAP_FOWNER)
#define CAP_FSETID		(0x01LL << 5)
#define CAP_KILL		(0x01LL << 6)
#define CAP_LINK_DIR		(0x01LL << 7)
#define CAP_SETFPRIV		(0x01LL << 8)
#define CAP_SETFCAP		CAP_SETFPRIV
#define CAP_SETPPRIV		(0x01LL << 9)
#define CAP_SETPCAP		CAP_SETPPRIV
#define CAP_SETGID		(0x01LL << 10)
#define CAP_SETUID		(0x01LL << 11)
#define CAP_MAC_DOWNGRADE	(0x01LL << 12)
#define CAP_MAC_READ		(0x01LL << 13)
#define CAP_MAC_RELABEL_SUBJ	(0x01LL << 14)
#define CAP_MAC_WRITE		(0x01LL << 15)
#define CAP_MAC_UPGRADE		(0x01LL << 16)
#define CAP_INF_NOFLOAT_OBJ	(0x01LL << 17)
#define CAP_INF_NOFLOAT_SUBJ	(0x01LL << 18)
#define CAP_INF_DOWNGRADE	(0x01LL << 19)
#define CAP_INF_UPGRADE		(0x01LL << 20)
#define CAP_INF_RELABEL_SUBJ	(0x01LL << 21)
#define CAP_AUDIT_CONTROL	(0x01LL << 22)
#define CAP_AUDIT_WRITE		(0x01LL << 23)
#define CAP_MAC_MLD		(0x01LL << 24)
#define CAP_MEMORY_MGT		(0x01LL << 25)
#define CAP_SWAP_MGT		(0x01LL << 26)
#define CAP_TIME_MGT		(0x01LL << 27)
#define CAP_SYSINFO_MGT		(0x01LL << 28)
#define CAP_NVRAM_MGT		CAP_SYSINFO_MGT
#define CAP_MOUNT_MGT		(0x01LL << 29)
#define CAP_QUOTA_MGT		(0x01LL << 30)
#define CAP_PRIV_PORT		(0x01LL << 31)
#define CAP_STREAMS_MGT		(0x01LL << 32)
#define CAP_SCHED_MGT		(0x01LL << 33)
#define CAP_PROC_MGT		(0x01LL << 34)
#define CAP_SVIPC_MGT		(0x01LL << 35)
#define CAP_NETWORK_MGT		(0x01LL << 36)
#define CAP_DEVICE_MGT		(0x01LL << 37)
#define CAP_MKNOD		CAP_DEVICE_MGT
#define CAP_ACCT_MGT		(0x01LL << 38)
#define CAP_SHUTDOWN		(0x01LL << 39)
#define CAP_CHROOT		(0x01LL << 40)
#define CAP_DAC_EXECUTE		(0x01LL << 41)
#define CAP_MAC_RELABEL_OPEN	(0x01LL << 42)
#define CAP_SIGMASK		(0x01LL << 43)
#define CAP_XTCB		(0x01LL << 44)
#define CAP_MAX_ID		44

#define CAP_ALL_ON		0x0ffffffffffffffeLL
#define CAP_ALL_OFF		0x0000000000000000LL
#define CAP_INVALID		0x0000000000000001LL

/* does capability mask 'a' contain every capability in 'b'? */
#define CAP_ID_ISSET(a, b)	(((a) & (b)) == (a))

typedef uint64_t cap_value_t;
typedef uint64_t cap_flag_t;
typedef uint64_t cap_flag_value_t;

struct cap_set {
	cap_value_t cap_effective;
	cap_value_t cap_permitted;
	cap_value_t cap_inheritable;
};
typedef struct cap_set cap_set_t;
typedef struct cap_set *cap_t;

#define CAP_EFFECTIVE	1
#define CAP_PERMITTED	2
#define CAP_INHERITABLE	4

#ifdef __cplusplus
extern "C" {
#endif
cap_t cap_init(void);
int cap_free(void *);
cap_t cap_dup(cap_t);
cap_t cap_from_text(const char *);
char *cap_to_text(cap_t, size_t *);
cap_t cap_get_proc(void);
int cap_set_proc(cap_t);
cap_t cap_acquire(size_t, const cap_value_t *);
int cap_surrender(cap_t);
int cap_envp(int, size_t, const cap_value_t *);
#ifdef __cplusplus
}
#endif
#endif
