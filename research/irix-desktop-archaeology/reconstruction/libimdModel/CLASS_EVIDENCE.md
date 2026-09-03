# libimdModel class evidence manifest

Names below are observed in DWARF public-name data. Parameter types, return types, inheritance, access control, overload sets, and object layout are intentionally not guessed here.

## `ImdModel`

- `ImdModel::getSelf`
- `ImdModel::getParentDNA`
- `ImdModel::addClientCallback`
- `ImdModel::removeClientCallback`
- `ImdModel::createChildIterator`
- `ImdModel::getNextChild`
- `ImdModel::resetChildIterator`
- `ImdModel::destroyChildIterator`
- `ImdModel::getAttr`
- `ImdModel::setAttr`
- `ImdModel::isAttrWritable`
- `ImdModel::getName`
- `ImdModel::getAltName`
- `ImdModel::getDNA`
- `ImdModel::update`
- `ImdModel::isValid`
- `ImdModel::notifyClients`
- `ImdModel::activate`
- `ImdModel::deactivate`
- `ImdModel::invalidate`
- `ImdModel::validate`
- `ImdModel::validDNA`
- `ImdModel::invalidateDNA`
- `ImdModel::ref`
- `ImdModel::unref`
- `ImdModel::getRef`
- `ImdModel::doOutput`
- `ImdModel::addToBatch`
- `ImdModel::sendBatch`
- `ImdModel::setBatchClient`

## `ImdDNA`

- `ImdDNA::copyAttributes`
- `ImdDNA::createIterator`
- `ImdDNA::exactlyEquals`
- `ImdDNA::getAttr`
- `ImdDNA::getNumAttributes`
- `ImdDNA::operator!=`
- `ImdDNA::operator=`
- `ImdDNA::operator==`
- `ImdDNA::removeAttr`
- `ImdDNA::selector`
- `ImdDNA::serialize`
- `ImdDNA::setAttr`
- `ImdDNA::type`

## `ImdGlobalEnv`

- `ImdGlobalEnv::abbrevRemoteHostName`
- `ImdGlobalEnv::getFileUnreachableError`
- `ImdGlobalEnv::getLocalHostName`
- `ImdGlobalEnv::hostMapRoot`
- `ImdGlobalEnv::isAutoMountPoint`
- `ImdGlobalEnv::isAutoMountedPath`
- `ImdGlobalEnv::isLocalHostName`
- `ImdGlobalEnv::isModelOpen`
- `ImdGlobalEnv::isURL`
- `ImdGlobalEnv::mountedPathFromDNA`
- `ImdGlobalEnv::protoDNAFromString`
- `ImdGlobalEnv::realPath`
- `ImdGlobalEnv::setCurDir`
- `ImdGlobalEnv::setModelOpen`
- `ImdGlobalEnv::stripAutoMountPrefix`

## `ImdBroker`

- `ImdBroker::altNameFromDNA`
- `ImdBroker::contextBroker`
- `ImdBroker::destroyContextBroker`
- `ImdBroker::getExistingModel`
- `ImdBroker::getModel`
- `ImdBroker::nameFromDNA`
- `ImdBroker::normalizeDNA`
- `ImdBroker::parentDNAFromDNA`
- `ImdBroker::registerFactory`
- `ImdBroker::releaseModel`
- `ImdBroker::unregisterFactory`
- `ImdBroker::validModelClass`
