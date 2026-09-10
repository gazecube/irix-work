#include "LoginTheUserModel.h"

namespace clogin
{

LoginAction loginPrecheckAction(
    int directoryState,
    bool homeUsableAfterFallback,
    std::size_t storedPasswordLength,
    bool mandatoryPassword,
    bool passwordRequired)
{
    if (directoryState == 3)
        return kLoginRejectNoHome;

    if (directoryState == 2 && !homeUsableAfterFallback)
        return kLoginRejectNoHome;

    /*
     * LoginTheUser() calls strlen(pw_passwd), then isMandPass(), then
     * isPassReq(). When a mandatory password is configured but the account's
     * stored password is empty, the original path presents err_badUser.
     */
    if (mandatoryPassword && storedPasswordLength == 0)
        return kLoginRejectBadUser;

    /*
     * A system requiring passwords with an empty account password enters the
     * CreateGetNewPwDialog() path before pwd0_checkPwd().
     */
    if (passwordRequired && storedPasswordLength == 0)
        return kLoginRequestNewPassword;

    return kLoginCheckPassword;
}

LoginAction loginPasswordStatusAction(int pwd0Status)
{
    switch (pwd0Status)
    {
        case 0:  return kLoginContinueSuccess;
        case 1:  return kLoginRejectIncorrectPassword;
        case 2:  return kLoginRequestPasswordAgeDialog;
        case 3:  return kLoginRejectRestricted;
        case 14: return kLoginRejectInactive;
        default: return kLoginUnhandledPasswordStatus;
    }
}

const char *loginActionName(LoginAction action)
{
    switch (action)
    {
        case kLoginCheckPassword:             return "check-password";
        case kLoginRejectNoHome:              return "reject-no-home";
        case kLoginRejectBadUser:             return "reject-bad-user";
        case kLoginRequestNewPassword:        return "request-new-password";
        case kLoginRejectIncorrectPassword:   return "reject-incorrect-password";
        case kLoginRequestPasswordAgeDialog:  return "request-password-age-dialog";
        case kLoginRejectRestricted:          return "reject-restricted";
        case kLoginRejectInactive:            return "reject-inactive";
        case kLoginContinueSuccess:            return "continue-success";
        case kLoginUnhandledPasswordStatus:   return "unhandled-password-status";
    }
    return "unknown";
}

} // namespace clogin
