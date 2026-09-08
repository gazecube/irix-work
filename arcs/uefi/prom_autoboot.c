#include <Uefi.h>

#include "uefi_boot.h"

extern char *make_bootfile(int rewind);
extern char *getenv(const char *Name);
extern int setenv(const char *Name, const char *Value);

/*
 * First UEFI integration checkpoint for the original libsc auto.c policy.
 *
 * The original SGI implementation additionally owns pause(), TIOCINTRCHAR,
 * SIGINT/setjmp interruption, the GUI progress dialog and the "Stop for
 * Maintenance" button.  Those standalone-runtime dependencies are being
 * ported separately rather than replaced with new behavior here.
 *
 * The bootfile iteration itself is kept identical in shape: make_bootfile(1)
 * rewinds the SystemPartition/OSLoader lists, subsequent make_bootfile(0)
 * calls advance fallbacks, kernname is updated, and Execute() is the final
 * machine-specific boundary.
 */
int
autoboot(int delay, char *message1, char *message2)
{
    char *bf;
    int bfcount = 0;
    INT32 rc = 1;

    (void)delay;
    (void)message1;
    (void)message2;

    for (bf = make_bootfile(1); bf; bf = make_bootfile(0)) {
        char *argv[2];

        ++bfcount;
        argv[0] = bf;
        argv[1] = 0;

        if (!getenv("OSLoadOptions"))
            (void)setenv("OSLoadOptions", "auto");

        (void)setenv("kernname", bf);
        rc = Execute(bf, 1, argv, 0);
        if (rc == 0)
            break;
    }

    if (bf)
        return 0;
    if (bfcount)
        return 22; /* EINVAL in the original standalone environment. */
    return 5;      /* EIO in the original standalone environment. */
}

/* Original pmenu_cmd.c item-1 service body, with giveup() deferred until the
 * rest of the PROM command/error runtime is linked into this build.
 */
int
menu_boot_sys(void)
{
    return autoboot(0, 0, 0);
}
