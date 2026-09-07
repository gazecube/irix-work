#include "AutoLogin.h"

#include <cstdio>
#include <unistd.h>

namespace Clogin
{
std::string consumeAutoLoginUser(const char *enablePath, const char *userPath)
{
    if (enablePath == nullptr || userPath == nullptr)
        return {};

    if (::access(enablePath, F_OK) != 0)
        return {};

    // Original checkAutoLogin() unlinks the one-shot marker immediately,
    // before opening /etc/autologin or validating the account.
    ::unlink(enablePath);

    std::FILE *file = std::fopen(userPath, "r");
    if (file == nullptr)
        return {};

    char username[512] = {};
    const int scanned = std::fscanf(file, "%511s", username);
    std::fclose(file);

    return scanned == 1 ? std::string(username) : std::string();
}
}
