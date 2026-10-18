#include <dirent.h>
#include <string.h>
static inline struct dirent *uw7_readdir_r2(DIR *d, struct dirent *buf)
{
    struct dirent *e = readdir(d);
    if (!e) return NULL;
    memcpy(buf, e, e->d_reclen);
    return buf;
}
#define readdir_r(d, b) uw7_readdir_r2((d), (b))
