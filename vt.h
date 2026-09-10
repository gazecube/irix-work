
/*	find_free_display - scan both the traditional X lock file and
	the X11 UNIX-domain socket to find the lowest unused display.
	Returns the display number (>= 0) or -1 on failure.
*/
static int find_free_display(void)
{
	int num;
	char lockpath[64];
	char socketpath[64];

	for(num = 0; num < MAX_DISPLAY_SCAN; num++)
	{
		snprintf(lockpath, sizeof(lockpath), "/tmp/.X%d-lock", num);
		snprintf(socketpath, sizeof(socketpath), "/tmp/.X11-unix/X%d", num);

		/* An existing X11 socket is authoritative evidence that the
		 * display number is already in use, even when no lock file is
		 * present. This is common with Xwayland and some display managers. */
		if(access(socketpath, F_OK) == 0) { continue; }

		/* No socket and no lock file means the display is available. */
		if(access(lockpath, F_OK) != 0) { return num; }

		/* A lock file can outlive its server. Only remove it when the
		 * recorded PID no longer exists and there is no corresponding
		 * X11 socket. */
		FILE *fp = fopen(lockpath, "r");
		if(fp != NULL)
		{
			int pid = 0;
			if(fscanf(fp, "%d", &pid) == 1)
			{
				if(kill(pid, 0) != 0 && errno == ESRCH)
				{
					fclose(fp);
					unlink(lockpath);
					return num;
				}
			}
			fclose(fp);
		}
	}

	return -1;
}

/*	find_current_vt - determine which virtual terminal we are
	running on so the login X server can take over that same VT.
	
	Tries ttyname(STDIN) first (/dev/ttyN -> N), then falls back
	to reading /sys/class/tty/tty0/active, then to ioctl on
	/dev/tty0 (works when launched from init with no real stdin).
	
	Returns the VT number (>= 1) or -1 if it cannot be determined.
*/
static int find_current_vt(void)
{
	const char *p;

	/* try the controlling terminal */
	char *tty = ttyname(STDIN_FILENO);
	if(tty != NULL)
	{
		/*
			Only match /dev/ttyN not /dev/pts/N or
			/dev/ttyUSB0 or anything else.
		*/
		if(strncmp(tty, "/dev/tty", 8) == 0 && isdigit((unsigned char)tty[8]))
		{
			return atoi(tty + 8);
		}
	}

	/* fallback: ask the kernel which VT is active via sysfs */
	FILE *fp = fopen("/sys/class/tty/tty0/active", "r");
	if(fp != NULL)
	{
		char buf[32];
		if(fgets(buf, sizeof(buf), fp) != NULL)
		{
			p = buf;
			while(*p && !isdigit((unsigned char)*p)) { p++; }
			if(*p)
			{
				int vt = atoi(p);
				fclose(fp);
				return vt;
			}
		}
		fclose(fp);
	}

#ifdef VT_GETSTATE
	/*	Last resort: ioctl on /dev/tty0. This works when started
		from an init script where stdin is /dev/null and sysfs
		may not be mounted yet.
	*/
	{
		int fd = open("/dev/tty0", O_RDONLY | O_NOCTTY);
		if(fd >= 0)
		{
			struct vt_stat vts;
			if(ioctl(fd, VT_GETSTATE, &vts) == 0)
			{
				close(fd);
				return vts.v_active;
			}
			close(fd);
		}
	}
#endif

	return -1;
}


/*	VT blanking
	
	Puts the console into KD_GRAPHICS mode so the kernel
	stops rendering text on the framebuffer, giving a clean
	black screen during the gap between X servers.
	
	The signal handler restores KD_TEXT if dvlogin crashes,
	so the user doesn't get stranded on a blank VT.
*/

#ifdef KDSETMODE
/*
static void blank_vt(int vt)
{
	char path[32];
	int fd;

	snprintf(path, sizeof(path), "/dev/tty%d", vt);
	fd = open(path, O_RDWR);
	if(fd >= 0)
	{
		ioctl(fd, KDSETMODE, KD_GRAPHICS);
		close(fd);
	}
}
*/
static void unblank_vt(int vt)
{
	char path[32];
	int fd;

	snprintf(path, sizeof(path), "/dev/tty%d", vt);
	fd = open(path, O_RDWR);
	if(fd >= 0)
	{
		ioctl(fd, KDSETMODE, KD_TEXT);
		close(fd);
	}
}

/*	paint_vt - switch the VT back to text mode, fill the screen
	with a solid background color, and hide the cursor.  This
	gives a clean colored screen during transitions between
	X servers instead of ugly console text.
	
	Uses ANSI escapes which work on the Linux console:
	ESC[40m  = black background
	ESC[30m  = black foreground (hides any stray text)
	ESC[2J   = clear entire screen
	ESC[H    = cursor home
	ESC[?25l = hide cursor
*/
static void paint_vt(int vt)
{
	char path[32];
	int fd;
	static const char seq[] = "\033[40m\033[30m\033[2J\033[H\033[?25l";

	snprintf(path, sizeof(path), "/dev/tty%d", vt);
	fd = open(path, O_RDWR);
	if(fd >= 0)
	{
		/* make sure we're in text mode so escapes work */
		ioctl(fd, KDSETMODE, KD_TEXT);
		write(fd, seq, sizeof(seq) - 1);
		close(fd);
	}
}




/*	crash_handler - last resort for SIGSEGV/SIGABRT/SIGBUS.
	Unblanks the VT so the user isn't stranded, then re-raises.
*/
static void crash_handler(int sig)
{
	unblank_vt(g_vt_num);
	signal(sig, SIG_DFL);
	raise(sig);
}

/*	shutdown_handler - SIGTERM/SIGINT from init or the admin.
	Sets the flag so the main loop exits after its current
	wait completes (EINTR will break waitpid).
*/
static void shutdown_handler(int sig)
{
	(void)sig;
	g_shutdown = 1;
}

static void install_signal_handlers(void)
{
	struct sigaction sa_crash;
	struct sigaction sa_shutdown;

	/* crash handlers: one-shot, unblank and re-raise */
	memset(&sa_crash, 0, sizeof(sa_crash));
	sa_crash.sa_handler = crash_handler;
	sigemptyset(&sa_crash.sa_mask);
	sa_crash.sa_flags = SA_RESETHAND;

	sigaction(SIGSEGV, &sa_crash, NULL);
	sigaction(SIGABRT, &sa_crash, NULL);
	sigaction(SIGBUS,  &sa_crash, NULL);

	/* shutdown handlers: set flag, let main loop clean up */
	memset(&sa_shutdown, 0, sizeof(sa_shutdown));
	sa_shutdown.sa_handler = shutdown_handler;
	sigemptyset(&sa_shutdown.sa_mask);
	sa_shutdown.sa_flags = 0;  /* allow EINTR on waitpid */

	sigaction(SIGTERM, &sa_shutdown, NULL);
	sigaction(SIGINT,  &sa_shutdown, NULL);
}

#else

/* no-ops for systems without KDSETMODE (e.g. NetBSD wscons) */
/*static void blank_vt(int vt)   { (void)vt; }*/
static void unblank_vt(int vt) { (void)vt; }
static void paint_vt(int vt)   { (void)vt; }

static void shutdown_handler(int sig)
{
	(void)sig;
	g_shutdown = 1;
}

static void install_signal_handlers(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = shutdown_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
}

#endif
