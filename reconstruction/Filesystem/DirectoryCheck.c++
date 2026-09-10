#include "DirectoryCheck.h"

#include <sys/stat.h>
#include <sys/vfs.h>

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif

namespace clogin {

DirectoryState checkDirectoryPortable(const std::string &path)
{
    if (path.empty() || path[0] != '/' || path == "/dev/null")
        return kDirectoryOther;

    /*
     * Preserve the original checkDirectory() numeric contract.
     *
     * The shipped IRIX binary classifies NFS as 1, native IRIX local
     * filesystems (efs/xfs) as 0, and everything else as 2.  A Linux
     * filesystem such as ext4, btrfs, or tmpfs must therefore remain OTHER
     * here rather than being re-labelled LOCAL for host convenience.
     *
     * Callers are responsible for their own state-2 behavior.  In particular,
     * pandora::createIconList() performs an ordinary stat() fallback for state
     * 2, while PandoraComponent::findUserPicture() requires state 0 exactly in
     * local-picture mode.
     */
    struct statfs fs;
    if (::statfs(path.c_str(), &fs) == 0) {
        if (static_cast<unsigned long>(fs.f_type) == NFS_SUPER_MAGIC)
            return kDirectoryNfs;
    }

    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return kDirectoryMissing;

    return kDirectoryOther;
}

} // namespace clogin
