#include "FileModel.h"

#include <limits.h>
#include <string>
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

ImdString
ImdFileModel::dnaSelectorFromPath(const ImdString &path,
                                  const ImdString &host)
{
    /*
     * Binary-backed reconstruction from libimdModel.so
     * dnaSelectorFromPath__12ImdFileModelSGRC9ImdStringT1
     * at 0x09741374, size 248.
     *
     * The original copies ImdFileModel::_urlPrefix ("file://"), appends the
     * supplied host when non-null or ImdFileModel::getLocalHostName() when the
     * host argument is ImdString::kNull, then appends the path when non-null.
     *
     * Native seam: the reconstructed ImdString does not expose the original
     * distinct kNull sentinel. An empty host selects the original local-host
     * branch, and an empty path means no path suffix.
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
