#ifndef IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H

#include "ImdIconPanel.h"

#include <vector>

class ImdModel;

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
     *
     * The argument is an existing icon view. The original implementation uses
     * that view's DNA to install a NotifyReceiver for the corresponding model;
     * it is not a zero-argument child-model enumerator.
     */
    void showChildren(ImdIconView &view);

    /* RE: inspection/helpers only; not original public interface. */
    ImdModel *portableModel() const { return _model; }
    unsigned int portableChildCount() const { return _portableChildCount; }
    void portableEnumerateModelChildren();

private:
    class NotifyReceiver;

    void destroyReceivers();
    void installRootReceiver();

    ImdModel *_model;
    unsigned int _portableChildCount;
    std::vector<NotifyReceiver *> _receivers;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H */
