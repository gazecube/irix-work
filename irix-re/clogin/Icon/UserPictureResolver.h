#ifndef CLOGIN_USER_PICTURE_RESOLVER_H
#define CLOGIN_USER_PICTURE_RESOLVER_H

#include "../Base/PwdNameList.h"

#include <pwd.h>
#include <string>

struct UserPicturePolicy
{
    int howDisplay = 0; // 0 faces, 1 icons
    int whereIcons = 1; // 0 locally, 1 anywhere
};

struct UserPictureResult
{
    std::string path;
    bool found = false;
};

UserPictureResult resolveUserPicture(const passwd &pw,
    const PwdNameList &names,
    const UserPicturePolicy &policy,
    int directoryCheckResult = 0);

#endif
