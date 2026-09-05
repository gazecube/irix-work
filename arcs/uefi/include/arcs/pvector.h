#ifndef ARCS_UEFI_COMPAT_PVECTOR_H
#define ARCS_UEFI_COMPAT_PVECTOR_H

#include <stand_htport.h>

struct arcs_uefi_pvector {
    struct htp_state *(*gethtp)(void);
};

extern struct arcs_uefi_pvector *__PTV;

#endif
