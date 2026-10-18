/* Unprivileged stand-in for the setuid-root IRIX sysadm runpriv.
 *
 *   runpriv [-auth <auth>] <privilege> [args...]
 *
 * The real one checks /var/sysadm/privilege and switches to root.  Here there
 * is no setuid root and no privilege database: just run
 * /usr/sysadm/privbin/<privilege> as the invoking user, so sysadm admin
 * classes (SaDiskClass, SaRemovableMediaClass) can load.  It never escalates. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char path[1024];
    int i = 1;
    if (i + 1 < argc && !strcmp(argv[i], "-auth")) i += 2;
    if (i >= argc || strchr(argv[i], '/') || argv[i][0] == '.') {
        fprintf(stderr, "usage: %s [ -auth <auth> ] <privilege> ...\n", argv[0]);
        return 2;
    }
    snprintf(path, sizeof path, "/usr/sysadm/privbin/%s", argv[i]);
    execv(path, argv + i);
    perror(path);
    return 127;
}
