#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <arcs/io.h>
#include <libsc.h>
#include <sys/types.h>

/*
 * Native X64/UEFI runtime boundary for standalone libsc.
 *
 * These symbols are supplied by machine-dependent PROM/libsk code on SGI
 * hardware.  Keep their original public contracts so the pristine libsc
 * parser/editor/stdio sources remain unchanged while the real X64 platform
 * backend is filled in beneath them.
 */

/* symmon/debug are only reached by pristine edit_gets() when SPB->DebugBlock
 * is non-NULL.  The UEFI SPB compatibility object deliberately leaves that
 * field NULL, so retain the original symbols without inventing a debugger. */
int symmon = 0;
void
debug(char *Reason)
{
    (void)Reason;
}

/* SGI's UI lock starts at zero and is not used until machine-dependent PROM
 * code initializes the lock subsystem.  The current native UEFI PROM is
 * single-threaded, so these architecture hooks preserve that uninitialized
 * behavior.  stdio_lock.c supplies the original arcs_ui_lock object. */
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

/* Convert one EFI key into the byte-oriented ARCS console contract. */
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

    /* EFI scan code 0x17 is Escape.  The original standalone console exposes
     * Escape as the ASCII byte consumed by the PROM editor/autoboot logic. */
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

    /* ARCS Read is allowed to return a short transfer.  Drain immediately
     * available keystrokes without blocking for the caller's full Count. */
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
    unsigned char *In = (unsigned char *)Buffer;
    ULONG Done = 0;

    if (WriteCount)
        *WriteCount = 0;
    if (FileId != StandardOut || !Buffer ||
        !gST || !gST->ConOut || !gST->ConOut->OutputString)
        return -1;

    /* Temporary platform sink while the original host textport becomes the
     * canonical owner of the already-working GOP backend.  Keep this narrow:
     * standalone stdio still calls the original ARCS Write() interface, and
     * no pristine SGI source knows about EFI text output. */
    while (Done < Count) {
        CHAR16 Text[2];
        Text[0] = (CHAR16)In[Done];
        Text[1] = 0;
        if (EFI_ERROR(gST->ConOut->OutputString(gST->ConOut, Text)))
            break;
        ++Done;
    }

    if (WriteCount)
        *WriteCount = Done;
    return (Done == Count) ? 0 : -1;
}

void
_errputc(char Character)
{
    ULONG Count;
    (void)Write(StandardOut, &Character, 1, &Count);
}
