#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>

#include <arcs/pvector.h>
#include <libsc.h>

int _prom = 1;

static struct htp_state *
ArcsCompatGetHtp(void)
{
    return 0;
}

static struct arcs_uefi_pvector ArcsCompatPvector = {
    ArcsCompatGetHtp
};

struct arcs_uefi_pvector *__PTV = &ArcsCompatPvector;

void *
malloc(size_t Size)
{
    return AllocatePool((UINTN)Size);
}

void
free(void *Ptr)
{
    if (Ptr)
        FreePool(Ptr);
}

size_t
strlen(const char *String)
{
    const char *P = String;

    while (*P)
        ++P;

    return (size_t)(P - String);
}

char *
strcpy(char *Dest, const char *Source)
{
    char *D = Dest;

    while ((*D++ = *Source++) != '\0')
        ;

    return Dest;
}

char *
getenv(const char *Name)
{
    /* Preserve the stock standalone GUI logo path during early bring-up.
     * ARCS environment storage will replace this compatibility hook. */
    static char Yes[] = "y";

    if (Name && Name[0] == 's' && Name[1] == 'g' && Name[2] == 'i' &&
        Name[3] == 'l' && Name[4] == 'o' && Name[5] == 'g' &&
        Name[6] == 'o' && Name[7] == '\0')
        return Yes;

    return 0;
}

void
panic(const char *Message)
{
    (void)Message;
    for (;;)
        ;
}
