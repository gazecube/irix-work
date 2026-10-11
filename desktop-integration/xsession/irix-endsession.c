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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
    rc = run(a1, reply, sizeof(reply), 2);
    if (rc >= 0 && rc != 126 && rc != 127 && rc != 255) {
        /* xconfirm ran and the user answered (or dismissed it: empty reply,
         * which is "no").  Never ask a second question. */
        /* The answer arrives on stderr, next to the runner's own status
         * line, so look for a line that is exactly the button label. */
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
    if (!force && !confirm(display, root))
        return 1;

    snprintf(tellwm, sizeof(tellwm), "%s/usr/bin/X11/tellwm", root);
    (void)run(tw, NULL, 0, 1);
    sleep(1);

    for (i = 0; display[i] && j + 1 < sizeof(tag); ++i)
        tag[j++] = isalnum((unsigned char)display[i]) ? display[i] : '_';
    tag[j] = '\0';
    snprintf(pidfile, sizeof(pidfile), "%s/irix-session-%s.pid",
             rt && *rt ? rt : "/tmp", tag);
    f = fopen(pidfile, "r");
    if (!f) {
        fprintf(stderr, "irix-endsession: no session pidfile %s\n", pidfile);
        return 3;
    }
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 3;
    }
    fclose(f);
    pid = strtol(buf, NULL, 10);
    if (pid <= 1 || kill((pid_t)pid, SIGUSR1) < 0) {
        fprintf(stderr, "irix-endsession: cannot signal session %ld: %s\n",
                pid, strerror(errno));
        return 3;
    }
    return 0;
}
