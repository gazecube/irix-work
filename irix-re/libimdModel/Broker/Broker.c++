#include "Broker.h"

#include "DirModel.h"
#include "Model.h"

namespace {

static ImdBroker *gContextBroker = 0;

static std::string
modelKey(const ImdDNA &dna)
{
    return dna.serialize().stdString();
}

} /* namespace */

ImdBroker::ImdBroker()
{
}

ImdBroker::~ImdBroker()
{
    for (ModelMap::iterator it = _models.begin(); it != _models.end(); ++it)
        delete it->second;
}

ImdBroker *
ImdBroker::contextBroker()
{
    if (!gContextBroker)
        gContextBroker = new ImdBroker;
    return gContextBroker;
}

void
ImdBroker::destroyContextBroker()
{
    delete gContextBroker;
    gContextBroker = 0;
}

ImdModel *
ImdBroker::getExistingModel(const ImdDNA &dna)
{
    ModelMap::iterator it = _models.find(modelKey(dna));
    if (it == _models.end())
        return 0;

    it->second->ref();
    return it->second;
}

ImdModel *
ImdBroker::getModel(const ImdDNA &inputDNA)
{
    if (inputDNA.type().empty() || inputDNA.selector().empty())
        return 0;

    ImdDNA dna(inputDNA);
    if (!normalizeDNA(dna))
        return 0;

    const std::string key = modelKey(dna);
    ModelMap::iterator existing = _models.find(key);
    if (existing != _models.end()) {
        existing->second->ref();
        return existing->second;
    }

    ImdModel *model = createBuiltInModel(dna);
    if (!model || !model->isValid()) {
        delete model;
        return 0;
    }

    _models[key] = model;
    model->ref();
    return model;
}

void
ImdBroker::releaseModel(ImdModel *model)
{
    if (model)
        model->unref();
}

bool
ImdBroker::normalizeDNA(ImdDNA &dna)
{
    /*
     * Original Broker::normalizeDNA() dispatches to the registered factory
     * for dna.type() and writes the normalized DNA back into the argument.
     * The Directory selector produced by desktopContainerDNA() is already in
     * the proven canonical file://host/path form, so the first native factory
     * path is identity normalization. Other model classes remain unresolved.
     */
    return !dna.type().empty() && !dna.selector().empty();
}

ImdModel *
ImdBroker::createBuiltInModel(const ImdDNA &dna)
{
    /*
     * Binary-backed architecture: BuiltInFactories registers model factories
     * by DNA type and getModel() dispatches through that registry. The native
     * reconstruction starts with the one factory needed by the desktop root:
     * ImdDirModel::kTypeName == "Directory".
     */
    if (dna.type() == "Directory")
        return new ImdDirModel(dna);

    return new ImdModel(dna);
}
