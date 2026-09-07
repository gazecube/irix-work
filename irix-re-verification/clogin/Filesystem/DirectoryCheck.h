#ifndef IRIX_RE_CLOGIN_DIRECTORYCHECK_H
#define IRIX_RE_CLOGIN_DIRECTORYCHECK_H

#include <string>

namespace clogin {

enum DirectoryState {
    kDirectoryLocal = 0,
    kDirectoryNfs = 1,
    kDirectoryOther = 2,
    kDirectoryMissing = 3
};

DirectoryState checkDirectoryPortable(const std::string &path);

} // namespace clogin

#endif
