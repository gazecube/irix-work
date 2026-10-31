#ifndef ARCS_UEFI_MAINTENANCE_MENU_BRINGUP_H
#define ARCS_UEFI_MAINTENANCE_MENU_BRINGUP_H

#include <Uefi.h>

void ArcsDrawMaintenanceMenuBringup(void);
void ArcsMaintenanceMenuSelect(UINTN OneBasedIndex);

int ArcsMenuItemRect(int Index, int *X1, int *Y1, int *X2, int *Y2);
void ArcsMenuPark(int Park);

#endif
