#include "Model.h"

#include <string>

ImdModel::ImdModel(const ImdDNA &dna)
    : _clientCallbacks(0),
      _clientChildCallbacks(0),
      _flags(kValidFlag | kValidDNAFlag),
      _attributes(0),
      _selector(new ImdString(dna.selector())),
      _attributeDNA(new ImdDNA(dna)),
      _refCount(0),
      _batchClient(0),
      _deactivationDelay(0)
{
}

ImdModel::~ImdModel()
{
    delete _selector;
    delete _attributeDNA;
}

ImdModel *ImdModel::getSelf() { return this; }
ImdDNA ImdModel::getParentDNA() const { return ImdDNA(); }

bool ImdModel::addClientCallback(Callback, void *, bool) { return false; }
bool ImdModel::removeClientCallback(Callback, void *, bool) { return false; }

void *ImdModel::createChildIterator() { return 0; }
ImdModel *ImdModel::getNextChild(void *) { return 0; }
void ImdModel::resetChildIterator(void *) {}
void ImdModel::destroyChildIterator(void *) {}

const ImdAttribute *ImdModel::getAttr(unsigned int) const { return 0; }
bool ImdModel::setAttr(const ImdAttribute &) { return false; }
bool ImdModel::isAttrWritable(unsigned int) const { return false; }

ImdString ImdModel::getName() const
{
    if (!_selector)
        return ImdString();

    const std::string &s = _selector->stdString();
    const std::string::size_type slash = s.find_last_of('/');
    if (slash == std::string::npos)
        return *_selector;
    return ImdString(s.substr(slash + 1));
}

ImdString ImdModel::getAltName() const { return getName(); }
ImdDNA ImdModel::getDNA() const { return _attributeDNA ? *_attributeDNA : ImdDNA(); }

void ImdModel::update(bool) { _flags |= kUpdatedFlag; }
bool ImdModel::isValid() const { return (_flags & kValidFlag) != 0; }

void ImdModel::notifyClients(const ImdAttribute **, int) {}
void ImdModel::notifyClients(const ImdAttribute &) {}

void ImdModel::activate() { _flags |= kOpenFlag; }
int ImdModel::deactivate(int) { _flags &= (unsigned char)~kOpenFlag; return _deactivationDelay; }
void ImdModel::invalidate() { _flags &= (unsigned char)~kValidFlag; }
void ImdModel::validate() { _flags |= kValidFlag; }
bool ImdModel::validDNA() const { return (_flags & kValidDNAFlag) != 0; }
void ImdModel::invalidateDNA() { _flags &= (unsigned char)~kValidDNAFlag; }

void ImdModel::ref() { ++_refCount; }
void ImdModel::unref() { if (_refCount > 0) --_refCount; }
int ImdModel::getRef() const { return _refCount; }

const ImdAttribute *ImdModel::_lookupAttr(unsigned int) const { return 0; }
bool ImdModel::_setAttr(const ImdAttribute &) { return false; }
void ImdModel::_preventDeactivation() {}
void ImdModel::_allowDeactivation() {}
void ImdModel::doOutput() {}
void ImdModel::addToBatch() {}
void ImdModel::sendBatch() {}
void ImdModel::setBatchClient(const CallbackInfo *client) { _batchClient = const_cast<CallbackInfo *>(client); }
bool ImdModel::setAttr_Privileged(const ImdAttribute &) { return false; }
