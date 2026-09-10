#include "PictureResolver.h"

#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>

namespace
{
bool readableByOpen(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

CloginPictureResult userPicture(const std::string &path)
{
    CloginPictureResult result;
    result.kind = CloginPictureResult::kUserPicture;
    result.path = path;
    return result;
}
}

CloginPictureResult CloginPictureResolver::resolve(
    const passwd &pw,
    const CloginPicturePolicy &policy,
    bool isBuiltInName,
    const DirectoryClassifier &checkDirectory)
{
    CloginPictureResult none;

    if (pw.pw_name == nullptr || pw.pw_dir == nullptr)
        return none;

    // findUserPicture(): in local-picture mode the home probe is rejected
    // unless checkDirectory(home) returns zero.
    if (policy.whereIcons != 1) {
        if (!checkDirectory || checkDirectory(pw.pw_dir) != 0)
            return none;
    }

    std::string path = pw.pw_dir;
    path += "/.icons/login.icon";
    if (readableByOpen(path))
        return userPicture(path);

    // Exact original gate after the per-home image probe.
    if (policy.whereIcons != 1 && policy.howDisplay == 0)
        return none;

    path = "/usr/local/lib/faces/";
    path += pw.pw_name;
    if (readableByOpen(path))
        return userPicture(path);

    // The original checks the live pandora whereIcons field again here.
    // In normal initialization this is the same policy copied into the
    // component at +0x98.
    if (policy.whereIcons != 1)
        return none;

    path = "/usr/lib/faces/";
    path += pw.pw_name;
    if (readableByOpen(path))
        return userPicture(path);

    CloginPictureResult fallback;
    if (isBuiltInName) {
        fallback.kind = CloginPictureResult::kMachineDefault;
        fallback.path = "/usr/local/lib/faces/machineUserIcon";
    } else {
        fallback.kind = CloginPictureResult::kUserDefault;
        fallback.path = "/usr/local/lib/faces/userIcon";
    }
    return fallback;
}
