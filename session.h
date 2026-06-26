
/*	Login GUI (runs in forked process)
	
	Returns 1 and fills username_out on successful auth.
	Returns 0 if the GUI exited without authenticating
	or if a shutdown was requested.
*/

static int run_login_gui(int argc, char **argv,
	char *username_out, size_t username_len)
{
	int pipefd[2];

	if(pipe(pipefd) < 0)
	{
		perror("dvlogin: pipe");
		return 0;
	}

	pid_t pid = fork();

	if(pid < 0)
	{
		perror("dvlogin: fork (GUI)");
		close(pipefd[0]);
		close(pipefd[1]);
		return 0;
	}

	if(pid == 0)
	{
		/* run the Motif login GUI */
		close(pipefd[0]);

		setenv("DISPLAY", g_display_str, 1);

		/* clear auth state */
		memset(g_auth_username, 0, sizeof(g_auth_username));

		/* blocks in XtAppMainLoop until XtAppSetExitFlag */
		run_motif_gui(argc, argv);

		/* write authenticated username back to parent */
		if(strlen(g_auth_username) > 0)
		{
			write(pipefd[1], g_auth_username, strlen(g_auth_username));
		}

		close(pipefd[1]);
		_exit(0);
	}

	/* wait for username */
	close(pipefd[1]);

	ssize_t n = read(pipefd[0], username_out, username_len - 1);
	close(pipefd[0]);

	/*
	 * waitpid may return -1/EINTR if we caught SIGTERM while waiting. 
	 */
	if(waitpid(pid, NULL, 0) < 0 && errno == EINTR)
	{
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
	}

	if(n > 0)
	{
		username_out[n] = '\0';
		return 1;
	}

	return 0;
}


static void run_user_session(const char *username)
{
	struct passwd *pw = getpwnam(username);

	if(pw == NULL)
	{
		fprintf(stderr, "dvlogin: getpwnam failed for '%s': %s\n",
			username, strerror(errno));
		return;
	}

	/* pick a fresh display for the session server */
	int display_num = find_free_display();
	if(display_num < 0)
	{
		fprintf(stderr, "dvlogin: no free display for session\n");
		return;
	}

	char session_display[16];
	snprintf(session_display, sizeof(session_display), ":%d", display_num);

	char authfile[64];
	snprintf(authfile, sizeof(authfile), "/run/dvlogin/auth-%d",
		(int)pw->pw_uid);

	if(make_session_xauth(authfile, session_display,
		pw->pw_uid, pw->pw_gid) != 0)
	{
		fprintf(stderr, "dvlogin: failed to build session xauth\n");
		return;
	}

	/*	The root parent needs the cookie too, so its XOpenDisplay
		probe in wait_for_xserver can authenticate against the
		-auth server.  Root can read the file regardless of the
		chown above.
	*/
	setenv("XAUTHORITY", authfile, 1);

	/*	Start the session X server ourselves, as root, on the
		login VT.  Root opens the DRM/input devices and the VT,
		so the rootless-Xorg ACL and Xorg.wrap console gates
		that killed the startx path never come into play.
	*/
	pid_t xpid = start_xserver(session_display, g_vt_str, authfile);
	if(xpid < 0)
	{
		fprintf(stderr, "dvlogin: session X server failed to start\n");
		unsetenv("XAUTHORITY");
		unlink(authfile);
		return;
	}
	g_xserver_pid = xpid;

	if(!wait_for_xserver(session_display, XSERVER_TIMEOUT))
	{
		fprintf(stderr, "dvlogin: session X not responding on %s\n",
			session_display);
		stop_xserver(xpid);
		g_xserver_pid = 0;
		unsetenv("XAUTHORITY");
		unlink(authfile);
		return;
	}

	/* fork the unprivileged session client */
	pid_t pid = fork();

	if(pid < 0)
	{
		perror("dvlogin: fork (session)");
		stop_xserver(xpid);
		g_xserver_pid = 0;
		unsetenv("XAUTHORITY");
		unlink(authfile);
		return;
	}

	if(pid == 0)
	{
		/* drop privileges: groups, then gid, then uid (uid last) */
		if(initgroups(username, pw->pw_gid) < 0)
		{
			perror("dvlogin: initgroups");
			_exit(1);
		}
		if(setgid(pw->pw_gid) < 0)
		{
			perror("dvlogin: setgid");
			_exit(1);
		}
		if(setuid(pw->pw_uid) < 0)
		{
			perror("dvlogin: setuid");
			_exit(1);
		}

		/* paranoia: confirm we cannot climb back to root */
		if(setuid(0) != -1)
		{
			fprintf(stderr, "dvlogin: privilege drop incomplete\n");
			_exit(1);
		}

		if(chdir(pw->pw_dir) < 0)
		{
			fprintf(stderr, "dvlogin: chdir to '%s' failed: %s\n",
				pw->pw_dir, strerror(errno));
			if(chdir("/") < 0)
			{
				_exit(1);
			}
		}

		/* clean environment for the session */
		clearenv();
		setenv("HOME",       pw->pw_dir,                     1);
		setenv("USER",       pw->pw_name,                    1);
		setenv("LOGNAME",    pw->pw_name,                    1);
		setenv("SHELL",      pw->pw_shell,                   1);
		setenv("PATH",       "/usr/local/bin:/usr/bin:/bin", 1);
		setenv("DISPLAY",    session_display,                1);
		setenv("XAUTHORITY", authfile,                       1);

		/*	Connect to the server we already started.  The cookie
			lets this uid in; no startx, no xinit, no wrapper.
			USER_SESSION_CMD must be a plain session client that
			blocks until logout and does NOT start its own X.
		*/
		execlp(USER_SESSION_CMD, USER_SESSION_CMD, (char *)NULL);

		perror("dvlogin: execlp (session)");
		_exit(1);
	}

	/* parent: wait for the session client to exit */
	int status;
	while(waitpid(pid, &status, 0) < 0)
	{
		if(errno == EINTR)
		{
			if(g_shutdown)
			{
				kill(pid, SIGTERM);
			}
			continue;
		}
		break;
	}

	if(WIFEXITED(status))
	{
		fprintf(stdout, "dvlogin: session exited with status %d\n",
			WEXITSTATUS(status));
	}
	else if(WIFSIGNALED(status))
	{
		fprintf(stdout, "dvlogin: session killed by signal %d\n",
			WTERMSIG(status));
	}
	fflush(stdout);

	/* tear down the session X server and its cookie */
	stop_xserver(xpid);
	g_xserver_pid = 0;
	unsetenv("XAUTHORITY");
	unlink(authfile);
}
