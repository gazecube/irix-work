#include "UserEligibility.h"

#include <sys/stat.h>

bool cloginPathIsIgnored(const std::string &path)
{
    for (const std::string &prefix : PwdNameList::getIgnoreList()) {
        if (!prefix.empty() && path.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}

bool cloginUserEligible(const passwd &pw, const UserEligibilityContext &context)
{
    if (pw.pw_name == nullptr || pw.pw_name[0] == '\0')
        return false;
    if (pw.pw_dir == nullptr || pw.pw_dir[0] == '\0')
        return false;
    if (pw.pw_passwd != nullptr && std::string(pw.pw_passwd) == "*")
        return false;
    if (context.alreadyKnown)
        return false;

    if (context.pwdNameList != nullptr) {
        const PwdNameListEntry *entry = context.pwdNameList->findEntry(pw.pw_name);
        if (entry != nullptr && (entry->flags &
            (PwdNameListEntry::kNoShow | PwdNameListEntry::kNoIcon)) != 0)
            return false;
    }

    if (cloginPathIsIgnored(pw.pw_dir))
        return false;

    // Exact original behavior observed in createIconList():
    // result 3 is rejected; result 2 requires stat() + directory type.
    // Semantic enum names for these values are not yet proven.
    if (context.directoryCheckResult == 3)
        return false;
    if (context.directoryCheckResult == 2) {
        struct stat st;
        if (::stat(pw.pw_dir, &st) != 0)
            return false;
        if (!S_ISDIR(st.st_mode))
            return false;
    }

    return true;
}
