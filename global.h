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

/*
 * Keep the password label and password input as distinct widget handles.
 * The original prototype temporarily aliased `password` to `textBox2`, which
 * caused the label pointer to be overwritten when gui.h constructed the input
 * widget.  The reconstructed UI now needs both independently so the prompt can
 * follow the original presentation state cleanly.
 */
Widget textBox1, textBox2, password, helpButton, powerButton, optionsButton,
	scrolledWin0, scrolledWin, hostLabel;
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
