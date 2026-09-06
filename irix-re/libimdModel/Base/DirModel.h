#ifndef IRIX_RE_LIBIMDMODEL_DIRMODEL_H
#define IRIX_RE_LIBIMDMODEL_DIRMODEL_H

#include "Model.h"

#include <vector>

class ImdDirModel : public ImdModel
{
public:
    explicit ImdDirModel(const ImdDNA &dna);
    virtual ~ImdDirModel();

    virtual void *createChildIterator();
    virtual ImdModel *getNextChild(void *iterator);
    virtual void resetChildIterator(void *iterator);
    virtual void destroyChildIterator(void *iterator);
    virtual void update(bool force);

private:
    struct Iterator
    {
        unsigned int index;
    };

    void rebuildChildren();

    std::vector<ImdModel *> _children;
};

#endif /* IRIX_RE_LIBIMDMODEL_DIRMODEL_H */
