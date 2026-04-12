
/*	start_xserver - fork and exec Xorg on the given display and VT.
	
	The parent process has already switched to the target VT and
	claimed it as its controlling terminal, so the Xorg child
	inherits a real console session.  -keeptty tells Xorg to
	stay on this inherited TTY rather than trying to open one
	itself (which would trigger "only console users" errors
	when started from init).
*/
static pid_t start_xserver(const char *display, const char *vt)
{
	pid_t pid = fork();

	if(pid < 0)
	{
		perror("dvlogin: fork (Xorg)");
		return -1;
	}

	if(pid == 0)
	{
		/* exec Xorg on the inherited VT */
		execlp("Xorg", "Xorg", display, vt,
			"-keeptty",
			"-nolisten", "tcp",
			"-noreset",
			(char *)NULL);

		/* only reached on exec failure */
		perror("dvlogin: execlp (Xorg)");
		_exit(1);
	}

	return pid;
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
