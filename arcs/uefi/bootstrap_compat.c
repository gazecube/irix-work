#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>

#include <arcs/pvector.h>
#include <arcs/spb.h>
#include <libsc.h>
#include <stringlist.h>

int _prom = 1;

/*
 * X64/UEFI backing for the standalone ARCS environment.
 *
 * The original PROM exposes both environ/environ_str and getenv/_setenv to
 * libsc commands.  Keep that original in-memory representation here while
 * deliberately leaving persistence/NVRAM outside this compatibility layer.
 */
struct string_list environ_str;
char **environ = environ_str.strptrs;
static int ArcsEnvInitialized;

static spb_t ArcsCompatSpb;

spb_t *
get_spb(void)
{
    return &ArcsCompatSpb;
}

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

void
bcopy(const void *Source, void *Dest, size_t Count)
{
    const unsigned char *S = Source;
    unsigned char *D = Dest;

    if (D == S || Count == 0)
        return;

    if (D < S || D >= S + Count) {
        while (Count--)
            *D++ = *S++;
    } else {
        S += Count;
        D += Count;
        while (Count--)
            *--D = *--S;
    }
}

void
bzero(void *Dest, size_t Count)
{
    unsigned char *D = Dest;

    while (Count--)
        *D++ = 0;
}

int
bcmp(const void *Left, const void *Right, size_t Count)
{
    const unsigned char *A = Left;
    const unsigned char *B = Right;

    while (Count--) {
        if (*A++ != *B++)
            return 1;
    }
    return 0;
}

static int
ArcsCompatLower(int C)
{
    if (C >= 'A' && C <= 'Z')
        return C + ('a' - 'A');
    return C;
}

int
strcasecmp(const char *A, const char *B)
{
    unsigned char CA, CB;

    do {
        CA = (unsigned char)ArcsCompatLower((unsigned char)*A++);
        CB = (unsigned char)ArcsCompatLower((unsigned char)*B++);
        if (CA != CB)
            return (int)CA - (int)CB;
    } while (CA != 0);
    return 0;
}

int
strncasecmp(const char *A, const char *B, size_t Count)
{
    unsigned char CA, CB;

    while (Count--) {
        CA = (unsigned char)ArcsCompatLower((unsigned char)*A++);
        CB = (unsigned char)ArcsCompatLower((unsigned char)*B++);
        if (CA != CB)
            return (int)CA - (int)CB;
        if (CA == 0)
            break;
    }
    return 0;
}

char *
strncpy(char *Dest, const char *Source, size_t Count)
{
    char *Ret = Dest;

    while (Count && *Source) {
        *Dest++ = *Source++;
        --Count;
    }
    while (Count) {
        *Dest++ = '\0';
        --Count;
    }
    return Ret;
}

char *
strcat(char *Dest, const char *Source)
{
    char *Ret = Dest;

    while (*Dest)
        ++Dest;
    while ((*Dest++ = *Source++) != '\0')
        ;
    return Ret;
}

static void
ArcsCompatInitEnv(void)
{
    if (ArcsEnvInitialized)
        return;

    init_str(&environ_str);
    new_str2("sgilogo", "y", &environ_str);
    new_str2("AutoLoad", "Y", &environ_str);
    new_str2("SystemPartition",
             "scsi(0)disk(1)rdisk(0)partition(1)", &environ_str);
    new_str2("OSLoader", "EFI/FreeBSD/loader.efi", &environ_str);
    new_str2("OSLoadOptions", "auto", &environ_str);
    new_str2("kernname", "", &environ_str);
    environ = environ_str.strptrs;
    ArcsEnvInitialized = 1;
}

char *
getenv(const char *Name)
{
    char *Entry;
    char *Value;

    if (!Name)
        return 0;

    ArcsCompatInitEnv();
    Entry = find_str((char *)Name, &environ_str);
    if (!Entry)
        return 0;

    Value = Entry;
    while (*Value && *Value != '=')
        ++Value;
    return *Value == '=' ? Value + 1 : 0;
}

int
setenv(char *Name, char *Value)
{
    if (!Name || !Value)
        return -1;

    ArcsCompatInitEnv();
    replace_str(Name, Value, &environ_str);
    environ = environ_str.strptrs;
    return 0;
}

/*
 * Machine-dependent _setenv normally owns NVRAM policy.  The UEFI port keeps
 * the original command contract but intentionally supplies only the volatile
 * environment until a source-faithful persistent ARCS store is selected.
 */
int
_setenv(char *Name, char *Value, int Override)
{
    (void)Override;

    if (!Name || !Value)
        return -1;

    ArcsCompatInitEnv();
    if (*Value == '\0')
        delete_str(Name, &environ_str);
    else
        replace_str(Name, Value, &environ_str);
    environ = environ_str.strptrs;
    return 0;
}

void
panic(char *Message, ...)
{
    (void)Message;
    for (;;)
        ;
}
