/*
 * waitmap pass-through for the native IRIX desktop session.
 *
 * Toolchest runs every f.checkexec / f.exec command as
 *     /usr/lib/desktop/waitmap /bin/sh -c "<command>"
 * The original waitmap waits for the launched program to map a window so the
 * launch-effect animation can end.  Under the guest runner it traps drawing
 * text with an X font set, so no menu command ever starts.  This replacement
 * simply runs its arguments.  Install over
 * $IRIX_RECOMP_ROOT/usr/lib/desktop/waitmap.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * The guest runner starts this helper with an empty environment (the guest
 * process never populated environ).  Recover the launching process's
 * environment from /proc/<ppid>/environ so commands see DISPLAY,
 * IRIX_RECOMP_ROOT and friends.  Variables already set are not overridden.
 */
static void import_parent_environment(void)
{
    char path[64];
    FILE *f;
    char *buf = 0;
    size_t cap = 0, len = 0;
    int c;
    size_t i;

    if (getenv("DISPLAY"))
        return;
    snprintf(path, sizeof(path), "/proc/%ld/environ", (long)getppid());
    f = fopen(path, "r");
    if (!f)
        return;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            cap = cap ? cap * 2 : 4096;
            buf = realloc(buf, cap);
            if (!buf) { fclose(f); return; }
        }
        buf[len++] = (char)c;
    }
    fclose(f);
    if (!buf)
        return;
    buf[len] = '\0';
    for (i = 0; i < len; ) {
        char *entry = buf + i;
        char *eq = strchr(entry, '=');
        size_t n = strlen(entry);
        if (eq && eq != entry) {
            *eq = '\0';
            if (!getenv(entry))
                setenv(entry, eq + 1, 0);
        }
        i += n + 1;
    }
    free(buf);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 0;
    import_parent_environment();
    execvp(argv[1], argv + 1);
    perror(argv[1]);
    return 127;
}
