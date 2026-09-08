#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <stand_htport.h>
#include <gfxgui.h>
#include <guicore.h>
#include <style.h>

#include "uefi_boot.h"

extern char *make_bootfile(int rewind);
extern char *getenv(const char *Name);
extern int setenv(const char *Name, const char *Value);

static EFI_SYSTEM_TABLE *ArcsAutobootSystemTable;
static volatile int ArcsAutobootStopRequested;
static char *defaultmsg = "\n\nStarting up the system...\n\n";

/* Original IRIX 6.5.7m libsc/gui/gutil.c implementation. */
int
isGuiMode(void)
{
    return(gfxgui.htp && (gfxgui.htp->textport.state == GUI));
}

void
ArcsPromAutobootInit(EFI_SYSTEM_TABLE *SystemTable)
{
    ArcsAutobootSystemTable = SystemTable;
}

/* Original auto.c stop callback semantics, adapted only at the non-local-jump
 * boundary while setjmp/Signal are still being ported. */
static void
stopCallBack(struct Button *b, __scunsigned_t button)
{
    (void)b;
    (void)button;
    ArcsAutobootStopRequested = 1;
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
        if (ArcsAutobootStopRequested) {
            gBS->CloseEvent(Timer);
            return 1;
        }

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
    return ArcsAutobootStopRequested ? 1 : 0;
}

/*
 * UEFI integration checkpoint for original libsc auto.c.
 *
 * The SGI GUI portion is now restored with the original createDialog(),
 * placement, progress icon, button text and object lifetime.  The remaining
 * compatibility boundary is the standalone interrupt/pause runtime.
 */
int
autoboot(int delay, char *message1, char *message2)
{
    struct Dialog *d = 0;
    char *startingmsg;
    char *bf;
    int bfcount = 0;
    INT32 rc = 1;

    (void)message2;

    startingmsg = message1 ? message1 : defaultmsg;
    ArcsAutobootStopRequested = 0;

    if (isGuiMode()) {
        d = createDialog(startingmsg,DIALOGPROGRESS,DIALOGBIGFONT);
        moveObject(guiobj(d),d->gui.x1,
                   MSGY1-((d->gui.y2-d->gui.y1)>>1));
        if (delay) {
            struct Button *b;
            b = addDialogButton(d,DIALOGSTOP);
            addButtonText(b,"Stop for Maintenance");
            addButtonCallBack(b, stopCallBack, 1);
        }
        drawObject(guiobj(d));
    }

    if (delay && ArcsAutobootPause(delay)) {
        rc = 0;
        goto done;
    }

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
        rc = 0;
    else if (bfcount)
        rc = 22;
    else
        rc = 5;

done:
    if (d && isGuiMode())
        deleteObject(guiobj(d));

    return (int)rc;
}

/* Original pmenu_cmd.c item-1 service body, with giveup() deferred until the
 * rest of the PROM command/error runtime is linked into this build.
 */
int
menu_boot_sys(void)
{
    return autoboot(0, 0, 0);
}
