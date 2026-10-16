#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *target = getenv("NETSCAPE_BUNDLE_LAUNCHER");
    char *sh_argv0;

    (void)argc;
    if (!target || !*target)
        target = "/opt/netscape/netscape";
    sh_argv0 = argv[0];
    argv[0] = (char *)target;
    execv(target, argv);
    fprintf(stderr, "%s: cannot exec %s\n", sh_argv0, target);
    return 127;
}
