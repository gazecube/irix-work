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

/* Show the picker (if there is anything to pick, or a key is held) and run
 * the chosen entry.  Chainload entries are started here and never return on
 * success.  A kernel entry sets OSLoadOptions and returns so the normal
 * autoboot path starts it.  Returns TRUE if the picker took over the choice. */
BOOLEAN ArcsBootMenuRun(EFI_SYSTEM_TABLE *SystemTable);

/* Set once the user (or the picker timeout) has chosen: autoboot skips its
 * own countdown. */
extern int ArcsBootPickerChose;

#endif
