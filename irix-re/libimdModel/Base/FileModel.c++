#include "FileModel.h"

#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

static ImdString
localHostName()
{
    char host[256];
    host[0] = '\0';
    if (gethostname(host, sizeof(host) - 1) != 0)
        return ImdString();
    host[sizeof(host) - 1] = '\0';
    return ImdString(host);
}

} /* namespace */

const char *
ImdFileModel::unknownTypeName()
{
    /*
     * Original static initializer in libimdModel.so constructs
     * ImdFileModel::kUnknownType from the literal "ImdUnknownFileModel".
     */
    return "ImdUnknownFileModel";
}

ImdString
ImdFileModel::dnaSelectorFromPath(const ImdString &path,
                                  const ImdString &host)
{
    /*
     * Binary-backed reconstruction from libimdModel.so
     * dnaSelectorFromPath__12ImdFileModelSGRC9ImdStringT1
     * at 0x09741374, size 248.
     */
    std::string selector("file://");
    if (host.empty())
        selector += localHostName().stdString();
    else
        selector += host.stdString();
    if (!path.empty())
        selector += path.stdString();
    return ImdString(selector);
}

ImdString
ImdFileModel::fileNameFromDNA(const ImdDNA &dna)
{
    /*
     * Binary-backed reconstruction from libimdModel.so
     * fileNameFromDNA__12ImdFileModelSGRC6ImdDNA
     * at 0x0974146c, size 360.
     */
    const std::string &selector = dna.selector().stdString();
    const std::string prefix("file://");
    if (selector.size() <= prefix.size())
        return ImdString();
    if (selector.compare(0, prefix.size(), prefix) != 0)
        return ImdString();
    const std::string::size_type slash = selector.find('/', prefix.size());
    if (slash == std::string::npos)
        return ImdString();
    return ImdString(selector.substr(slash));
}

ImdString
ImdFileModel::dnaTypeFromPath(const ImdString &path)
{
    /*
     * Binary-backed reconstruction from
     * dnaTypeFromPath__12ImdFileModelSGRC9ImdString @ 0x09746d68.
     * The original performs lstat() and maps S_IFDIR to ImdDirModel,
     * S_IFLNK to ImdRefModel, and ordinary filesystem objects to
     * ImdFileModel.
     *
     * Native seam: ImdRefModel has not yet been reconstructed. Preserve the
     * existing Linux desktop-reference behavior by following a symlink only
     * far enough to keep directory references directory-facing. Once
     * ImdRefModel is native this exception should be removed.
     */
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
        return ImdString(unknownTypeName());

    if (S_ISDIR(st.st_mode))
        return ImdString("ImdDirModel");

    if (S_ISLNK(st.st_mode)) {
        struct stat target;
        if (stat(path.c_str(), &target) == 0 && S_ISDIR(target.st_mode))
            return ImdString("ImdDirModel");
        return ImdString("ImdRefModel");
    }

    return ImdString("ImdFileModel");
}
