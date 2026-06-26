#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#ifdef __linux__
#include <linux/kd.h>
#include <linux/vt.h>
#elif defined(__FreeBSD__)
#include <sys/kbio.h>
#include <sys/consio.h>
#endif

#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Frame.h>
#include <Xm/ScrolledW.h>
#include <Xm/Text.h>
#include <Xm/Separator.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Label.h>
#include <Xm/LabelG.h>
#include <Xm/MwmUtil.h>

#include <X11/xpm.h>
#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>

#include "global.h"
#include "fwddec.h"
#include "vt.h"
#include "callbacks.h"
#include "shortcuts.h"
#include "gui.h"
#include "x11mgmt.h"
#include "session.h"


int main(int argc, char *argv[])
{
	/* verifySuperUser(); */

	char username[MAX_USERNAME_LEN + 1];
	int vt_num = -1;
	int i;

	/* parse command-line arguments */
	for(i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-vt") == 0 && i + 1 < argc)
		{
			vt_num = atoi(argv[i + 1]);
			if(vt_num < 1 || vt_num > 63)
			{
				fprintf(stderr, "dvlogin: invalid VT number '%s'\n", argv[i + 1]);
				return 1;
			}
			i++;  /* skip the number */
		}
		else
		{
			fprintf(stderr, "usage: dvlogin [-vt N]\n");
			return 1;
		}
	}

	/* auto-detect VT if not specified on command line */
	if(vt_num < 0)
	{
		vt_num = find_current_vt();
		if(vt_num < 0)
		{
			fprintf(stderr, "dvlogin: could not determine current VT, defaulting to vt7\n");
			vt_num = 7;
		}
	}
	g_vt_num = vt_num;
	snprintf(g_vt_str, sizeof(g_vt_str), "vt%d", vt_num);

/*
	Take ownership of the target VT.  We are root, so we
	can forcibly switch to it, become session leader, and
	claim it as our controlling terminal.  This means the
	Xorg child inherits a real console on the right VT,
	which avoids "only console users" errors and ensures
	everything renders on the correct TTY.
*/
	{
		char ttypath[32];
		int console_fd, vt_fd;

		/* switch the active VT to our target */
		console_fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
		if(console_fd >= 0)
		{
			ioctl(console_fd, VT_ACTIVATE, vt_num);
			ioctl(console_fd, VT_WAITACTIVE, vt_num);
			close(console_fd);
		}

		/* become session leader so TIOCSCTTY works */
		setsid();

		/* open the target VT and claim it */
		snprintf(ttypath, sizeof(ttypath), "/dev/tty%d", vt_num);
		vt_fd = open(ttypath, O_RDWR);
		if(vt_fd >= 0)
		{
			ioctl(vt_fd, TIOCSCTTY, 1);  /* 1 = steal from other session */

			/* point stdio at the VT */
			dup2(vt_fd, STDIN_FILENO);
			dup2(vt_fd, STDOUT_FILENO);
			dup2(vt_fd, STDERR_FILENO);
			if(vt_fd > STDERR_FILENO) { close(vt_fd); }

			/*	Paint the VT so there's no ugly text visible
				during transitions.  ESC[44m = blue background,
				ESC[2J = clear screen, ESC[H = cursor home,
				ESC[?25l = hide cursor.
			*/
			{
				static const char seq[] =
					"\033[40m\033[30m\033[2J\033[H\033[?25l";
				write(STDOUT_FILENO, seq, sizeof(seq) - 1);
			}
		}
	}

	/* install signal handlers (crash + clean shutdown) */
	install_signal_handlers();

	fprintf(stdout, "dvlogin: starting session loop on %s\n", g_vt_str);
	fflush(stdout);

	while(!g_shutdown)
	{
		memset(username, 0, sizeof(username));

		/* find a free X display number */
		int display_num = find_free_display();
		if(display_num < 0)
		{
			fprintf(stderr, "dvlogin: no free X display found (scanned :%d-:%d), retrying...\n",
				0, MAX_DISPLAY_SCAN - 1);
			sleep(5);
			continue;
		}
		snprintf(g_display_str, sizeof(g_display_str), ":%d", display_num);

		fprintf(stdout, "dvlogin: using display %s on %s\n", g_display_str, g_vt_str);
		fflush(stdout);

		/* start X server for the login screen */
		pid_t xpid = start_xserver(g_display_str, g_vt_str, NULL);
		if(xpid < 0)
		{
			fprintf(stderr, "dvlogin: X server failed to start, retrying...\n");
			sleep(5);
			continue;
		}
		g_xserver_pid = xpid;

		/* wait until the X server is accepting connections */
		if(!wait_for_xserver(g_display_str, XSERVER_TIMEOUT))
		{
			fprintf(stderr, "dvlogin: X server not responding on %s, retrying...\n",
				g_display_str);
			stop_xserver(xpid);
			g_xserver_pid = 0;
			sleep(5);
			continue;
		}

		/* optional: set login screen background */
		setenv("DISPLAY", g_display_str, 1);
		system("xsetroot -solid skyblue4");

		/* run login GUI, blocks until auth succeeds or GUI exits */
		int result = run_login_gui(argc, argv, username, sizeof(username));

		/*	Paint the VT blue before tearing down X so the user
			sees a clean screen instead of TTY text flicker.
		*/
		paint_vt(g_vt_num);

		/* tear down the login X server */
		stop_xserver(xpid);
		g_xserver_pid = 0;

		if(g_shutdown) { break; }

		if(result != 1 || strlen(username) == 0)
		{
			fprintf(stderr, "dvlogin: GUI exited without authentication, restarting...\n");
			sleep(2);
			continue;
		}

		fprintf(stdout, "dvlogin: authenticated user '%s', launching session\n", username);
		fflush(stdout);

		/* run the user's desktop session, blocks until it ends */
		run_user_session(username);

		if(g_shutdown) { break; }

		/*	Session ended - paint the VT blue for a clean
			transition back to the login screen.
		*/
		paint_vt(g_vt_num);

		fprintf(stdout, "dvlogin: session ended, returning to login screen\n");
		fflush(stdout);

		/* small delay before restarting login screen */
		sleep(1);
	}

	/* clean shutdown */
	if(g_xserver_pid > 0)
	{
		stop_xserver(g_xserver_pid);
		g_xserver_pid = 0;
	}

	unblank_vt(g_vt_num);

	fprintf(stdout, "dvlogin: shutting down cleanly\n");
	fflush(stdout);

	return 0;
}
