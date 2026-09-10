/*
 * IRIX-style desktop session handoff for dvlogin/clogin reconstruction.
 *
 * This is deliberately a compatibility layer rather than a MaXX-specific
 * session launcher.  The observed IRIX Xsession behavior gives a readable
 * $HOME/.xsession precedence over the system desktop session.  MaXX's
 * Xsession.dt is currently used as the system desktop backend while the
 * remaining IRIX startup sequence is reconstructed here incrementally.
 */

#ifndef DVLOGIN_IRIX_SESSION_H
#define DVLOGIN_IRIX_SESSION_H

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

#ifndef IRIX_SYSTEM_XSESSION
#define IRIX_SYSTEM_XSESSION "/opt/MaXX/etc/skel/Xsession.dt"
#endif

static int irix_session_is_readable_regular(const char *path)
{
	struct stat st;

	if(path == NULL || access(path, R_OK) != 0)
		return 0;

	if(stat(path, &st) != 0)
		return 0;

	return S_ISREG(st.st_mode);
}

/*
 * Reconstruct the top-level IRIX Xsession selection rule:
 *
 *     $HOME/.xsession, when readable
 *         - exec directly when executable
 *         - otherwise run it through /bin/sh
 *
 *     otherwise use the system desktop session.
 *
 * This function is called only after dvlogin has dropped to the authenticated
 * uid/gid and installed HOME/USER/LOGNAME/SHELL/DISPLAY/XAUTHORITY.
 */
static void irix_exec_user_session(const struct passwd *pw)
{
	char xsession[MAXPATHLEN];

	if(pw == NULL || pw->pw_dir == NULL)
	{
		errno = EINVAL;
		perror("dvlogin: IRIX session passwd state");
		_exit(1);
	}

	if(snprintf(xsession, sizeof(xsession), "%s/.xsession", pw->pw_dir)
		>= (int)sizeof(xsession))
	{
		fprintf(stderr, "dvlogin: .xsession path is too long\n");
		_exit(1);
	}

	if(irix_session_is_readable_regular(xsession))
	{
		if(access(xsession, X_OK) == 0)
		{
			execl(xsession, xsession, (char *)NULL);
			perror("dvlogin: exec ~/.xsession");
		}
		else
		{
			execl("/bin/sh", "sh", xsession, (char *)NULL);
			perror("dvlogin: /bin/sh ~/.xsession");
		}

		_exit(1);
	}

	/*
	 * Transitional backend: MaXX advertises Xsession.dt as its desktop
	 * session entrypoint.  Keeping this decision here (rather than in the
	 * login manager core) lets us replace it piece-by-piece with reconstructed
	 * IRIX Xsession behavior without changing run_user_session() again.
	 */
	execl(IRIX_SYSTEM_XSESSION, IRIX_SYSTEM_XSESSION, (char *)NULL);
	perror("dvlogin: exec system Xsession");
	_exit(1);
}

#endif /* DVLOGIN_IRIX_SESSION_H */
