
/*	start_xserver - fork and exec Xorg on the given display and VT.
	
	The parent process has already switched to the target VT and
	claimed it as its controlling terminal, so the Xorg child
	inherits a real console session. -keeptty tells Xorg to
	stay on this inherited TTY rather than trying to open one
	itself.
*/
static pid_t start_xserver(const char *display, const char *vt, const char *authfile)
{
	pid_t pid = fork();

	if(pid < 0)
	{
		perror("dvlogin: fork (Xorg)");
		return -1;
	}

	if(pid == 0)
	{
		int logfd = open("/tmp/dvlogin-xorg.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
		if(logfd >= 0)
		{
			dup2(logfd, STDOUT_FILENO);
			dup2(logfd, STDERR_FILENO);
			if(logfd > STDERR_FILENO) close(logfd);
		}

		if(authfile != NULL)
		{
			execlp("Xorg", "Xorg", display, vt,
				"-auth", authfile,
				"-keeptty",
				"-nolisten", "tcp",
				"-noreset",
				(char *)NULL);
		}
		else
		{
			execlp("Xorg", "Xorg", display, vt,
				"-keeptty",
				"-nolisten", "tcp",
				"-noreset",
				(char *)NULL);
		}

		perror("dvlogin: execlp (Xorg)");
		_exit(1);
	}

	return pid;
}

static int make_session_xauth(const char *authfile, const char *display,
	uid_t uid, gid_t gid)
{
	unsigned char rnd[16];
	char cookie[33];
	int i;
	pid_t pid;
	int status;

	if(mkdir("/run/dvlogin", 0755) < 0 && errno != EEXIST)
	{
		perror("dvlogin: mkdir /run/dvlogin");
		return -1;
	}

	{
		int fd = open("/dev/urandom", O_RDONLY);
		if(fd < 0)
		{
			perror("dvlogin: open /dev/urandom");
			return -1;
		}
		if(read(fd, rnd, sizeof(rnd)) != (ssize_t)sizeof(rnd))
		{
			perror("dvlogin: read /dev/urandom");
			close(fd);
			return -1;
		}
		close(fd);
	}

	for(i = 0; i < 16; i++)
	{
		snprintf(cookie + (i * 2), 3, "%02x", rnd[i]);
	}
	cookie[32] = '\0';

	unlink(authfile);

	pid = fork();
	if(pid < 0)
	{
		perror("dvlogin: fork (xauth)");
		return -1;
	}
	if(pid == 0)
	{
		execlp("xauth", "xauth", "-q", "-f", authfile,
			"add", display, ".", cookie,
			(char *)NULL);
		perror("dvlogin: execlp (xauth)");
		_exit(127);
	}

	if(waitpid(pid, &status, 0) < 0)
	{
		perror("dvlogin: waitpid (xauth)");
		return -1;
	}
	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		fprintf(stderr, "dvlogin: xauth failed\n");
		return -1;
	}

	if(chown(authfile, uid, gid) < 0)
	{
		perror("dvlogin: chown (authfile)");
		return -1;
	}
	if(chmod(authfile, 0600) < 0)
	{
		perror("dvlogin: chmod (authfile)");
		return -1;
	}

	return 0;
}

static void stop_xserver(pid_t pid)
{
	if(pid <= 0) { return; }

	kill(pid, SIGTERM);

	int status;
	int tries = 0;
	while(tries < 20)
	{
		pid_t ret = waitpid(pid, &status, WNOHANG);
		if(ret != 0) { return; }
		usleep(100000);
		tries++;
	}

	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
}

static int wait_for_xserver(const char *display, int timeout_secs)
{
	int elapsed = 0;
	pid_t xpid = g_xserver_pid;

	while(elapsed < timeout_secs)
	{
		int status;
		pid_t ret;

		if(g_shutdown) { return 0; }

		if(xpid > 0)
		{
			ret = waitpid(xpid, &status, WNOHANG);
			if(ret == xpid)
			{
				if(WIFEXITED(status))
					fprintf(stderr, "dvlogin: Xorg exited early with status %d; see /tmp/dvlogin-xorg.log\n", WEXITSTATUS(status));
				else if(WIFSIGNALED(status))
					fprintf(stderr, "dvlogin: Xorg died from signal %d; see /tmp/dvlogin-xorg.log\n", WTERMSIG(status));
				return 0;
			}
		}

		Display *dpy = XOpenDisplay(display);
		if(dpy != NULL)
		{
			XCloseDisplay(dpy);
			return 1;
		}
		sleep(1);
		elapsed++;
	}

	return 0;
}
