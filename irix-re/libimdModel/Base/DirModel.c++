#include "DirModel.h"

#include "Attribute.h"
#include "FileModel.h"

#include <dirent.h>
#include <string>
#include <vector>

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

    /*
     * Binary-backed ImdDirModel::_loadChildren() @ 0x0975b928 uses the
     * OpenDir()/ReadDir() wrappers, skips dot and dot-dot, then passes
     * ImdFileModel::kUnknownType into _addChild(). The directory scan itself
     * does not classify every entry with stat/lstat.
     *
     * Preserve that separation here: enumerate names and construct unknown
     * filesystem DNA. ImdBroker::normalizeDNA() owns the later resolution to
     * ImdFileModel/ImdDirModel/ImdRefModel, matching the original factory
     * boundary and avoiding an eager classification pass during readdir().
     */
    const ImdString unknownType(ImdFileModel::unknownTypeName());
    const std::string base(path.stdString());

    for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;

        std::string childPath(base);
        if (childPath.empty() || childPath[childPath.size() - 1] != '/')
            childPath += '/';
        childPath += entry->d_name;

        const ImdString selector =
            ImdFileModel::dnaSelectorFromPath(ImdString(childPath), ImdString());

        _children.push_back(new ImdModel(ImdDNA(unknownType, selector)));
    }

    closedir(dir);
}
