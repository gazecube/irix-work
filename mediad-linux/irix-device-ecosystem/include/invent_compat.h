#pragma once
#include <sys/types.h>
typedef struct inventory_s {
    struct inventory_s *inv_next;
    int inv_class, inv_type;
    unsigned int inv_controller, inv_unit;
    int inv_state;
} inventory_t;
#define INV_DISK 2
#define INV_PARALLEL 5
#define INV_TAPE 6
#define INV_SCSI 9
#define INV_SCSIFLOPPY 5
#define INV_PCCARD 21
#define INV_CDROM 5
#define INV_OPTICAL 7
#define INV_EPP_ECP_PLP 6

#define INV_SCSIQIC 1
