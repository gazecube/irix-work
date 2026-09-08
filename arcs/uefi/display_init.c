#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EdidActive.h>
#include <Protocol/GraphicsOutput.h>

#include "display_init.h"

/* Reconstructed UEFI display glue.
 *
 * The SGI GUI itself remains resolution-independent and uses gfxWidth()/
 * gfxHeight().  This code only translates the platform's preferred display
 * timing into the GOP mode that should be active before the original SGI
 * drawing code starts.
 */

static BOOLEAN
ArcsPreferredEdidMode(UINT32 *Width, UINT32 *Height)
{
    EFI_STATUS Status;
    EFI_EDID_ACTIVE_PROTOCOL *Edid;
    CONST UINT8 *Dtd;
    UINT32 HActive;
    UINT32 VActive;

    Edid = NULL;
    Status = gBS->LocateProtocol(
        &gEfiEdidActiveProtocolGuid,
        NULL,
        (VOID **)&Edid
        );
    if (EFI_ERROR(Status) || Edid == NULL || Edid->Edid == NULL ||
        Edid->SizeOfEdid < 72)
        return FALSE;

    /* EDID 1.x preferred timing is the first detailed timing descriptor,
     * starting at byte 54.  A zero pixel clock means this is not a timing
     * descriptor, so leave the GOP mode unchanged in that case.
     */
    Dtd = Edid->Edid + 54;
    if (Dtd[0] == 0 && Dtd[1] == 0)
        return FALSE;

    HActive = (UINT32)Dtd[2] | ((UINT32)(Dtd[4] & 0xF0) << 4);
    VActive = (UINT32)Dtd[5] | ((UINT32)(Dtd[7] & 0xF0) << 4);

    if (HActive == 0 || VActive == 0)
        return FALSE;

    *Width = HActive;
    *Height = VActive;
    return TRUE;
}

static EFI_STATUS
ArcsSelectPreferredMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    UINT32 Width;
    UINT32 Height;
    UINT32 Mode;

    if (!ArcsPreferredEdidMode(&Width, &Height))
        return EFI_NOT_FOUND;

    for (Mode = 0; Mode < Gop->Mode->MaxMode; ++Mode) {
        EFI_STATUS Status;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
        UINTN InfoSize;

        Info = NULL;
        InfoSize = 0;
        Status = Gop->QueryMode(Gop, Mode, &InfoSize, &Info);
        if (EFI_ERROR(Status))
            continue;

        if (Info->HorizontalResolution == Width &&
            Info->VerticalResolution == Height) {
            FreePool(Info);
            if (Gop->Mode->Mode == Mode)
                return EFI_SUCCESS;
            return Gop->SetMode(Gop, Mode);
        }

        FreePool(Info);
    }

    return EFI_NOT_FOUND;
}

static EFI_STATUS
ArcsClearFramebuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;

    Black.Blue = 0;
    Black.Green = 0;
    Black.Red = 0;
    Black.Reserved = 0;

    return Gop->Blt(
        Gop,
        &Black,
        EfiBltVideoFill,
        0,
        0,
        0,
        0,
        Gop->Mode->Info->HorizontalResolution,
        Gop->Mode->Info->VerticalResolution,
        0
        );
}

EFI_STATUS
ArcsUefiPrepareDisplay(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    EFI_STATUS Status;

    if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL)
        return EFI_INVALID_PARAMETER;

    /* A missing/unsupported EDID mode is not fatal.  Keep the firmware's
     * current GOP mode and still clear it before the SGI GUI is drawn.
     */
    Status = ArcsSelectPreferredMode(Gop);
    if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND)
        return Status;

    return ArcsClearFramebuffer(Gop);
}
