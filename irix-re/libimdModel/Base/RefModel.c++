#include "RefModel.h"

#include "FileModel.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {

static bool
isDirectory(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool
resolveDesktopDirectory(char *path, size_t pathSize)
{
    const char *overridePath;
    const char *home;

    if (!path || pathSize == 0)
        return false;

    path[0] = '\0';

    /*
     * Binary-backed behavior from ImdRefModel::desktopContainerDNA()
     * (libimdModel.so 0x0974aae4, size 376): SGI_DESKTOP_DIRECTORY is used
     * only when it names an existing directory.
     */
    overridePath = getenv("SGI_DESKTOP_DIRECTORY");
    if (isDirectory(overridePath)) {
        if (strlen(overridePath) >= pathSize)
            return false;
        strcpy(path, overridePath);
        return true;
    }

    /*
     * The original fallback obtains the current session path through
     * imdCreateSessionPath(), applies dirname(), and appends "/Desktop".
     * The extracted Xsession.dt establishes the corresponding user-visible
     * default as $HOME/Desktop. During native host execution we do not yet
     * have imdCreateSessionPath(), so resolve that proven session result
     * directly rather than inventing a different desktop directory.
     */
    home = getenv("HOME");
    if (!home || !*home)
        return false;

    return snprintf(path, pathSize, "%s/Desktop", home) < (int)pathSize;
}

} /* namespace */

ImdDNA
ImdRefModel::desktopContainerDNA()
{
    char path[PATH_MAX];

    if (!resolveDesktopDirectory(path, sizeof(path)))
        return ImdDNA();

    /*
     * Original sequence recovered from desktopContainerDNA():
     *   ImdString(path)
     *   ImdFileModel::dnaSelectorFromPath(path, ImdString::kNull)
     *   ImdDNA(ImdDirModel::kTypeName, selector)
     *
     * Fresh local static-initialization disassembly resolves
     * ImdDirModel::kTypeName itself: the library initializes it from
     * name__9type_info(__T_11ImdDirModel), whose type-info name string is
     * exactly "ImdDirModel". The nearby "Directory" string belongs to a
     * different semantic layer and must not be substituted for kTypeName.
     */
    const ImdString desktopPath(path);
    const ImdString selector =
        ImdFileModel::dnaSelectorFromPath(desktopPath, ImdString());

    return ImdDNA(ImdString("ImdDirModel"), selector);
}
