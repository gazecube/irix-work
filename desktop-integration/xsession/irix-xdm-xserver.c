/* X server launcher for the original IRIX xdm.
 *
 * xdm executes the server named in Xservers itself, and it does so with the
 * IRIX exec path, which only reaches binaries inside the IRIX tree.  This is
 * a small native binary that lives in the tree and hands over to the host
 * server (Xorg by default; IRIX_XSERVER overrides, e.g. Xephyr for tests).
 * Arguments (display name, vtN, -auth ...) are passed through unchanged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *server = getenv("IRIX_XSERVER");
    if (!server || !*server)
        server = "/usr/bin/Xorg";
    argv[0] = (char *)server;
    execv(server, argv);
    perror("irix-xdm-xserver: exec");
    return 127;
}
