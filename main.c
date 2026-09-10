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
#include "clogin_reconstructed.h"
#include "clogin_ui.h"
#include "clogin_autologin.h"
#include "gui.h"

/* Keep the clogin compatibility rewrites scoped to gui.h only. */
#undef addCustomShortcuts
#undef loginButtonCB
#undef textInputActivatedCB
#undef XtRealizeWidget

#include "pandora_runtime.h"
#include "x11mgmt.h"
#include "irix_session.h"
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
			i++;
		}
		else
		{
			fprintf(stderr, "usage: dvlogin [-vt N]\n");
			return 1;
		}
	}

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

	{
		char ttypath[32];
		int console_fd, vt_fd;

		console_fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
		if(console_fd >= 0)
		{
			ioctl(console_fd, VT_ACTIVATE, vt_num);
			ioctl(console_fd, VT_WAITACTIVE, vt_num);
			close(console_fd);
		}

		setsid();
		snprintf(ttypath, sizeof(ttypath), "/dev/tty%d", vt_num);
		vt_fd = open(ttypath, O_RDWR);
		if(vt_fd >= 0)
		{
			ioctl(vt_fd, TIOCSCTTY, 1);
			dup2(vt_fd, STDIN_FILENO);
			dup2(vt_fd, STDOUT_FILENO);
			dup2(vt_fd, STDERR_FILENO);
			if(vt_fd > STDERR_FILENO) { close(vt_fd); }
			{
				static const char seq[] =
					"\033[40m\033[30m\033[2J\033[H\033[?25l";
				write(STDOUT_FILENO, seq, sizeof(seq) - 1);
			}
		}
	}

	install_signal_handlers();

	fprintf(stdout, "dvlogin: starting session loop on %s\n", g_vt_str);
	fflush(stdout);

	while(!g_shutdown)
	{
		int display_num;
		pid_t xpid;
		int result;

		memset(username, 0, sizeof(username));

		/* Original clogin consumes /etc/autologin.on before validating
		 * /etc/autologin. A valid entry bypasses the greeter entirely. */
		if(clogin_check_autologin(username, sizeof(username)))
		{
			fprintf(stdout, "dvlogin: IRIX-style autologin for '%s'\n", username);
			fflush(stdout);
			run_user_session(username);
			if(g_shutdown) { break; }
			paint_vt(g_vt_num);
			fprintf(stdout, "dvlogin: autologin session ended, returning to login screen\n");
			fflush(stdout);
			sleep(1);
			continue;
		}

		display_num = find_free_display();
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

		xpid = start_xserver(g_display_str, g_vt_str, NULL);
		if(xpid < 0)
		{
			fprintf(stderr, "dvlogin: X server failed to start, retrying...\n");
			sleep(5);
			continue;
		}
		g_xserver_pid = xpid;

		if(!wait_for_xserver(g_display_str, XSERVER_TIMEOUT))
		{
			fprintf(stderr, "dvlogin: X server not responding on %s, retrying...\n",
				g_display_str);
			stop_xserver(xpid);
			g_xserver_pid = 0;
			sleep(5);
			continue;
		}

		setenv("DISPLAY", g_display_str, 1);
		system("xsetroot -solid skyblue4");

		result = run_login_gui(argc, argv, username, sizeof(username));

		paint_vt(g_vt_num);
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

		run_user_session(username);

		if(g_shutdown) { break; }

		paint_vt(g_vt_num);
		fprintf(stdout, "dvlogin: session ended, returning to login screen\n");
		fflush(stdout);
		sleep(1);
	}

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
