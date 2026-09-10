#ifndef IRIX_RE_CLOGIN_LOGIN_THE_USER_MODEL_H
#define IRIX_RE_CLOGIN_LOGIN_THE_USER_MODEL_H

#include <cstddef>

namespace clogin
{

/*
 * Dependency-free model of the control-flow decisions recovered from the
 * shipped IRIX clogin LoginTheUser() routine at 0x10014830.
 *
 * This is source-level reconstruction only. It does not claim the original
 * xpasswd ABI, pwd0 implementation, or IRIX authentication backend.
 */
enum LoginAction
{
    kLoginCheckPassword,
    kLoginRejectNoHome,
    kLoginRejectBadUser,
    kLoginRequestNewPassword,
    kLoginRejectIncorrectPassword,
    kLoginRequestPasswordAgeDialog,
    kLoginRejectRestricted,
    kLoginRejectInactive,
    kLoginContinueSuccess,
    kLoginUnhandledPasswordStatus
};

/*
 * checkDirectory() state meanings recovered elsewhere:
 *   0 native/local
 *   1 NFS
 *   2 other/unknown; caller performs stat() fallback
 *   3 missing/not-directory
 *
 * Pass homeUsableAfterFallback=false only when state 2 was followed by the
 * binary-proven stat() fallback and that fallback failed or was not a dir.
 */
LoginAction loginPrecheckAction(
    int directoryState,
    bool homeUsableAfterFallback,
    std::size_t storedPasswordLength,
    bool mandatoryPassword,
    bool passwordRequired);

/* Map the recovered pwd0_checkPwd() return values used by LoginTheUser(). */
LoginAction loginPasswordStatusAction(int pwd0Status);

const char *loginActionName(LoginAction action);

} // namespace clogin

#endif /* IRIX_RE_CLOGIN_LOGIN_THE_USER_MODEL_H */
