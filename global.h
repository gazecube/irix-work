#define USER_SESSION_CMD "/opt/MaXX/etc/skel/Xsession.dt"

#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 128
#define XSERVER_TIMEOUT  10
#define MAX_DISPLAY_SCAN 32
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#define SHIRT_COLOR_ROOT  "#9b4949"
#define SHIRT_COLOR_USER  "#499b94"
#define SHIRT_COLOR_DEMO  "#9177a5"


int enableRootShortcut = 1;

Widget textBox1, textBox2, helpButton, powerButton, optionsButton, scrolledWin0, scrolledWin, hostLabel;
/* Temporary compatibility alias for the reconstructed clogin bridge.
 * The current Motif prototype's password input widget is textBox2. */
#define password textBox2
XtAppContext app;

/*	Set by auth callbacks on success.
	Read by run_login_gui() after XtAppMainLoop returns.
*/
static char g_auth_username[MAX_USERNAME_LEN + 1];

/*	Resolved at startup by find_free_display() and find_current_vt().
*/
static char g_display_str[16];
static char g_vt_str[16];
static int  g_vt_num = 7;

/*	Clean shutdown flag - set by SIGTERM/SIGINT handler so the
	main loop can exit gracefully instead of looping forever.
*/
static volatile sig_atomic_t g_shutdown = 0;

/*	PID of the currently running X server, if any.
	Used by the shutdown path to clean up on SIGTERM.
*/
static volatile pid_t g_xserver_pid = 0;
