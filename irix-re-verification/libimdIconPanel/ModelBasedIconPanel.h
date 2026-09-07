#ifndef IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H

#include "ImdIconPanel.h"

#include <vector>

class ImdAttribute;
class ImdDNA;
class ImdModel;
class ImdModelBasedIconView;

/* ImdModelBasedIconPanel - an icon panel associated with a model.
 *
 * Constructor and original method names/signatures below are recovered from
 * the original IRIX libimdIconPanel.so symbols and local MIPS disassembly.
 */
class ImdModelBasedIconPanel : public ImdIconPanel
{
public:
    ImdModelBasedIconPanel(
        const char *name,
        Widget parent,
        ImdModel *model,
        Arg *args,
        int count);
    virtual ~ImdModelBasedIconPanel();

    void changeModel(ImdModel *model);
    void setModel(ImdModel *model);

    void showChildren(ImdIconView &view);

    void beginExists();
    void endExists();
    void addOrDelete();

    /* Original exported override @ 0x096a0a30. */
    virtual void dragEnd();

    ImdModel *portableModel() const { return _model; }
    unsigned int portableReceiverCount() const
    {
        return static_cast<unsigned int>(_receivers.size());
    }

private:
    class NotifyReceiver
    {
    public:
        NotifyReceiver(ImdModelBasedIconPanel *panel,
                       ImdModelBasedIconView *view,
                       ImdModel *model,
                       bool childCapabilities);
        ~NotifyReceiver();

        static void receiveNotification(const ImdDNA &dna,
                                        const ImdAttribute **attributes,
                                        int attributeCount,
                                        void *clientData);

    private:
        ImdModelBasedIconPanel *_panel;
        ImdModelBasedIconView *_view;
        ImdModel *_model;
        bool _childCapabilities;
    };

    void destroyReceivers();

    ImdModel *_model;
    std::vector<NotifyReceiver *> _receivers;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H */
