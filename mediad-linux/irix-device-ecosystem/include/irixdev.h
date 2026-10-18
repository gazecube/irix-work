#ifndef IRIXDEV_H
#define IRIXDEV_H
#include <stddef.h>
#include <sys/types.h>
#include "invent_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
struct irixdev_device {
    unsigned controller, target, lun;
    int optical;
    char block[4096], scsi[4096], raw_name[128], identity[4096];
};
/* Manifest selected by IRIXDEV_STATE, default /var/lib/irix-devices. */
int irixdev_list(struct irixdev_device *, size_t capacity);
int irixdev_lookup(const char *, struct irixdev_device *, int *slice);
int irixdev_resolve(const char *, char *, size_t);
int irixdev_address(unsigned, unsigned, unsigned, struct irixdev_device *);
int setinvent(void);
inventory_t *getinvent(void);
void endinvent(void);
char *filename_to_scsiname(const char *, char *, int *);
char *filename_to_diskname(const char *, int raw, int partno, char *, int *);
int path_to_partnum(char *);
#ifdef __cplusplus
}
#endif
#endif
