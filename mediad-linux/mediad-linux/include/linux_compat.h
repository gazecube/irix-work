#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <limits.h>
#include <regex.h>
#include <dirent.h>
#include <sys/types.h>
typedef unsigned char uchar_t;
typedef unsigned int uint;
typedef void (*SIG_PF)(int);
#ifndef MNTMAXSTR
#define MNTMAXSTR 128
#endif
#ifndef MNTTYPE_NFS
#define MNTTYPE_NFS "nfs"
#endif
#ifndef MNTOPT_RO
#define MNTOPT_RO "ro"
#endif
#ifndef MNTOPT_RW
#define MNTOPT_RW "rw"
#endif
#ifdef __cplusplus
extern "C" {
#endif
char *re_comp(const char *);
int re_exec(const char *);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
const char *linux_state_path(const char *);
const char *linux_module_dir();
bool linux_actions_enabled();
int linux_media_mount(const char *irix_dev, const char *link);
int linux_media_unmount(const char *irix_dev, const char *link);
int linux_media_block(const char *irix_dev, char *blk, size_t n);
unsigned linux_media_owner_uid(void);
unsigned linux_media_owner_gid(void);
#endif
