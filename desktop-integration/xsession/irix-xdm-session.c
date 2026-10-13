/* Session program for the original IRIX xdm: hand over to the native
 * irix-session launcher.  A real x86 binary so the IRIX shell that xdm
 * starts can exec it without interpreting the (host) launcher script. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* xdm does not create a logind/PAM session, so the per-user runtime
 * environment host applications expect is missing.  Provide it when the
 * user's runtime directory exists (it does while the user manager runs,
 * e.g. with `loginctl enable-linger <user>`). */
static void user_runtime_env(void)
{
    char path[128];
    char bus[160];
    uid_t uid = getuid();
    snprintf(path, sizeof(path), "/run/user/%u", (unsigned)uid);
    if (access(path, X_OK) != 0)
        return;
    if (!getenv("XDG_RUNTIME_DIR"))
        setenv("XDG_RUNTIME_DIR", path, 1);
    snprintf(bus, sizeof(bus), "unix:path=%s/bus", path);
    if (!getenv("DBUS_SESSION_BUS_ADDRESS"))
        setenv("DBUS_SESSION_BUS_ADDRESS", bus, 1);
    if (!getenv("XDG_SESSION_TYPE"))
        setenv("XDG_SESSION_TYPE", "x11", 1);
}

int main(int argc, char **argv)
{
    const char *s = getenv("IRIX_SESSION");
    (void)argc; (void)argv;
    user_runtime_env();
    if (!s || !*s)
        s = "/home/nekron/Projects/irix-work/build-irix-re/recomp/quick/irix-session";
    execl("/bin/sh", "sh", s, (char *)0);
    perror("irix-xdm-session: exec");
    return 127;
}
