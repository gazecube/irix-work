#include "DirModel.h"

#include "Attribute.h"
#include "FileModel.h"

#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

static bool
isDirectoryPath(const std::string &path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

} /* namespace */

ImdDirModel::ImdDirModel(const ImdDNA &dna)
    : ImdModel(dna)
{
    rebuildChildren();
}

ImdDirModel::~ImdDirModel()
{
    for (std::vector<ImdModel *>::iterator it = _children.begin();
         it != _children.end(); ++it)
        delete *it;
}

bool
ImdDirModel::addClientCallback(Callback callback,
                               void *clientData,
                               bool childCallback)
{
    const bool added = ImdModel::addClientCallback(callback,
                                                   clientData,
                                                   childCallback);
    if (!added || !childCallback)
        return added;

    /*
     * Binary-backed initial-child notification grammar from
     * ImdDirModel::addClientCallback() @ 0x09747a84.
     *
     * The original temporarily makes the newly registered CallbackInfo the
     * batch client, emits:
     *   ImdAttrBool(0xEFFF004B, true)
     *   ImdAttrDNA (0xEFFF004C, child DNA) for every current child
     *   ImdAttrBool(0xEFFF004D, true)
     * then sends that batch only to the registering client.
     *
     * The original symbolic names for these three attribute IDs have not yet
     * been recovered; keep the numeric IDs literal.
     */
    ImdAttrBool beginExists(0xEFFF004B, true);
    ImdAttrBool endExists(0xEFFF004D, true);
    std::vector<ImdAttrDNA *> childAttributes;
    std::vector<const ImdAttribute *> batch;

    batch.push_back(&beginExists);

    for (std::vector<ImdModel *>::const_iterator it = _children.begin();
         it != _children.end(); ++it) {
        ImdAttrDNA *attribute = new ImdAttrDNA(0xEFFF004C, (*it)->getDNA());
        childAttributes.push_back(attribute);
        batch.push_back(attribute);
    }

    batch.push_back(&endExists);

    callback(getDNA(),
             batch.empty() ? 0 : &batch[0],
             static_cast<int>(batch.size()),
             clientData);

    for (std::vector<ImdAttrDNA *>::iterator it = childAttributes.begin();
         it != childAttributes.end(); ++it)
        delete *it;

    /* File-monitor activation on first child client remains to be restored. */
    return true;
}

bool
ImdDirModel::removeClientCallback(Callback callback,
                                  void *clientData,
                                  bool childCallback)
{
    /*
     * Original ImdDirModel::removeClientCallback() delegates removal to the
     * base model and also owns first/last-child-client monitor transitions.
     * Monitoring is not part of the initial desktop population checkpoint.
     */
    return ImdModel::removeClientCallback(callback, clientData, childCallback);
}

void *
ImdDirModel::createChildIterator()
{
    Iterator *iterator = new Iterator;
    iterator->index = 0;
    return iterator;
}

ImdModel *
ImdDirModel::getNextChild(void *opaque)
{
    Iterator *iterator = static_cast<Iterator *>(opaque);
    if (!iterator || iterator->index >= _children.size())
        return 0;
    return _children[iterator->index++];
}

void
ImdDirModel::resetChildIterator(void *opaque)
{
    Iterator *iterator = static_cast<Iterator *>(opaque);
    if (iterator)
        iterator->index = 0;
}

void
ImdDirModel::destroyChildIterator(void *opaque)
{
    delete static_cast<Iterator *>(opaque);
}

void
ImdDirModel::update(bool force)
{
    if (force)
        rebuildChildren();
    ImdModel::update(force);
}

void
ImdDirModel::rebuildChildren()
{
    for (std::vector<ImdModel *>::iterator it = _children.begin();
         it != _children.end(); ++it)
        delete *it;
    _children.clear();

    const ImdString path = ImdFileModel::fileNameFromDNA(getDNA());
    if (path.empty())
        return;

    DIR *dir = opendir(path.c_str());
    if (!dir)
        return;

    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        std::string childPath(path.stdString());
        if (childPath.empty() || childPath[childPath.size() - 1] != '/')
            childPath += '/';
        childPath += entry->d_name;

        const ImdString selector =
            ImdFileModel::dnaSelectorFromPath(ImdString(childPath), ImdString());

        /*
         * dnaTypeFromPath() maps directories to ImdDirModel::kTypeName and
         * ordinary filesystem objects to ImdFileModel::kTypeName. Local
         * static-initialization disassembly proves those two kTypeName values
         * are "ImdDirModel" and "ImdFileModel" respectively: each is built
         * from the corresponding SGI type_info name. RefModel/symlink child
         * creation is still deferred until that path is restored natively.
         */
        const ImdString type(isDirectoryPath(childPath)
                                 ? "ImdDirModel"
                                 : "ImdFileModel");
        _children.push_back(new ImdModel(ImdDNA(type, selector)));
    }

    closedir(dir);
}
