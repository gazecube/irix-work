#ifndef IMD_MODEL_H
#define IMD_MODEL_H

/*
 * Reconstructed from IRIX 6.5.0 libimdModel.so symbols, DWARF, vtables,
 * strings, and MIPS disassembly.
 *
 * Confidence convention:
 *   no note       - strongly supported original name/semantics
 *   best-guess    - behavior/layout confirmed, original spelling unknown
 *   unresolved    - declaration intentionally incomplete
 */

class ImdDNA;
class ImdString;
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
        Callback callback;          /* +0x00 */
        void *clientData;           /* +0x04 */
        unsigned char active;       /* +0x08, best-guess member name */
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

    /* Exact overload parameter types are still being recovered. */
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
    ImdCollectionOf<CallbackInfo> *_clientCallbacks;       /* +0x00 */
    ImdCollectionOf<CallbackInfo> *_clientChildCallbacks;  /* +0x04 */

    /*
     * Packed flags byte at +0x08.
     * Bit behavior is confirmed. Original member spellings are not.
     */
    unsigned char _flags;

    ImdCollectionOf<ImdAttribute> *_attributes; /* +0x0c container identity confirmed */
    ImdString *_selector;                       /* +0x10 semantic identity confirmed */
    ImdDNA *_attributeDNA;                      /* +0x18, best-guess name */
    int _refCount;                              /* +0x1c */
    CallbackInfo *_batchClient;                 /* +0x20 */
    int _deactivationDelay;                     /* +0x24, best-guess name */

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
