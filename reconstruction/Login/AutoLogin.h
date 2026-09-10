#ifndef CLOGIN_AUTOLOGIN_H
#define CLOGIN_AUTOLOGIN_H

#include <string>

namespace Clogin
{
// Portable equivalent of the file-consumption portion of
// pandora::checkAutoLogin(). An empty string means no usable autologin user.
std::string consumeAutoLoginUser(
    const char *enablePath = "/etc/autologin.on",
    const char *userPath = "/etc/autologin");
}

#endif
