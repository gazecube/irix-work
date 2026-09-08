#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include "uefi_boot.h"

extern char *make_bootfile(int rewind);
extern char *getenv(const char *Name);
extern int setenv(const char *Name, const char *Value);

static EFI_SYSTEM_TABLE *ArcsAutobootSystemTable;

void
ArcsPromAutobootInit(EFI_SYSTEM_TABLE *SystemTable)
{
    ArcsAutobootSystemTable = SystemTable;
}

/*
 * UEFI compatibility for the standalone pause(delay, "\r\n", "\033") call
 * used by the original libsc auto.c.  The policy remains SGI's: a nonzero
 * autoboot delay provides a maintenance-interrupt window and Escape cancels
 * the boot.  This shim is intentionally isolated so it can be replaced by
 * the original pause/ioctl/Signal runtime as those dependencies are ported.
 */
static int
ArcsAutobootPause(int Delay)
{
    EFI_EVENT Timer;
    EFI_EVENT Events[2];
    EFI_INPUT_KEY Key;
    EFI_STATUS Status;
    UINTN Count;
    UINTN Which;

    if (Delay <= 0)
        return 0;
    if (!ArcsAutobootSystemTable || !ArcsAutobootSystemTable->ConIn)
        return 0;

    Timer = NULL;
    Status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &Timer);
    if (EFI_ERROR(Status))
        return 0;

    Status = gBS->SetTimer(
        Timer,
        TimerRelative,
        (UINT64)Delay * 10000000ULL
        );
    if (EFI_ERROR(Status)) {
        gBS->CloseEvent(Timer);
        return 0;
    }

    Count = 0;
    if (ArcsAutobootSystemTable->ConIn->WaitForKey)
        Events[Count++] = ArcsAutobootSystemTable->ConIn->WaitForKey;
    Events[Count++] = Timer;

    for (;;) {
        Status = gBS->WaitForEvent(Count, Events, &Which);
        if (EFI_ERROR(Status))
            break;

        if (Events[Which] == Timer)
            break;

        while (!EFI_ERROR(ArcsAutobootSystemTable->ConIn->ReadKeyStroke(
                    ArcsAutobootSystemTable->ConIn, &Key))) {
            if (Key.ScanCode == SCAN_ESC) {
                gBS->CloseEvent(Timer);
                return 1;
            }
        }
    }

    gBS->CloseEvent(Timer);
    return 0;
}

/*
 * First UEFI integration checkpoint for the original libsc auto.c policy.
 *
 * The bootfile iteration is kept identical in shape: make_bootfile(1)
 * rewinds the SystemPartition/OSLoader lists, subsequent make_bootfile(0)
 * calls advance fallbacks, kernname is updated, and Execute() is the final
 * machine-specific boundary.
 *
 * The original auto.c also owns TIOCINTRCHAR, SIGINT/setjmp handling and the
 * graphical "Stop for Maintenance" progress dialog.  Those dependencies are
 * still being ported; the delay/Escape semantics above are the temporary UEFI
 * implementation of its pause() boundary, not a replacement boot policy.
 */
int
autoboot(int delay, char *message1, char *message2)
{
    char *bf;
    int bfcount = 0;
    INT32 rc = 1;

    (void)message1;
    (void)message2;

    if (delay && ArcsAutobootPause(delay))
        return 0;

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
