
/*	start_xserver - fork and exec Xorg on the given display and VT.
	
	The parent process has already switched to the target VT and
	claimed it as its controlling terminal, so the Xorg child
	inherits a real console session.  -keeptty tells Xorg to
	stay on this inherited TTY rather than trying to open one
	itself (which would trigger "only console users" errors
	when started from init).
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

		/* only reached on exec failure */
		perror("dvlogin: execlp (Xorg)");
		_exit(1);
	}

	return pid;
}




/*	make_session_xauth - generate a fresh MIT-MAGIC-COOKIE-1,
	install it for the given display via xauth, and chown the
	file to the session user so the unprivileged client can
	read it.  Run as root; xauth creates the file 0600/root,
	then we hand it over.  Returns 0 on success, -1 on failure.
*/
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

	/* 128 bits of cookie */
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

	/* fresh file each session so stale cookies don't pile up */
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

	/* hand the cookie to the session user */
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

	/* give it a moment to shut down gracefully */
	int status;
	int tries = 0;
	while(tries < 20)
	{
		pid_t ret = waitpid(pid, &status, WNOHANG);
		if(ret != 0) { return; }
		usleep(100000);  /* 100 ms */
		tries++;
	}

	/* still alive - force kill */
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
}


static int wait_for_xserver(const char *display, int timeout_secs)
{
	int elapsed = 0;

	while(elapsed < timeout_secs)
	{
		if(g_shutdown) { return 0; }

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
