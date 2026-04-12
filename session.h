
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

	pid_t pid = fork();

	if(pid < 0)
	{
		perror("dvlogin: fork (session)");
		return;
	}

	if(pid == 0)
	{
		/* set up supplementary groups */
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

		/* move into user's home directory */
		if(chdir(pw->pw_dir) < 0)
		{
			fprintf(stderr, "dvlogin: chdir to '%s' failed: %s\n",
				pw->pw_dir, strerror(errno));
			/* fall back to / rather than failing */
			chdir("/");
		}

		/* set up a clean environment */
		clearenv();
		setenv("HOME",    pw->pw_dir,   1);
		setenv("USER",    pw->pw_name,  1);
		setenv("LOGNAME", pw->pw_name,  1);
		setenv("SHELL",   pw->pw_shell, 1);
		setenv("PATH",    "/usr/local/bin:/usr/bin:/bin", 1);

		/* launch the desktop session */
		execlp("startx", "startx", USER_SESSION_CMD,
			(char *)NULL);

		perror("dvlogin: execlp (startx)");
		_exit(1);
	}

	/* parent: wait for session to end */
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
		/* unexpected waitpid error */
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
}
