/*
 * IRIX-style desktop session handoff for dvlogin/clogin reconstruction.
 *
 * The login manager currently exposes one implicit desktop session.  That
 * session is MaXX Interactive Desktop's normal Xsession.dt entrypoint.  The
 * small session descriptor below is intentionally shaped so a future session
 * switcher can select another descriptor without changing the login/session
 * handoff itself.
 *
 * A readable $HOME/.xsession still takes precedence, matching the recovered
 * IRIX-style per-user override behavior.
 */

#ifndef DVLOGIN_IRIX_SESSION_H
#define DVLOGIN_IRIX_SESSION_H

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

#ifndef CLOGIN_DEFAULT_SESSION_PATH
#define CLOGIN_DEFAULT_SESSION_PATH "/opt/MaXX/etc/skel/Xsession.dt"
#endif

#ifndef CLOGIN_DEFAULT_SESSION_NAME
#define CLOGIN_DEFAULT_SESSION_NAME "MaXX Interactive Desktop"
#endif

typedef struct CloginSession
{
	const char *name;
	const char *path;
} CloginSession;

static const CloginSession g_clogin_default_session =
{
	CLOGIN_DEFAULT_SESSION_NAME,
	CLOGIN_DEFAULT_SESSION_PATH
};

static int irix_session_is_readable_regular(const char *path)
{
	struct stat st;

	if(path == NULL || access(path, R_OK) != 0)
		return 0;

	if(stat(path, &st) != 0)
		return 0;

	return S_ISREG(st.st_mode);
}

static const CloginSession *clogin_selected_session(void)
{
	/*
	 * Future session switcher hook.  Until a selector UI exists, the selected
	 * session is always the MaXX default descriptor above.
	 */
	return &g_clogin_default_session;
}

static void clogin_exec_session(const CloginSession *session)
{
	if(session == NULL || session->path == NULL || session->path[0] == '\0')
	{
		errno = EINVAL;
		perror("dvlogin: invalid session descriptor");
		_exit(1);
	}

	execl(session->path, session->path, (char *)NULL);
	fprintf(stderr, "dvlogin: exec session '%s' (%s) failed: %s\n",
		session->name ? session->name : "unnamed",
		session->path,
		strerror(errno));
	_exit(1);
}

/*
 * Reconstruct the top-level IRIX Xsession selection rule:
 *
 *     $HOME/.xsession, when readable
 *         - exec directly when executable
 *         - otherwise run it through /bin/sh
 *
 *     otherwise use the currently selected desktop session.
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

	clogin_exec_session(clogin_selected_session());
}

#endif /* DVLOGIN_IRIX_SESSION_H */
