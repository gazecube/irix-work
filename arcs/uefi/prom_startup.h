#ifndef ARCS_PROM_STARTUP_H
#define ARCS_PROM_STARTUP_H

#include <Uefi.h>

void ArcsPromAutobootInit(EFI_SYSTEM_TABLE *SystemTable);
void startup(void);

#endif
