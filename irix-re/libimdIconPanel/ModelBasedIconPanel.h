#ifndef IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H
#define IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H

#include "ImdIconPanel.h"

class ImdModel;

/* ImdModelBasedIconPanel - an icon panel associated with a model.
 *
 * the constructor argument list and setModel method name are recovered
 * from the original libimdIconPanel.so and libimdApp.so objects.
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

    void setModel(ImdModel *model);

    /* RE: inspection helper; not original public interface
     */
    ImdModel *portableModel() const { return _model; }

private:
    ImdModel *_model;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_MODELBASEDICONPANEL_H */
