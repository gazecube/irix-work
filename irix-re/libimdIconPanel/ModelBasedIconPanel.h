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

    /*
     * Original exported signature:
     *   showChildren__22ImdModelBasedIconPanelGR11ImdIconView
     */
    void showChildren(ImdIconView &view);

    /* Original exported event-hook methods. */
    void beginExists();
    void endExists();
    void addOrDelete();

    /* RE portable inspection only. */
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
        /*
         * Portable source-level storage corresponding to the proven original
         * fields at N32 +0x08/+0x0c/+0x10/+0x14. The original also has cache
         * state at +0x00 and a flag byte at +0x04; those are not yet needed by
         * the initial existence-batch path and are not fabricated here.
         */
        ImdModelBasedIconPanel *_panel;
        ImdModelBasedIconView *_view;
        ImdModel *_model;
        bool _childCapabilities;
    };

    void destroyReceivers();

    ImdModel *_model;

    /*
     * Portable container only. The original panel owns an
     * ImdCollectionOf<NotifyReceiver>; this vector does not claim ABI parity.
     */
    std::vector<NotifyReceiver *> _receivers;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H */
