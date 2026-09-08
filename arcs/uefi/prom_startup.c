#include <Uefi.h>

#include "prom_startup.h"

extern char *getenv(const char *Name);
extern int autoboot(int delay, char *message1, char *message2);

#define AUTO_DELAY 5
#define AUTO_MESSAGE1 0

/*
 * UEFI integration of the original libsk/ml/startup.c autoboot policy.
 *
 * Original SGI behavior:
 *   - autoboot only when AutoLoad begins with 'y' or 'Y'
 *   - suppress autoboot when NoAutoLoad exists
 *   - pass AUTO_DELAY (5 seconds) to autoboot()
 *   - after interruption/failure, continue to interactive maintenance mode
 *
 * The surrounding EnterInteractiveMode()/console-mode mechanics remain in the
 * UEFI entry/input layer for now.  Keeping this function separate preserves
 * the original startup policy boundary so the stock startup.c body can replace
 * it as the remaining standalone console/runtime dependencies are imported.
 */
void
startup(void)
{
    char *cp;
    char bootchar;
    char *stopboot;

    cp = getenv("AutoLoad");
    bootchar = cp ? *cp : 0;
    stopboot = getenv("NoAutoLoad");

    if (!stopboot && (bootchar == 'y' || bootchar == 'Y'))
        (void)autoboot(AUTO_DELAY, AUTO_MESSAGE1, 0);
}
