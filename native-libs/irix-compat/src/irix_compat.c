/*
 * libirixcompat: the IRIX library calls the init-chain commands use, over
 * Linux and the irix-on-gentoo kernel.  Built without irix_compat.h.
 *
 *   capabilities  IRIX 64-bit masks <-> Linux capget/capset/ambient
 *   pfmt/lfmt     SVR4 message output (default text, no catalogs)
 *   uadmin        reboot(2)
 *   syssgi        SGI_GETNVRAM/SGI_SETNVRAM, file backed for now
 *   sysinfo       SI_* strings from uname
 *   sysconf       _SC_CAP and _SC_MAC
 *   MAC, audit and clearance: absent, as on a non-trusted IRIX
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <linux/reboot.h>

#include <sys/capability.h>
#include <sys/mac.h>
#include <sys/uadmin.h>
#include <capability.h>
#include <clearance.h>
#include <pfmt.h>
#include <sat.h>
#include <sys/syssgi.h>
#include <sys/stropts.h>
#include <sys/sysmp.h>
#include <dirent.h>
#include <syslog.h>
#include <sched.h>
#include <time.h>
#include <stdarg.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/systeminfo.h>

/* ---- capabilities ---------------------------------------------------- */

struct cap_hdr { unsigned int version; int pid; };
struct cap_data { unsigned int effective, permitted, inheritable; };
#define CAP_VERSION_3	0x20080522

/* IRIX capability bit -> Linux capability number (-1: no equivalent).
 * 41..56 are the irix-on-gentoo kernel's extension bits (patch 0003). */
static const int lin[64] = {
	[1] = 0,	/* CHOWN */
	[2] = 1,	/* DAC_WRITE -> DAC_OVERRIDE */
	[3] = 2,	/* DAC_READ_SEARCH */
	[4] = 3,	/* FOWNER */
	[5] = 4,	/* FSETID */
	[6] = 5,	/* KILL */
	[7] = 55,	/* LINK_DIR */
	[8] = 31,	/* SETFPRIV -> SETFCAP */
	[9] = 8,	/* SETPPRIV -> SETPCAP */
	[10] = 6,	/* SETGID */
	[11] = 7,	/* SETUID */
	[12] = 41, [13] = 42, [14] = 43, [15] = 44, [16] = 45,	/* MAC_* */
	[17] = 48, [18] = 49, [19] = 50, [20] = 51, [21] = 52,	/* INF_* */
	[22] = 30,	/* AUDIT_CONTROL */
	[23] = 29,	/* AUDIT_WRITE */
	[24] = 46,	/* MAC_MLD */
	[25] = 14,	/* MEMORY_MGT -> IPC_LOCK */
	[26] = 21,	/* SWAP_MGT -> SYS_ADMIN */
	[27] = 25,	/* TIME_MGT -> SYS_TIME */
	[28] = 21,	/* SYSINFO_MGT -> SYS_ADMIN */
	[29] = 21,	/* MOUNT_MGT -> SYS_ADMIN */
	[30] = 21,	/* QUOTA_MGT -> SYS_ADMIN */
	[31] = 10,	/* PRIV_PORT -> NET_BIND_SERVICE */
	[32] = 53,	/* STREAMS_MGT */
	[33] = 23,	/* SCHED_MGT -> SYS_NICE */
	[34] = 19,	/* PROC_MGT -> SYS_PTRACE */
	[35] = 54,	/* SVIPC_MGT */
	[36] = 12,	/* NETWORK_MGT -> NET_ADMIN */
	[37] = 27,	/* DEVICE_MGT -> MKNOD */
	[38] = 20,	/* ACCT_MGT -> SYS_PACCT */
	[39] = 22,	/* SHUTDOWN -> SYS_BOOT */
	[40] = 18,	/* CHROOT -> SYS_CHROOT */
	[41] = 56,	/* DAC_EXECUTE */
	[42] = 47,	/* MAC_RELABEL_OPEN */
	/* 43 SIGMASK, 44 XTCB: none */
};

/* highest capability number the running kernel has: the irix-on-gentoo
 * kernel has the MAC/INF/etc. bits (up to 56), a stock kernel does not. */
static int last_cap(void)
{
	static int last = -1;

	if (last < 0) {
		FILE *f = fopen("/proc/sys/kernel/cap_last_cap", "r");

		last = 40;
		if (f) {
			if (fscanf(f, "%d", &last) != 1)
				last = 40;
			fclose(f);
		}
	}
	return last;
}

/* A few IRIX capabilities cover more than one Linux capability. */
static const struct { int bit, extra; } lin_extra[] = {
	{ 37, 26 },	/* DEVICE_MGT also covers vhangup() and tty setup: SYS_TTY_CONFIG */
};

static int mapped(int bit)
{
	if (bit < 1 || bit > CAP_MAX_ID || !(lin[bit] || bit == 1))
		return 0;
	return lin[bit] <= last_cap();
}

static uint64_t to_linux(uint64_t m)
{
	uint64_t r = 0;
	int b;

	for (b = 1; b <= CAP_MAX_ID; b++)
		if ((m >> b & 1) && mapped(b))
			r |= 1ULL << lin[b];
	for (b = 0; b < (int)(sizeof(lin_extra) / sizeof(lin_extra[0])); b++)
		if ((m >> lin_extra[b].bit & 1) && mapped(lin_extra[b].bit) &&
		    lin_extra[b].extra <= last_cap())
			r |= 1ULL << lin_extra[b].extra;
	return r;
}

static uint64_t to_irix(uint64_t l)
{
	uint64_t r = 0;
	int b;

	for (b = 1; b <= CAP_MAX_ID; b++)
		if (mapped(b) && (l >> lin[b] & 1))
			r |= 1ULL << b;
	return r;
}

static int lget(uint64_t *e, uint64_t *p, uint64_t *i)
{
	struct cap_hdr h = { CAP_VERSION_3, 0 };
	struct cap_data d[2];

	if (syscall(SYS_capget, &h, d) < 0)
		return -1;
	*e = d[0].effective | (uint64_t)d[1].effective << 32;
	*p = d[0].permitted | (uint64_t)d[1].permitted << 32;
	*i = d[0].inheritable | (uint64_t)d[1].inheritable << 32;
	return 0;
}

static int lset(uint64_t e, uint64_t p, uint64_t i)
{
	struct cap_hdr h = { CAP_VERSION_3, 0 };
	struct cap_data d[2];

	d[0].effective = (unsigned int)e;	d[1].effective = (unsigned int)(e >> 32);
	d[0].permitted = (unsigned int)p;	d[1].permitted = (unsigned int)(p >> 32);
	d[0].inheritable = (unsigned int)i;	d[1].inheritable = (unsigned int)(i >> 32);
	return syscall(SYS_capset, &h, d);
}

/* A cap_t from cap_get_proc() also remembers the exact Linux sets so that
 * cap_surrender() restores capabilities that have no IRIX bit. */
struct cap_priv {
	struct cap_set pub;
	int exact;
	uint64_t le;
};

cap_t cap_init(void)
{
	return calloc(1, sizeof(struct cap_priv));
}

int cap_free(void *p)
{
	free(p);
	return 0;
}

cap_t cap_dup(cap_t c)
{
	cap_t n = cap_init();

	if (n && c)
		*(struct cap_priv *)n = *(struct cap_priv *)c;
	return n;
}

cap_t cap_get_proc(void)
{
	uint64_t e, p, i;
	cap_t c;

	struct cap_priv *pr;

	if (lget(&e, &p, &i) < 0 || !(pr = calloc(1, sizeof(*pr))))
		return NULL;
	pr->exact = 1;
	pr->le = e;
	c = &pr->pub;
	c->cap_effective = to_irix(e);
	c->cap_permitted = to_irix(p);
	c->cap_inheritable = to_irix(i);
	return c;
}

int cap_set_proc(cap_t c)
{
	uint64_t e, p, i, ne, np, ni, amb;
	int b;

	if (!c)
		return errno = EINVAL, -1;
	if (lget(&e, &p, &i) < 0)
		return -1;
	ne = to_linux(c->cap_effective);
	np = to_linux(c->cap_permitted);
	ni = to_linux(c->cap_inheritable);
	if (lset(ne, np, ni) < 0)
		return -1;
	/* what is both permitted and inheritable survives exec as ambient */
	amb = np & ni;
	prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);
	for (b = 0; b < 64; b++)
		if (amb >> b & 1)
			prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, b, 0, 0);
	return 0;
}

/* raise capabilities in the effective set; returns the previous state */
cap_t cap_acquire(size_t n, const cap_value_t *v)
{
	uint64_t e, p, i, add = 0;
	cap_t old = cap_get_proc();
	size_t k;

	if (!old || lget(&e, &p, &i) < 0)
		return old;
	for (k = 0; k < n; k++)
		add |= to_linux(v[k]);
	lset(e | (add & p), p, i);
	return old;
}

int cap_surrender(cap_t old)
{
	uint64_t e, p, i;

	if (!old)
		return 0;
	if (lget(&e, &p, &i) == 0) {
		struct cap_priv *pr = (struct cap_priv *)old;

		lset((pr->exact ? pr->le : to_linux(old->cap_effective)) & p, p, i);
	}
	free(old);
	return 0;
}

/* are all the capabilities available to the process? */
int cap_envp(int flags, size_t n, const cap_value_t *v)
{
	uint64_t e, p, i, need = 0;
	size_t k;

	(void)flags;
	if (lget(&e, &p, &i) < 0)
		return -1;
	for (k = 0; k < n; k++)
		need |= to_linux(v[k]);
	if ((need & p) != need)
		return errno = EPERM, -1;
	return 0;
}

static const struct { const char *name; cap_value_t v; } names[] = {
	{"CHOWN", CAP_CHOWN}, {"DAC_WRITE", CAP_DAC_WRITE},
	{"DAC_READ_SEARCH", CAP_DAC_READ_SEARCH}, {"FOWNER", CAP_FOWNER},
	{"DAC_OVERRIDE", CAP_DAC_OVERRIDE}, {"FSETID", CAP_FSETID}, {"KILL", CAP_KILL},
	{"LINK_DIR", CAP_LINK_DIR}, {"SETFPRIV", CAP_SETFPRIV}, {"SETFCAP", CAP_SETFCAP},
	{"SETPPRIV", CAP_SETPPRIV}, {"SETPCAP", CAP_SETPCAP}, {"SETGID", CAP_SETGID},
	{"SETUID", CAP_SETUID}, {"MAC_DOWNGRADE", CAP_MAC_DOWNGRADE}, {"MAC_READ", CAP_MAC_READ},
	{"MAC_RELABEL_SUBJ", CAP_MAC_RELABEL_SUBJ}, {"MAC_WRITE", CAP_MAC_WRITE},
	{"MAC_UPGRADE", CAP_MAC_UPGRADE}, {"INF_NOFLOAT_OBJ", CAP_INF_NOFLOAT_OBJ},
	{"INF_NOFLOAT_SUBJ", CAP_INF_NOFLOAT_SUBJ}, {"INF_DOWNGRADE", CAP_INF_DOWNGRADE},
	{"INF_UPGRADE", CAP_INF_UPGRADE}, {"INF_RELABEL_SUBJ", CAP_INF_RELABEL_SUBJ},
	{"AUDIT_CONTROL", CAP_AUDIT_CONTROL}, {"AUDIT_WRITE", CAP_AUDIT_WRITE},
	{"MAC_MLD", CAP_MAC_MLD}, {"MEMORY_MGT", CAP_MEMORY_MGT}, {"SWAP_MGT", CAP_SWAP_MGT},
	{"TIME_MGT", CAP_TIME_MGT}, {"SYSINFO_MGT", CAP_SYSINFO_MGT}, {"NVRAM_MGT", CAP_NVRAM_MGT},
	{"MOUNT_MGT", CAP_MOUNT_MGT}, {"QUOTA_MGT", CAP_QUOTA_MGT}, {"PRIV_PORT", CAP_PRIV_PORT},
	{"STREAMS_MGT", CAP_STREAMS_MGT}, {"SCHED_MGT", CAP_SCHED_MGT}, {"PROC_MGT", CAP_PROC_MGT},
	{"SVIPC_MGT", CAP_SVIPC_MGT}, {"NETWORK_MGT", CAP_NETWORK_MGT}, {"DEVICE_MGT", CAP_DEVICE_MGT},
	{"MKNOD", CAP_MKNOD}, {"ACCT_MGT", CAP_ACCT_MGT}, {"SHUTDOWN", CAP_SHUTDOWN},
	{"CHROOT", CAP_CHROOT}, {"DAC_EXECUTE", CAP_DAC_EXECUTE},
	{"MAC_RELABEL_OPEN", CAP_MAC_RELABEL_OPEN}, {"SIGMASK", CAP_SIGMASK}, {"XTCB", CAP_XTCB},
};

static int lookup(const char *s, size_t n, cap_value_t *out)
{
	size_t k;

	if (n >= 4 && !strncasecmp(s, "CAP_", 4))
		s += 4, n -= 4;
	if (n == 3 && !strncasecmp(s, "all", 3)) {
		*out = CAP_ALL_ON;
		return 1;
	}
	for (k = 0; k < sizeof(names) / sizeof(names[0]); k++)
		if (strlen(names[k].name) == n && !strncasecmp(names[k].name, s, n)) {
			*out = names[k].v;
			return 1;
		}
	return 0;
}

/* "CAP_A,CAP_B+ip CAP_C=e": capability list, then one or more op+flags */
cap_t cap_from_text(const char *text)
{
	cap_t c = cap_init();
	const char *p = text;

	if (!c)
		return NULL;
	while (*p) {
		cap_value_t set = 0, v;
		const char *q;

		while (isspace((unsigned char)*p))
			p++;
		if (!*p)
			break;
		for (;;) {			/* the list; empty list means "all" */
			q = p;
			while (*q && *q != ',' && *q != '+' && *q != '-' && *q != '=' &&
			       !isspace((unsigned char)*q))
				q++;
			if (q > p) {
				if (!lookup(p, q - p, &v))
					goto bad;
				set |= v;
			}
			p = q;
			if (*p != ',')
				break;
			p++;
		}
		if (!set)
			set = CAP_ALL_ON;
		if (*p != '+' && *p != '-' && *p != '=')
			goto bad;
		while (*p == '+' || *p == '-' || *p == '=') {
			char op = *p++;
			cap_value_t fl = 0;

			for (; *p && strchr("eipEIP", *p); p++)
				fl |= (*p == 'e' || *p == 'E') ? CAP_EFFECTIVE :
				      (*p == 'p' || *p == 'P') ? CAP_PERMITTED : CAP_INHERITABLE;
			if (op == '=') {
				if (fl & CAP_EFFECTIVE) c->cap_effective |= set; else c->cap_effective &= ~set;
				if (fl & CAP_PERMITTED) c->cap_permitted |= set; else c->cap_permitted &= ~set;
				if (fl & CAP_INHERITABLE) c->cap_inheritable |= set; else c->cap_inheritable &= ~set;
			} else if (op == '+') {
				if (fl & CAP_EFFECTIVE) c->cap_effective |= set;
				if (fl & CAP_PERMITTED) c->cap_permitted |= set;
				if (fl & CAP_INHERITABLE) c->cap_inheritable |= set;
			} else {
				if (fl & CAP_EFFECTIVE) c->cap_effective &= ~set;
				if (fl & CAP_PERMITTED) c->cap_permitted &= ~set;
				if (fl & CAP_INHERITABLE) c->cap_inheritable &= ~set;
			}
		}
	}
	return c;
bad:
	free(c);
	errno = EINVAL;
	return NULL;
}

char *cap_to_text(cap_t c, size_t *len)
{
	char *s = malloc(160);

	if (!s)
		return NULL;
	snprintf(s, 160, "e=%#llx p=%#llx i=%#llx", (unsigned long long)c->cap_effective,
		 (unsigned long long)c->cap_permitted, (unsigned long long)c->cap_inheritable);
	if (len)
		*len = strlen(s);
	return s;
}

/* ---- pfmt / lfmt ------------------------------------------------------ */

static char pf_label[64];

int setcat(const char *c) { (void)c; return 0; }

char *setlabel(const char *l)
{
	snprintf(pf_label, sizeof(pf_label), "%s", l ? l : "");
	return pf_label;
}

static int vpfmt(FILE *f, long flags, const char *fmt, va_list ap)
{
	const char *sev = "";
	const char *p = fmt;
	int n = 0;

	/* "[catalog]:id:default text": use the default text */
	if (!(flags & MM_NOGET)) {
		const char *q = p;

		while (*q && *q != ':' && *q != '%' && *q != ' ')
			q++;
		if (*q == ':') {
			const char *r = q + 1;

			while (isdigit((unsigned char)*r))
				r++;
			if (*r == ':' && r > q + 1)
				p = r + 1;
		}
	}
	if (!(flags & MM_NOSTD)) {
		if (flags & MM_ERROR) sev = "ERROR";
		else if (flags & MM_WARNING) sev = "WARNING";
		else if (flags & MM_INFO) sev = "INFO";
		else if (flags & MM_ACTION) sev = "TO FIX";
		else if (flags & MM_HALT) sev = "HALT";
		if (pf_label[0])
			n += fprintf(f, "%s: ", pf_label);
		if (*sev)
			n += fprintf(f, "%s: ", sev);
	}
	return n + vfprintf(f, p, ap);
}

int pfmt(FILE *f, long flags, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vpfmt(f, flags, fmt, ap);
	va_end(ap);
	return n;
}

int lfmt(FILE *f, long flags, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vpfmt(f, flags, fmt, ap);
	va_end(ap);
	return n;
}

/* ---- uadmin ------------------------------------------------------------ */

int uadmin(int cmd, int fcn, int mdep)
{
	int lc;

	(void)mdep;
	switch (cmd) {
	case A_SHUTDOWN:
	case A_REBOOT:
		sync();
		if (fcn == AD_POWEROFF)
			lc = LINUX_REBOOT_CMD_POWER_OFF;
		else if (fcn == AD_HALT)
			lc = LINUX_REBOOT_CMD_HALT;
		else
			lc = LINUX_REBOOT_CMD_RESTART;
		return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, lc, NULL);
	case A_REMOUNT:
		sync();
		return mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
	}
	return errno = EINVAL, -1;
}

/* ---- syssgi nvram: name=value file, until it is backed by the ARCS env --- */

static const char *nvfile(void)
{
	const char *f = getenv("IRIX_NVRAM_FILE");

	return f ? f : "/var/lib/irix/nvram";
}

static int nv_get(const char *name, char *buf)
{
	FILE *f = fopen(nvfile(), "r");
	char line[SGI_NVSTRSIZE + 128];
	size_t n = strlen(name);

	if (!f)
		return errno = EINVAL, -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, name, n) && line[n] == '=') {
			line[strcspn(line, "\n")] = 0;
			strncpy(buf, line + n + 1, SGI_NVSTRSIZE - 1);
			buf[SGI_NVSTRSIZE - 1] = 0;
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return errno = EINVAL, -1;	/* IRIX: EINVAL means no such variable */
}

static int nv_set(const char *name, const char *val)
{
	char tmp[512], line[SGI_NVSTRSIZE + 128];
	FILE *in, *out;
	size_t n = strlen(name);

	snprintf(tmp, sizeof(tmp), "%s.new", nvfile());
	out = fopen(tmp, "w");
	if (!out)
		return -1;
	in = fopen(nvfile(), "r");
	while (in && fgets(line, sizeof(line), in))
		if (!(!strncmp(line, name, n) && line[n] == '='))
			fputs(line, out);
	if (in)
		fclose(in);
	fprintf(out, "%s=%s\n", name, val);
	if (fclose(out) || rename(tmp, nvfile()) < 0)
		return -1;
	return 0;
}

long irix_syssgi(int cmd, ...)
{
	va_list ap;
	const char *name;
	long r = -1;

	va_start(ap, cmd);
	name = va_arg(ap, const char *);
	if (cmd == SGI_GETNVRAM) {
		char *buf = va_arg(ap, char *);

		r = nv_get(name, buf);
	} else if (cmd == SGI_SETNVRAM) {
		r = nv_set(name, va_arg(ap, const char *));
	} else {
		errno = EINVAL;
	}
	va_end(ap);
	return r;
}

/* ---- STREAMS message calls (IRIX syscall block, patch 0011) ------------ */

int irix_getmsg(int fd, struct strbuf *ctl, struct strbuf *dat, int *flagsp)
{
	return syscall(__NR_irix_getmsg, fd, ctl, dat, flagsp);
}

int irix_putmsg(int fd, const struct strbuf *ctl, const struct strbuf *dat, int flags)
{
	return syscall(__NR_irix_putmsg, fd, ctl, dat, flags);
}

int irix_getpmsg(int fd, struct strbuf *ctl, struct strbuf *dat, int *bandp, int *flagsp)
{
	return syscall(__NR_irix_getpmsg, fd, ctl, dat, bandp, flagsp);
}

int irix_putpmsg(int fd, const struct strbuf *ctl, const struct strbuf *dat, int band, int flags)
{
	return syscall(__NR_irix_putpmsg, fd, ctl, dat, band, flags);
}

/* ---- termio over termios --------------------------------------------- */

/* the IRIX struct termio, redeclared here because termio.h redirects ioctl */
struct itermio {
	unsigned int c_iflag, c_oflag, c_cflag, c_lflag, c_ospeed, c_ispeed;
	char c_line;
	unsigned char c_cc[23];
};
#define LDSETT_REQ	(('D' << 8) | 1)
#define LDGETT_REQ	(('D' << 8) | 0)

int irix_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	int r;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);

	if (req == LDSETT_REQ || req == LDGETT_REQ)
		return 0;
	if (req == I_SRDOPT || req == I_SETSIG) {
		/* a real stream (kernel STREAMS) takes these itself; anything
		 * else is a plain FIFO or a kernel without STREAMS */
		r = syscall(SYS_ioctl, fd, req, arg);
		if (r == 0 || (errno != ENOTTY && errno != EINVAL))
			return r;
	}
	if (req == I_SRDOPT)			/* message discard mode: nothing to do */
		return 0;
	if (req == I_SETSIG) {			/* SIGPOLL when the FIFO has input */
		int fl = fcntl(fd, F_GETFL);

		if (fl < 0 || fcntl(fd, F_SETOWN, getpid()) < 0)
			return -1;
		return fcntl(fd, F_SETFL, fl | O_ASYNC);
	}
	if (req == TCGETA) {
		struct itermio *t = arg;
		struct termios k;

		if (tcgetattr(fd, &k) < 0)
			return -1;
		memset(t, 0, sizeof(*t));
		t->c_iflag = k.c_iflag;
		t->c_oflag = k.c_oflag;
		t->c_cflag = k.c_cflag;
		t->c_lflag = k.c_lflag;
		t->c_ospeed = cfgetospeed(&k);
		t->c_ispeed = cfgetispeed(&k);
		t->c_cc[0] = k.c_cc[VINTR];
		t->c_cc[1] = k.c_cc[VQUIT];
		t->c_cc[2] = k.c_cc[VERASE];
		t->c_cc[3] = k.c_cc[VKILL];
		t->c_cc[4] = k.c_cc[VEOF];
		t->c_cc[5] = k.c_cc[VEOL];
		return 0;
	}
	if (req == TCSETA || req == TCSETAW || req == TCSETAF) {
		struct itermio *t = arg;
		struct termios k;

		if (tcgetattr(fd, &k) < 0)
			return -1;
		k.c_iflag = t->c_iflag;
		k.c_oflag = t->c_oflag;
		k.c_cflag = t->c_cflag & ~CBAUD;
		k.c_lflag = t->c_lflag;
		k.c_cc[VINTR] = t->c_cc[0];
		k.c_cc[VQUIT] = t->c_cc[1];
		k.c_cc[VERASE] = t->c_cc[2];
		k.c_cc[VKILL] = t->c_cc[3];
		k.c_cc[VEOF] = t->c_cc[4];
		k.c_cc[VMIN] = t->c_cc[4];
		k.c_cc[VEOL] = t->c_cc[5];
		k.c_cc[VTIME] = t->c_cc[5];
		cfsetospeed(&k, t->c_ospeed ? t->c_ospeed : (t->c_cflag & CBAUD));
		cfsetispeed(&k, t->c_ospeed ? t->c_ospeed : (t->c_cflag & CBAUD));
		r = tcsetattr(fd, req == TCSETA ? TCSANOW : req == TCSETAW ? TCSADRAIN : TCSAFLUSH, &k);
		return r;
	}
	return ioctl(fd, req, arg);
}

/* ---- syslog debugging aid ----------------------------------------------- */

void irix_syslog(int prio, const char *fmt, ...)
{
	va_list ap;
	const char *path = getenv("IRIX_SYSLOG_FILE");

	va_start(ap, fmt);
	if (path) {
		FILE *f = fopen(path, "a");

		if (f) {
			va_list ap2;

			va_copy(ap2, ap);
			fprintf(f, "[%d] ", prio);
			vfprintf(f, fmt, ap2);
			fputc('\n', f);
			fclose(f);
			va_end(ap2);
		}
	}
	vsyslog(prio, fmt, ap);
	va_end(ap);
}

/* ---- sysmp: nothing here is the kernel's; ask for the defaults ----------- */

long irix_sysmp(int cmd, ...)
{
	(void)cmd;
	return errno = EINVAL, -1;
}

/* ---- kernel options, argv saving, availmon --------------------------------- */

/* IRIX sgikopt(): a boot option passed to the kernel.  Here: name=value on the
 * kernel command line (the ARCS PROM puts OSLoadOptions there). */
int sgikopt(const char *name, char *buf, int len)
{
	FILE *f = fopen("/proc/cmdline", "r");
	char cmd[4096];
	size_t n = strlen(name);
	char *p, *save = NULL;

	if (!f)
		return errno = EINVAL, -1;
	if (!fgets(cmd, sizeof(cmd), f))
		cmd[0] = 0;
	fclose(f);
	cmd[strcspn(cmd, "\n")] = 0;
	for (p = strtok_r(cmd, " \t", &save); p; p = strtok_r(NULL, " \t", &save)) {
		if (!strncmp(p, name, n) && p[n] == '=') {
			strncpy(buf, p + n + 1, len - 1);
			buf[len - 1] = 0;
			return (int)strlen(buf);
		}
	}
	return errno = EINVAL, -1;
}

/* audit-record argv saving: not kept */
char *argvtostr(char **argv)
{
	static char one[] = "";

	(void)argv;
	return one;
}

/* init's availability-monitor hook (availmon is not part of this port) */
int doavailmon(char state)
{
	(void)state;
	return 0;
}

/* ---- sginap, getdtablehi ------------------------------------------------ */

long sginap(long ticks)
{
	struct timespec ts;

	if (ticks <= 0)
		return sched_yield();
	ts.tv_sec = ticks / 100;
	ts.tv_nsec = (ticks % 100) * 10000000L;
	nanosleep(&ts, NULL);
	return 0;
}

int getdtablehi(void)
{
	DIR *d = opendir("/proc/self/fd");
	struct dirent *e;
	int hi = 0, dfd;

	if (!d) {
		int n = getdtablesize();

		return n > 65536 ? 65536 : n;
	}
	dfd = dirfd(d);
	while ((e = readdir(d))) {
		int fd = atoi(e->d_name);

		if (e->d_name[0] != '.' && fd != dfd && fd + 1 > hi)
			hi = fd + 1;
	}
	closedir(d);
	return hi;
}

/* ---- sysinfo, sysconf -------------------------------------------------- */

long irix_sysinfo(int cmd, char *buf, long len)
{
	struct utsname u;
	const char *s;

	if (uname(&u) < 0)
		return -1;
	switch (cmd) {
	case SI_SYSNAME: s = u.sysname; break;
	case SI_HOSTNAME: s = u.nodename; break;
	case SI_RELEASE: s = u.release; break;
	case SI_VERSION: s = u.version; break;
	case SI_MACHINE: s = u.machine; break;
	default: return errno = EINVAL, -1;
	}
	if (buf && len > 0)
		snprintf(buf, len, "%s", s);
	return strlen(s) + 1;
}

long irix_sysconf(int name)
{
	if (name == 0x10001)			/* _SC_CAP: capabilities plus superuser */
		return CAP_SYS_SUPERUSER;
	if (name == 0x10002) {			/* _SC_MAC: kernel.irix.mac_enabled */
		FILE *f = fopen("/proc/sys/kernel/irix/mac_enabled", "r");
		int v = 0;

		if (f) {
			if (fscanf(f, "%d", &v) != 1)
				v = 0;
			fclose(f);
		}
		return v;
	}
	return sysconf(name);
}

const char *const *irix_errlist(void)
{
	static const char *list[134];
	int i;

	if (!list[1])
		for (i = 0; i < 134; i++)
			list[i] = strdup(strerror(i));
	return list;
}

/* ---- /etc/capability: "user:default:allowed" per line ------------------ */

struct user_cap *sgi_getcapabilitybyname(const char *user)
{
	static struct user_cap uc;
	static char line[1024];
	const char *path = getenv("IRIX_CAPABILITY_FILE");
	FILE *f = fopen(path ? path : USER_CAPABILITY, "r");
	size_t n = strlen(user);

	if (!f)
		return NULL;
	while (fgets(line, sizeof(line), f)) {
		char *def, *allowed;

		line[strcspn(line, "\n")] = 0;
		if (line[0] == '#' || strncmp(line, user, n) || line[n] != ':')
			continue;
		def = line + n + 1;
		allowed = strchr(def, ':');
		if (!allowed)
			continue;
		*allowed++ = 0;
		uc.ca_name = line;
		uc.ca_default = def;
		uc.ca_allowed = allowed;
		fclose(f);
		return &uc;
	}
	fclose(f);
	return NULL;
}

/* ---- MAC, audit, clearance: not present --------------------------------- */

int satvwrite(int ev, int outcome, const char *fmt, ...) { (void)ev; (void)outcome; (void)fmt; return 0; }
mac_t mac_get_proc(void) { return errno = ENOSYS, (mac_t)0; }
mac_t mac_from_text(const char *t) { (void)t; return errno = ENOSYS, (mac_t)0; }
int mac_set_proc(mac_t m) { (void)m; return errno = ENOSYS, -1; }
mac_t mac_set_moldy(mac_t m) { (void)m; return errno = ENOSYS, (mac_t)0; }
int mac_equal(mac_t a, mac_t b) { return a == b; }
int mac_free(void *p) { (void)p; return 0; }
int mac_clearedlbl(struct clearance *c, mac_t m) { (void)c; (void)m; return 0; }
struct clearance *sgi_getclearancebyname(const char *u) { (void)u; return NULL; }
