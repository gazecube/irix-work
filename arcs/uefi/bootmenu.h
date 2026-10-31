#ifndef ARCS_UEFI_BOOTMENU_H
#define ARCS_UEFI_BOOTMENU_H

#include <Uefi.h>

/*
 * ARCS boot manager.
 *
 * Entries come from \EFI\ARCS\boot.conf on the ESP the PROM was loaded from,
 * plus any other EFI loaders found on the machine's ESPs (systemd-boot, GRUB,
 * Windows, rEFInd ...).  boot.conf format, one key per line, '#' comments:
 *
 *   timeout=5                  seconds before the default entry boots (0 = no wait)
 *   default=Gentoo Linux       title of the default entry
 *   entry=Gentoo Linux         starts a new entry
 *     icon=linux               linux | chain | windows | irix | disk
 *     kernel=\EFI\ARCS\bzImage.efi   EFI-stub kernel on the PROM's own ESP
 *     options=root=... rw      kernel command line (OSLoadOptions)
 *   entry=Cachy
 *     chainload=\EFI\systemd\systemd-bootx64.efi   any EFI image, on any ESP
 *
 * With no boot.conf, one entry is made from arcs.env (the previous behaviour).
 */

typedef enum {
    ARCS_START_PROCEED,     /* kernel chosen: run the normal autoboot path */
    ARCS_START_CANCELLED,   /* Escape / nothing started: back to the menu  */
    ARCS_START_RAN          /* another image ran and returned              */
} ARCS_START_RESULT;

/* Power-on: TRUE = let the normal autoboot path run (one entry, or boot.conf
 * names a default=).  FALSE = several entries: skip autoboot, show the
 * maintenance menu. */
BOOLEAN ArcsBootMenuPowerOn(VOID);

/* Maintenance menu "Start System": show the picker when there is a choice. */
ARCS_START_RESULT ArcsBootMenuStartSystem(EFI_SYSTEM_TABLE *SystemTable);

/* Shift held (best effort): Start System then boots verbose into the textport. */
BOOLEAN ArcsBootMenuShiftHeld(EFI_SYSTEM_TABLE *SystemTable);

extern int ArcsBootPickerChose;

#endif
