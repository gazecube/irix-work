#include "DirectoryCheck.h"

#include <sys/stat.h>
#include <sys/vfs.h>

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif
#ifndef EXT2_SUPER_MAGIC
#define EXT2_SUPER_MAGIC 0xEF53
#endif
#ifndef BTRFS_SUPER_MAGIC
#define BTRFS_SUPER_MAGIC 0x9123683E
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

namespace clogin {

static bool isKnownLocal(long type)
{
    switch (static_cast<unsigned long>(type)) {
    case XFS_SUPER_MAGIC:
    case EXT2_SUPER_MAGIC:
    case BTRFS_SUPER_MAGIC:
    case TMPFS_MAGIC:
        return true;
    default:
        return false;
    }
}

DirectoryState checkDirectoryPortable(const std::string &path)
{
    if (path.empty() || path[0] != '/' || path == "/dev/null")
        return kDirectoryOther;

    struct statfs fs;
    if (::statfs(path.c_str(), &fs) == 0) {
        if (static_cast<unsigned long>(fs.f_type) == NFS_SUPER_MAGIC)
            return kDirectoryNfs;
        if (isKnownLocal(fs.f_type))
            return kDirectoryLocal;
    }

    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return kDirectoryMissing;

    return kDirectoryOther;
}

} // namespace clogin
