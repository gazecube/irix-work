#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <arcs/io.h>
#include <libsc.h>
#include <libsk.h>
#include <sys/types.h>

/*
 * Native X64/UEFI runtime boundary for standalone libsc.
 */
int symmon = 0;
void
debug(char *Reason)
{
    (void)Reason;
}

int
splhi(void)
{
    return 0;
}

int
splockspl(lock_t Lock, int (*Raise)(void))
{
    (void)Lock;
    return Raise ? Raise() : 0;
}

void
spunlockspl(lock_t Lock, int OldSpl)
{
    (void)Lock;
    (void)OldSpl;
}

static int
ArcsKeyToByte(const EFI_INPUT_KEY *Key, unsigned char *Byte)
{
    if (!Key || !Byte)
        return 0;

    if (Key->UnicodeChar != 0) {
        *Byte = (Key->UnicodeChar <= 0xff) ?
            (unsigned char)Key->UnicodeChar : (unsigned char)'?';
        return 1;
    }

    if (Key->ScanCode == 0x17) {
        *Byte = 0x1b;
        return 1;
    }

    return 0;
}

LONG
Read(ULONG FileId, void *Buffer, ULONG Count, ULONG *ReadCount)
{
    EFI_INPUT_KEY Key;
    unsigned char *Out = (unsigned char *)Buffer;
    ULONG Done = 0;

    if (ReadCount)
        *ReadCount = 0;
    if (FileId != StandardIn || !Buffer || Count == 0 ||
        !gST || !gST->ConIn || !gST->ConIn->ReadKeyStroke)
        return -1;

    while (Done == 0) {
        EFI_STATUS Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);

        if (Status == EFI_NOT_READY) {
            UINTN Which;
            if (!gST->ConIn->WaitForKey ||
                EFI_ERROR(gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Which)))
                return -1;
            continue;
        }
        if (EFI_ERROR(Status))
            return -1;

        if (ArcsKeyToByte(&Key, &Out[Done]))
            ++Done;
    }

    while (Done < Count) {
        EFI_STATUS Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
        if (EFI_ERROR(Status))
            break;
        if (ArcsKeyToByte(&Key, &Out[Done]))
            ++Done;
    }

    if (ReadCount)
        *ReadCount = Done;
    return 0;
}

LONG
Write(ULONG FileId, void *Buffer, ULONG Count, ULONG *WriteCount)
{
    if (WriteCount)
        *WriteCount = 0;
    if (FileId != StandardOut || !Buffer)
        return -1;

    /* This is the original SGI console route: standalone stdio -> ARCS
     * Write(StandardOut) -> pristine libsk htp_write().  GOP now exists only
     * below htp->fncs as the machine-specific graphics implementation. */
    if (!htp_write((char *)Buffer, (int)Count))
        return -1;

    if (WriteCount)
        *WriteCount = Count;
    return 0;
}

void
_errputc(char Character)
{
    ULONG Count;
    (void)Write(StandardOut, &Character, 1, &Count);
}
