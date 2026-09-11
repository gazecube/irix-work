#include "Broker.h"

#include "DirModel.h"
#include "FileModel.h"
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
     * Original ImdBroker::normalizeDNA() @ 0x09746f48 looks up the factory for
     * dna.type() and delegates normalization through that factory. Directory
     * population deliberately creates ImdUnknownFileModel DNA; the original
     * ImdFileModelFactory::normalize() resolves that filesystem-backed DNA to
     * its concrete model class later.
     *
     * Reconstruct the same boundary for the built-in host models. The selector
     * remains unchanged; only the model type is resolved from the file path.
     */
    if (dna.type().empty() || dna.selector().empty())
        return false;

    if (dna.type() == ImdFileModel::unknownTypeName()) {
        const ImdString path = ImdFileModel::fileNameFromDNA(dna);
        if (path.empty())
            return false;

        const ImdString resolvedType = ImdFileModel::dnaTypeFromPath(path);
        if (resolvedType.empty() || resolvedType == ImdFileModel::unknownTypeName())
            return false;

        dna = ImdDNA(resolvedType, dna.selector());
    }

    return true;
}

ImdModel *
ImdBroker::createBuiltInModel(const ImdDNA &dna)
{
    /*
     * Binary-backed architecture: BuiltInFactories registers model factories
     * by DNA type and getModel() dispatches through that registry.
     */
    if (dna.type() == "ImdDirModel")
        return new ImdDirModel(dna);

    return new ImdModel(dna);
}
