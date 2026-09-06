#ifndef IMD_MODEL_H
#define IMD_MODEL_H

#include "ImdDNA.h"
#include "ImdString.h"

#include <vector>

/*
 * Reconstructed from IRIX 6.5.0 libimdModel.so symbols, DWARF, vtables,
 * strings, and MIPS disassembly.
 *
 * Confidence convention:
 *   no note       - strongly supported original name/semantics
 *   best-guess    - behavior/layout confirmed, original spelling unknown
 *   unresolved    - declaration intentionally incomplete
 *
 * IMPORTANT:
 * This is currently a source-level reconstruction header, not an SGI N32 ABI
 * compatibility header. The original IRIX object layout is documented below,
 * but incomplete embedded utility types are temporarily represented by
 * pointers in the portable declaration. Do not infer IRIX offsets from the
 * C++ members at the end of this class.
 *
 * Recovered IRIX 6.5.0 layout:
 *   +0x00 ImdCollectionOf<CallbackInfo> * _clientCallbacks
 *   +0x04 ImdCollectionOf<CallbackInfo> * _clientChildCallbacks
 *   +0x08 packed flag byte
 *   +0x0c ImdCollectionOf<ImdAttribute>    _attributes
 *   +0x10 ImdString                        _selector
 *   +0x18 ImdDNA *                         _attributeDNA
 *   +0x1c int                              _refCount
 *   +0x20 CallbackInfo *                   _batchClient
 *   +0x24 int                              deactivation delay seconds
 *   +0x28 SGI C++ vptr for this interface
 */

class ImdAttribute;

template <class T> class ImdCollectionOf;

class ImdModel
{
public:
    typedef void (*Callback)(
        const ImdDNA &dna,
        const ImdAttribute **attributes,
        int attributeCount,
        void *clientData
    );

    struct CallbackInfo
    {
        Callback callback;
        void *clientData;
        unsigned char active;       /* binary-proven layout byte */
    };

    ImdModel(const ImdDNA &dna);
    virtual ~ImdModel();

    virtual ImdModel *getSelf();
    virtual ImdDNA getParentDNA() const;

    virtual bool addClientCallback(Callback callback,
                                   void *clientData,
                                   bool childCallback);
    virtual bool removeClientCallback(Callback callback,
                                      void *clientData,
                                      bool childCallback);

    virtual void *createChildIterator();
    virtual ImdModel *getNextChild(void *iterator);
    virtual void resetChildIterator(void *iterator);
    virtual void destroyChildIterator(void *iterator);

    virtual const ImdAttribute *getAttr(unsigned int attributeId) const;
    virtual bool setAttr(const ImdAttribute &attribute);
    virtual bool isAttrWritable(unsigned int attributeId) const;

    virtual ImdString getName() const;
    virtual ImdString getAltName() const;
    virtual ImdDNA getDNA() const;

    virtual void update(bool force);
    virtual bool isValid() const;

    virtual void notifyClients(const ImdAttribute **attributes,
                               int attributeCount);
    virtual void notifyClients(const ImdAttribute &attribute);

    virtual void activate();

    /*
     * Return value is a delay in seconds before another broker reap attempt.
     * attempt == 0 is the initial deactivation request.
     * attempt > 0 is a subsequent reap attempt.
     */
    virtual int deactivate(int attempt);

    virtual void invalidate();
    virtual void validate();
    virtual bool validDNA() const;
    virtual void invalidateDNA();

    virtual void ref();
    virtual void unref();
    virtual int getRef() const;

protected:
    const ImdAttribute *_lookupAttr(unsigned int attributeId) const;
    bool _setAttr(const ImdAttribute &attribute);

    void _preventDeactivation();
    void _allowDeactivation();

    /* unresolved original signatures */
    void doOutput();
    void addToBatch();
    void sendBatch();

    void setBatchClient(const CallbackInfo *client);
    bool setAttr_Privileged(const ImdAttribute &attribute);

protected:
    /*
     * Portable/reimplementation storage placeholders only.
     * These intentionally do NOT claim to reproduce the SGI object layout.
     */
    ImdCollectionOf<CallbackInfo> *_clientCallbacks;
    ImdCollectionOf<CallbackInfo> *_clientChildCallbacks;
    unsigned char _flags;
    ImdCollectionOf<ImdAttribute> *_attributes;
    ImdString *_selector;
    ImdDNA *_attributeDNA;              /* best-guess name */
    int _refCount;
    CallbackInfo *_batchClient;
    int _deactivationDelay;             /* best-guess name */

    /*
     * RE portable storage only. The original uses the collection pointers at
     * +0x00/+0x04. Keeping host vectors separate avoids pretending to match
     * SGI collection ABI while preserving the binary-proven callback behavior.
     */
    std::vector<CallbackInfo> _portableClientCallbacks;
    std::vector<CallbackInfo> _portableClientChildCallbacks;

public:
    enum ReconstructedFlags
    {
        kOpenFlag                    = 0x80,
        kUpdatedFlag                 = 0x40,
        kValidFlag                   = 0x20,
        kValidDNAFlag                = 0x10,
        kIteratingCallbacksFlag      = 0x08,
        kIteratingChildCallbacksFlag = 0x04
    };

    enum ReconstructedAttributeIds
    {
        kCanOpen = 0xEFFF0042, /* best-guess name; semantics confirmed */
        kOpen    = 0xEFFF0047  /* best-guess name; semantics confirmed */
    };
};

#endif
