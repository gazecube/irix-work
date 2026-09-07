#include "UserPictureResolver.h"

#include <fcntl.h>
#include <unistd.h>

namespace
{
bool readableByOriginalOpen(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

UserPictureResult firstExisting(const std::string &path)
{
    return { path, readableByOriginalOpen(path) };
}
}

UserPictureResult resolveUserPicture(const passwd &pw,
    const PwdNameList &names,
    const UserPicturePolicy &policy,
    int directoryCheckResult)
{
    if (pw.pw_name == nullptr || pw.pw_dir == nullptr)
        return {};

    // Original findUserPicture(): when whereIcons != 1, checkDirectory(home)
    // is consulted before the per-user home icon. Non-zero aborts lookup.
    if (policy.whereIcons != 1 && directoryCheckResult != 0)
        return {};

    UserPictureResult result = firstExisting(
        std::string(pw.pw_dir) + "/.icons/login.icon");
    if (result.found)
        return result;

    // Binary-exact gate: /usr/local/lib/faces/$USER is attempted when
    // whereIcons == 1 OR howDisplay != 0.
    if (policy.whereIcons == 1 || policy.howDisplay != 0) {
        result = firstExisting(std::string("/usr/local/lib/faces/") + pw.pw_name);
        if (result.found)
            return result;
    }

    // component+0x98 and pandora::getWhereIcons() are the same policy copied
    // during initialize(), so the second original gate reduces to anywhere.
    if (policy.whereIcons == 1) {
        result = firstExisting(std::string("/usr/lib/faces/") + pw.pw_name);
        if (result.found)
            return result;
    }

    // Original isBuiltInName() is exactly PwdNameList::findEntry(name) != NULL.
    const bool builtIn = names.findEntry(pw.pw_name) != nullptr;
    result = firstExisting(builtIn
        ? "/usr/local/lib/faces/machineUserIcon"
        : "/usr/local/lib/faces/userIcon");
    return result;
}
