#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include <gfxgui.h>

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

void *
guimalloc(size_t Size)
{
    return malloc(Size);
}

void
addObject(struct gui_obj *Obj)
{
    /* Not needed by drawBackground().  Kept only so the original text.c
     * can be linked unchanged during the first renderer milestone. */
    (void)Obj;
}
