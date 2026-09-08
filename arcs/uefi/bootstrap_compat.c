#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>

#include <arcs/pvector.h>
#include <libsc.h>

int _prom = 1;

/*
 * Reconstructed bootstrap environment storage.
 *
 * The original libsc/libsk boot policy consumes AutoLoad, NoAutoLoad,
 * SystemPartition, OSLoader and OSLoadOptions through getenv()/setenv().
 * Keep those names and semantics at the compatibility boundary so original
 * bootname.c/startup/autoboot policy can run above it.  A persistent ARCS
 * environment backend will replace this fixed bootstrap store.
 */
static char ArcsEnvSgilogo[8] = "y";
static char ArcsEnvAutoLoad[8] = "Y";
static char ArcsEnvSystemPartition[128] =
    "scsi(0)disk(1)rdisk(0)partition(1)";
static char ArcsEnvOSLoader[128] = "EFI/FreeBSD/loader.efi";
static char ArcsEnvOSLoadOptions[128] = "auto";
static char ArcsEnvKernname[256] = "";

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

static int
ArcsCompatStreq(const char *A, const char *B)
{
    while (*A && *B && *A == *B) {
        ++A;
        ++B;
    }
    return *A == *B;
}

static void
ArcsCompatCopy(char *Dest, UINTN Capacity, const char *Source)
{
    UINTN I = 0;

    if (!Dest || Capacity == 0)
        return;

    while (Source && Source[I] && I + 1 < Capacity) {
        Dest[I] = Source[I];
        ++I;
    }
    Dest[I] = '\0';
}

char *
getenv(const char *Name)
{
    if (!Name)
        return 0;
    if (ArcsCompatStreq(Name, "sgilogo"))
        return ArcsEnvSgilogo;
    if (ArcsCompatStreq(Name, "AutoLoad"))
        return ArcsEnvAutoLoad;
    /* NoAutoLoad is intentionally absent by default.  Returning NULL here
     * preserves the original startup.c condition: !getenv("NoAutoLoad"). */
    if (ArcsCompatStreq(Name, "NoAutoLoad"))
        return 0;
    if (ArcsCompatStreq(Name, "SystemPartition"))
        return ArcsEnvSystemPartition;
    if (ArcsCompatStreq(Name, "OSLoader"))
        return ArcsEnvOSLoader;
    if (ArcsCompatStreq(Name, "OSLoadOptions"))
        return ArcsEnvOSLoadOptions;
    if (ArcsCompatStreq(Name, "kernname"))
        return ArcsEnvKernname;
    return 0;
}

int
setenv(const char *Name, const char *Value)
{
    if (!Name || !Value)
        return -1;
    if (ArcsCompatStreq(Name, "AutoLoad")) {
        ArcsCompatCopy(ArcsEnvAutoLoad, sizeof(ArcsEnvAutoLoad), Value);
        return 0;
    }
    if (ArcsCompatStreq(Name, "SystemPartition")) {
        ArcsCompatCopy(ArcsEnvSystemPartition,
                       sizeof(ArcsEnvSystemPartition), Value);
        return 0;
    }
    if (ArcsCompatStreq(Name, "OSLoader")) {
        ArcsCompatCopy(ArcsEnvOSLoader, sizeof(ArcsEnvOSLoader), Value);
        return 0;
    }
    if (ArcsCompatStreq(Name, "OSLoadOptions")) {
        ArcsCompatCopy(ArcsEnvOSLoadOptions,
                       sizeof(ArcsEnvOSLoadOptions), Value);
        return 0;
    }
    if (ArcsCompatStreq(Name, "kernname")) {
        ArcsCompatCopy(ArcsEnvKernname, sizeof(ArcsEnvKernname), Value);
        return 0;
    }
    return -1;
}

void
panic(const char *Message)
{
    (void)Message;
    for (;;)
        ;
}
