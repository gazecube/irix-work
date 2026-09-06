#ifndef IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H

#include "ImdIconPanel.h"

class ImdModel;

/* ImdModelBasedIconPanel - an icon panel associated with a model.
 *
 * the constructor argument list and method names below are recovered from
 * the original libimdIconPanel.so/libimdApp.so symbols.
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
    void showChildren();

    /* RE: inspection helpers; not original public interface. */
    ImdModel *portableModel() const { return _model; }
    unsigned int portableChildCount() const { return _portableChildCount; }

private:
    ImdModel *_model;
    unsigned int _portableChildCount;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H */
