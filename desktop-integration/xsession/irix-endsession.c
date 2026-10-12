/*
 * irix-endsession [-f]
 *
 * Log Out for the IRIX desktop session.  Toolchest runs menu commands through
 * the guest /bin/sh, so this must be a native executable, not a host shell
 * script (the guest shell has none of the host tools a script would need).
 *
 *   1. confirm: IRIX xconfirm if it works, otherwise a plain Xlib xmessage
 *      dialog (-f skips the question)
 *   2. ask 4Dwm to end the session (tellwm end_session)
 *   3. signal the running session script (SIGUSR1 via its per-display
 *      pidfile), which stops 4Dwm, Toolchest and fm so the X session ends
 *
 * Environment: DISPLAY, IRIX_RECOMP_ROOT, XDG_RUNTIME_DIR (optional).
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Append one line to $XDG_CACHE_HOME/irix-endsession.log (or ~/.cache).
 * Log Out runs from Toolchest with no terminal, so this is the only way to
 * see why it did or did not end the session. */
static void logf_(const char *fmt, ...)
{
    char path[4096];
    const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    FILE *f;
    va_list ap;
    if (cache && *cache)
        snprintf(path, sizeof(path), "%s/irix-endsession.log", cache);
    else if (home && *home)
        snprintf(path, sizeof(path), "%s/.cache/irix-endsession.log", home);
    else
        return;
    f = fopen(path, "a");
    if (!f)
        return;
    fprintf(f, "%ld pid=%ld ", (long)time(NULL), (long)getpid());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* Run argv; if capture is non-NULL read up to cap-1 bytes of the given
 * output stream (1 = stdout, 2 = stderr) into it.  Returns the exit status,
 * or -1 if it could not run. */
static int run(char *const argv[], char *capture, size_t cap, int stream)
{
    int fds[2] = { -1, -1 };
    pid_t pid;
    int status = 0;

    if (capture && pipe(fds) < 0)
        return -1;
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (capture) {
            dup2(fds[1], stream);
            close(fds[0]);
            close(fds[1]);
        }
        if (!capture || stream != 2)
            freopen("/dev/null", "w", stderr);
        if (capture && stream == 2)
            freopen("/dev/null", "w", stdout);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (capture) {
        size_t n = 0;
        ssize_t r;
        close(fds[1]);
        while (n + 1 < cap && (r = read(fds[0], capture + n, cap - 1 - n)) > 0)
            n += (size_t)r;
        capture[n] = '\0';
        close(fds[0]);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int confirm(const char *display, const char *root)
{
    char xconfirm[4096], reply[256];
    int rc;
    char *a1[] = { xconfirm, "-display", (char *)display, "-header", "Confirm",
                   "-icon", "question", "-B", "No", "-b", "Yes",
                   "-t", "Do you want to log out now?", NULL };
    char *a2[] = { "xmessage", "-display", (char *)display, "-center",
                   "-default", "No", "-buttons", "No:1,Yes:0",
                   "Do you want to log out now?", NULL };

    /* Toolchest hands children an almost empty environment; xconfirm needs
     * the same guest display transport the session launcher sets up. */
    setenv("IRIX_RECOMP_ROOT", root, 0);
    if (display[0] == ':') {
        char guest_display[256];
        snprintf(guest_display, sizeof(guest_display), "unix/%s", display);
        setenv("IRIX_GUEST_DISPLAY", guest_display, 0);
    }
    setenv("IRIX_XT_XM_STABILITY", "0", 0);
    snprintf(xconfirm, sizeof(xconfirm), "%s/usr/bin/X11/xconfirm", root);
    reply[0] = '\0';
    rc = run(a1, reply, sizeof(reply), 1);
    logf_("xconfirm rc=%d reply=[%.120s]", rc, reply);
    if (rc >= 0 && rc != 126 && rc != 127 && rc != 255) {
        /* xconfirm ran and the user answered (or dismissed it: empty reply,
         * which is "no").  Never ask a second question. */
        /* xconfirm prints the pressed button's label on stdout; look for a
         * line that is exactly "Yes". */
        char *line = reply, *nl;
        int yes = 0;
        while (line && *line) {
            nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strcmp(line, "Yes") == 0)
                yes = 1;
            line = nl ? nl + 1 : 0;
        }
        return rc == 0 && yes;
    }
    /* xconfirm could not run (not found, killed, or exited 255 on a startup
     * failure such as a missing display transport): fall back to xmessage
     * (exit 0 = Yes) so a broken dialog never silently cancels Log Out. */
    return run(a2, NULL, 0, 1) == 0;
}

int main(int argc, char **argv)
{
    const char *display = getenv("DISPLAY");
    const char *root = getenv("IRIX_RECOMP_ROOT");
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char tellwm[4096], pidfile[4096], tag[128], buf[32];
    char *tw[] = { tellwm, "end_session", NULL };
    int force = argc > 1 && strcmp(argv[1], "-f") == 0;
    size_t i, j = 0;
    FILE *f;
    long pid;

    if (!display || !*display || !root || !*root) {
        fprintf(stderr, "irix-endsession: DISPLAY and IRIX_RECOMP_ROOT required\n");
        return 2;
    }
    /* Guest processes see the display as "unix/:0" (the transport form the
     * session launcher selects); the pidfile and X tools want ":0". */
    if (!strncmp(display, "unix/", 5u))
        display += 5;
    logf_("start display=%s root=%s xdg_runtime=%s force=%d", display, root,
          rt ? rt : "(unset)", force);
    if (!force && !confirm(display, root)) {
        logf_("not confirmed; leaving session running");
        return 1;
    }

    /* Exactly what /usr/bin/X11/endsession does once the user has confirmed:
     * if 4Dwm answers the test message, tell it the session is ending (it
     * saves the session and then removes _SGI_SESSION_PROPERTY, which is
     * what lets the login session finish); clean up _SGI_TELL_WM in case
     * 4Dwm died.  If 4Dwm is not there, remove _SGI_SESSION_PROPERTY
     * ourselves. */
    {
        char xprop[4096];
        char *tw_t[] = { tellwm, "-t", "end_session", NULL };
        char *rm_tell[] = { xprop, "-display", (char *)display, "-root",
                            "-remove", "_SGI_TELL_WM", NULL };
        char *rm_sess[] = { xprop, "-display", (char *)display, "-root",
                            "-remove", "_SGI_SESSION_PROPERTY", NULL };
        snprintf(tellwm, sizeof(tellwm), "%s/usr/bin/X11/tellwm", root);
        snprintf(xprop, sizeof(xprop), "%s/usr/bin/X11/xprop", root);
        setenv("DISPLAY", display, 1);
        if (run(tw_t, NULL, 0, 1) == 0) {
            logf_("tellwm -t end_session ok; sending end_session");
            logf_("tellwm end_session rc=%d", run(tw, NULL, 0, 1));
            run(rm_tell, NULL, 0, 1);
        } else {
            logf_("4Dwm not answering; removing _SGI_SESSION_PROPERTY");
            run(rm_sess, NULL, 0, 1);
        }
    }

    /* The session script waits for _SGI_SESSION_PROPERTY to disappear.  As a
     * last resort, if it is still published well after that, stop the
     * session script directly through its pidfile. */
    {
        char xprop[4096], out[512];
        char *q[] = { xprop, "-display", (char *)display, "-root",
                      "_SGI_SESSION_PROPERTY", NULL };
        int still = 0;
        snprintf(xprop, sizeof(xprop), "%s/usr/bin/X11/xprop", root);
        for (i = 0; i < 20; ++i) {
            out[0] = '\0';
            run(q, out, sizeof(out), 1);
            still = strstr(out, "(ATOM)") != NULL;
            if (!still)
                break;
            sleep(1);
        }
        if (!still) {
            logf_("session property cleared; session is ending");
            return 0;
        }
        logf_("property still set after 20s; signalling session script");
    }

    for (i = 0; display[i] && j + 1 < sizeof(tag); ++i)
        tag[j++] = isalnum((unsigned char)display[i]) ? display[i] : '_';
    tag[j] = '\0';
    snprintf(pidfile, sizeof(pidfile), "%s/irix-session-%s.pid",
             rt && *rt ? rt : "/tmp", tag);
    f = fopen(pidfile, "r");
    logf_("pidfile %s %s", pidfile, f ? "found" : "MISSING");
    if (!f)
        return 3;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 3;
    }
    fclose(f);
    pid = strtol(buf, NULL, 10);
    if (pid <= 1 || kill((pid_t)pid, SIGUSR1) < 0) {
        logf_("cannot signal session pid %ld: %s", pid, strerror(errno));
        return 3;
    }
    logf_("sent SIGUSR1 to session pid %ld", pid);
    return 0;
}
