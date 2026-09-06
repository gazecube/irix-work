#include "DirModel.h"

#include "FileModel.h"

#include <dirent.h>
#include <string>
#include <sys/stat.h>

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
         * Binary-backed part: dnaTypeFromPath() maps directories to
         * ImdDirModel::kTypeName and ordinary filesystem objects to
         * ImdFileModel::kTypeName. The exact RefModel child path for symlinks
         * is intentionally deferred until its kTypeName literal and factory
         * behavior are recovered in the native layer.
         */
        const ImdString type(isDirectoryPath(childPath) ? "Directory" : "File");
        _children.push_back(new ImdModel(ImdDNA(type, selector)));
    }

    closedir(dir);
}
