
/* Login GUI (runs in forked process)
 *
 * Returns 1 and fills username_out on successful auth.
 * Returns 0 if the GUI exited without authenticating
 * or if a shutdown was requested.
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
		char child_username[MAX_USERNAME_LEN + 1];
		int authenticated = 0;
		const char *legacy = getenv("CLOGIN_USE_LEGACY_MOTIF");

		close(pipefd[0]);
		setenv("DISPLAY", g_display_str, 1);
		memset(child_username, 0, sizeof(child_username));

		/*
		 * The reconstructed ViewKit Pandora implementation is the primary
		 * greeter. Keep the old Motif prototype available only as an explicit
		 * debugging fallback while host integration is being shaken out.
		 */
		if(legacy != NULL && legacy[0] != '\0' && strcmp(legacy, "0") != 0)
		{
			memset(g_auth_username, 0, sizeof(g_auth_username));
			run_motif_gui(argc, argv);
			if(strlen(g_auth_username) > 0)
			{
				strncpy(child_username, g_auth_username, MAX_USERNAME_LEN);
				child_username[MAX_USERNAME_LEN] = '\0';
				authenticated = 1;
			}
		}
		else
		{
			authenticated = run_pandora_gui(
				argc, argv, child_username, sizeof(child_username));
		}

		if(authenticated && strlen(child_username) > 0)
			write(pipefd[1], child_username, strlen(child_username));

		close(pipefd[1]);
		_exit(0);
	}

	close(pipefd[1]);

	ssize_t n = read(pipefd[0], username_out, username_len - 1);
	close(pipefd[0]);

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

	/* The root parent needs the cookie too, so its XOpenDisplay
	 * probe in wait_for_xserver can authenticate against the
	 * -auth server. Root can read the file regardless of chown. */
	setenv("XAUTHORITY", authfile, 1);

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

		clearenv();
		setenv("HOME",       pw->pw_dir,                     1);
		setenv("USER",       pw->pw_name,                    1);
		setenv("LOGNAME",    pw->pw_name,                    1);
		setenv("SHELL",      pw->pw_shell,                   1);
		setenv("PATH",       "/usr/local/bin:/usr/bin:/bin", 1);
		setenv("DISPLAY",    session_display,                1);
		setenv("XAUTHORITY", authfile,                       1);

		irix_exec_user_session(pw);
		_exit(1);
	}

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

	stop_xserver(xpid);
	g_xserver_pid = 0;
	unsetenv("XAUTHORITY");
	unlink(authfile);
}
