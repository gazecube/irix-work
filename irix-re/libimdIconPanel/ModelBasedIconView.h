#ifndef IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONVIEW_H
#define IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONVIEW_H

#include "ImdIconPanel.h"
#include "ImdDNA.h"
#include "ImdString.h"

/*
 * ImdModelBasedIconView
 *
 * Public method names/signatures below are recovered from the original
 * IRIX libimdIconPanel.so symbols and local MIPS disassembly.
 *
 * Original N32 allocation size observed in
 * ImdModelBasedIconPanel::NotifyReceiver::receiveNotification(): 88 bytes.
 * This native class intentionally does not claim N32 layout compatibility.
 */
class ImdModelBasedIconView : public ImdIconView
{
public:
    ImdModelBasedIconView(ImdDNA &dna, bool childCapabilities);
    virtual ~ImdModelBasedIconView();

    ImdDNA getDNA() const;

    /* Original exported methods. */
    void update(Widget widget);
    void specifyDNA();
    void createContents(bool force);
    void setChildCapabilities();

    /*
     * RE portable inspection only. The stored DNA is the source-level value
     * used while the original broker/model-backed getDNA() branches are being
     * reconstructed.
     */
    const ImdDNA &portableDNA() const { return _dna; }
    bool portableChildCapabilities() const { return _childCapabilities; }

private:
    ImdDNA _dna;
    bool _childCapabilities;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONVIEW_H */
