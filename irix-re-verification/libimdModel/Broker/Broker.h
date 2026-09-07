#ifndef IRIX_RE_LIBIMDMODEL_BROKER_H
#define IRIX_RE_LIBIMDMODEL_BROKER_H

#include "ImdDNA.h"

#include <map>
#include <string>

class ImdModel;

class ImdBroker
{
public:
    static ImdBroker *contextBroker();
    static void destroyContextBroker();

    ImdModel *getExistingModel(const ImdDNA &dna);
    ImdModel *getModel(const ImdDNA &dna);
    void releaseModel(ImdModel *model);
    bool normalizeDNA(ImdDNA &dna);

private:
    ImdBroker();
    ~ImdBroker();
    ImdBroker(const ImdBroker &);
    ImdBroker &operator=(const ImdBroker &);

    ImdModel *createBuiltInModel(const ImdDNA &dna);

    typedef std::map<std::string, ImdModel *> ModelMap;
    ModelMap _models;
};

#endif /* IRIX_RE_LIBIMDMODEL_BROKER_H */
