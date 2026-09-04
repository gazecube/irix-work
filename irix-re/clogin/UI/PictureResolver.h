#ifndef CLOGIN_PICTURE_RESOLVER_H
#define CLOGIN_PICTURE_RESOLVER_H

#include <functional>
#include <string>

struct passwd;

struct CloginPicturePolicy
{
    // Values match the original PwdNameList/pandora booleans.
    int howDisplay = 0;   // 0 = display faces, 1 = display icons
    int whereIcons = 1;   // 0 = pictures locally, 1 = pictures anywhere
};

struct CloginPictureResult
{
    enum Kind
    {
        kNone,
        kUserPicture,
        kMachineDefault,
        kUserDefault
    } kind = kNone;

    std::string path;
};

class CloginPictureResolver
{
public:
    // checkDirectory must return the same classification convention as the
    // caller wants to use. The original findUserPicture() accepts a home
    // directory for its first probe only when checkDirectory(home) == 0 in
    // local-picture mode.
    using DirectoryClassifier = std::function<int(const char *)>;

    static CloginPictureResult resolve(const passwd &pw,
                                       const CloginPicturePolicy &policy,
                                       bool isBuiltInName,
                                       const DirectoryClassifier &checkDirectory);
};

#endif
