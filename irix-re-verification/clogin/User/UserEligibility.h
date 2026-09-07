#ifndef CLOGIN_USER_ELIGIBILITY_H
#define CLOGIN_USER_ELIGIBILITY_H

#include "../Base/PwdNameList.h"

#include <pwd.h>
#include <string>

struct UserEligibilityContext
{
    const PwdNameList *pwdNameList = nullptr;
    bool alreadyKnown = false;
    int directoryCheckResult = -1;
};

bool cloginPathIsIgnored(const std::string &path);
bool cloginUserEligible(const passwd &pw, const UserEligibilityContext &context);

#endif
