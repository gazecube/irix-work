#ifndef _IRIX_SYS_MAC_LABEL_H
#define _IRIX_SYS_MAC_LABEL_H
typedef struct mac_label *mac_t;
#define MAC_CLEARED 1
struct clearance;
mac_t mac_get_proc(void);
mac_t mac_from_text(const char *);
int mac_set_proc(mac_t);
mac_t mac_set_moldy(mac_t);
int mac_equal(mac_t, mac_t);
int mac_free(void *);
int mac_clearedlbl(struct clearance *, mac_t);
#endif
