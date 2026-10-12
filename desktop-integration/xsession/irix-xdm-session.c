/* Session program for the original IRIX xdm: hand over to the native
 * irix-session launcher.  A real x86 binary so the IRIX shell that xdm
 * starts can exec it without interpreting the (host) launcher script. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *s = getenv("IRIX_SESSION");
    (void)argc; (void)argv;
    if (!s || !*s)
        s = "/home/nekron/Projects/irix-work/build-irix-re/recomp/quick/irix-session";
    execl("/bin/sh", "sh", s, (char *)0);
    perror("irix-xdm-session: exec");
    return 127;
}
