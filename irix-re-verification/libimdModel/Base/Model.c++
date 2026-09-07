#include "Model.h"

#include "Attribute.h"

#include <string>
#include <vector>

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

ImdModel *
ImdModel::getSelf()
{
    /* Original getSelf() calls the virtual ref() slot before returning this. */
    ref();
    return this;
}

ImdDNA ImdModel::getParentDNA() const { return ImdDNA(); }

bool
ImdModel::addClientCallback(Callback callback,
                            void *clientData,
                            bool childCallback)
{
    if (!callback)
        return false;

    /*
     * Binary-backed behavior from libimdModel.so @ 0x09753470:
     *   ref();
     *   allocate 12-byte CallbackInfo(callback, clientData, active=1);
     *   insert into the normal or child callback collection.
     *
     * Host vectors are deliberately used instead of pretending to implement
     * the original ImdCollectionOf ABI.
     */
    ref();

    CallbackInfo info;
    info.callback = callback;
    info.clientData = clientData;
    info.active = 1;

    if (childCallback)
        _portableClientChildCallbacks.push_back(info);
    else
        _portableClientCallbacks.push_back(info);

    return true;
}

bool
ImdModel::removeClientCallback(Callback callback,
                               void *clientData,
                               bool childCallback)
{
    std::vector<CallbackInfo> &callbacks = childCallback
        ? _portableClientChildCallbacks
        : _portableClientCallbacks;

    for (std::vector<CallbackInfo>::iterator it = callbacks.begin();
         it != callbacks.end(); ++it) {
        if (it->callback != callback || it->clientData != clientData)
            continue;

        /*
         * Original removeClientCallback() marks the record inactive when the
         * corresponding collection is being iterated; otherwise it removes
         * and deletes it immediately. Dispatch below iterates a snapshot, so
         * immediate vector erasure has the same externally visible effect.
         */
        callbacks.erase(it);
        unref();
        return true;
    }

    return false;
}

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

void
ImdModel::notifyClients(const ImdAttribute **attributes, int attributeCount)
{
    /*
     * Binary-backed callback tuple and normal-client dispatch surface.
     * The original implementation has batching/iteration flags and richer
     * routing. Snapshot iteration preserves safe self-removal without claiming
     * those still-unrecovered collection internals.
     */
    const std::vector<CallbackInfo> callbacks(_portableClientCallbacks);
    const ImdDNA dna = getDNA();

    for (std::vector<CallbackInfo>::const_iterator it = callbacks.begin();
         it != callbacks.end(); ++it) {
        if (it->active && it->callback)
            it->callback(dna, attributes, attributeCount, it->clientData);
    }
}

void
ImdModel::notifyClients(const ImdAttribute &attribute)
{
    const ImdAttribute *attributes[1];
    attributes[0] = &attribute;
    notifyClients(attributes, 1);
}

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
