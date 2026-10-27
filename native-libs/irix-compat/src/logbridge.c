/*
 * logbridge: carry glibc syslog() traffic to the IRIX syslogd.
 *
 * Linux programs log by sending datagrams ("<pri>Mmm dd hh:mm:ss tag: text")
 * to the unix socket /dev/log.  IRIX programs reach syslogd through the
 * STREAMS log driver instead (libc syslog() does a putmsg() of a log_ctl and
 * the line).  This daemon owns /dev/log and forwards every datagram to
 * /dev/streams/log the way IRIX libc would, so the one syslogd sees both.
 *
 * usage: logbridge [-f]    (-f: stay in the foreground)
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>

#include <sys/irix_stropts_uapi.h>

#define LOGSOCK		"/dev/log"
#define LOGSTREAM	"/dev/streams/log"
#define MAXLINE		1024

static volatile sig_atomic_t done;

static void onsig(int sig)
{
	done = 1;
}

static int open_stream(void)
{
	int fd = open(LOGSTREAM, O_RDWR);

	if (fd < 0)
		fprintf(stderr, "logbridge: %s: %s\n", LOGSTREAM, strerror(errno));
	return fd;
}

/* "<pri>text" as IRIX libc sends it; add the timestamp when the sender left it out */
static int forward(int sfd, const char *msg, size_t len)
{
	struct log_ctl lc;
	char text[MAXLINE + 32];
	struct strbuf c, d;
	int pri = 14, n = 0;
	const char *p = msg;

	memset(&lc, 0, sizeof(lc));
	if (len && *p == '<') {
		pri = 0;
		while (++p < msg + len && *p >= '0' && *p <= '9')
			pri = pri * 10 + (*p - '0');
		if (p < msg + len && *p == '>')
			p++;
		else
			p = msg;
	}
	len -= p - msg;
	if (len > MAXLINE)
		len = MAXLINE;
	/* a BSD line already carries "Mmm dd hh:mm:ss ": position 3 and 6 are a space and 9 a colon */
	if (!(len > 16 && p[3] == ' ' && p[6] == ' ' && p[9] == ':')) {
		time_t t = time(NULL);
		char stamp[32];

		strftime(stamp, sizeof(stamp), "%b %e %H:%M:%S ", localtime(&t));
		n = snprintf(text, sizeof(text), "<%d>%s%.*s", pri, stamp, (int)len, p);
	} else {
		n = snprintf(text, sizeof(text), "<%d>%.*s", pri, (int)len, p);
	}
	lc.flags = SL_CONSOLE;
	lc.pri = pri;
	c.len = sizeof(lc);
	c.buf = (char *)&lc;
	d.len = n + 1;
	d.buf = text;
	return syscall(__NR_irix_putmsg, sfd, &c, &d, 0);
}

int main(int argc, char **argv)
{
	int foreground = argc > 1 && !strcmp(argv[1], "-f");
	struct sockaddr_un sa;
	int s, sfd;
	char buf[MAXLINE + 1];

	sfd = open_stream();
	if (sfd < 0)
		return 1;
	s = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0) {
		perror("logbridge: socket");
		return 1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, LOGSOCK);
	unlink(LOGSOCK);
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("logbridge: bind " LOGSOCK);
		return 1;
	}
	chmod(LOGSOCK, 0666);

	signal(SIGTERM, onsig);
	signal(SIGINT, onsig);
	if (!foreground) {
		if (fork())
			return 0;
		setsid();
	}
	while (!done) {
		ssize_t n = recv(s, buf, MAXLINE, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\0'))
			n--;
		if (n && forward(sfd, buf, n) < 0 && errno == ENXIO) {
			close(sfd);		/* log driver went away; reopen */
			sfd = open_stream();
			if (sfd < 0)
				break;
		}
	}
	unlink(LOGSOCK);
	return 0;
}
