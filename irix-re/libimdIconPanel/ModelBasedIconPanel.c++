#include "ModelBasedIconPanel.h"
#include "Model.h"

#include <cstdio>

ImdModelBasedIconPanel::ImdModelBasedIconPanel(
    const char *name,
    Widget parent,
    ImdModel *model,
    Arg *args,
    int count)
    : ImdIconPanel(name, parent, args, count),
      _model(0),
      _portableChildCount(0)
{
    if (model)
        changeModel(model);
}

ImdModelBasedIconPanel::~ImdModelBasedIconPanel()
{
    _model = 0;
    _portableChildCount = 0;
}

void
ImdModelBasedIconPanel::changeModel(ImdModel *model)
{
    /*
     * RE: changeModel(), setModel(), and showChildren() are original exported
     * methods. The original panel rebuilds its child views while panel changes
     * are blocked. Keep that ownership/order here while the exact callback
     * registration body is recovered.
     */
    ImdViewPanelBeginBlockChanges(baseWidget());
    portableResetViews(true);
    _model = model;
    _portableChildCount = 0;

    if (_model)
        showChildren();

    ImdViewPanelEndBlockChanges(baseWidget());
}

void
ImdModelBasedIconPanel::setModel(ImdModel *model)
{
    changeModel(model);
}

void
ImdModelBasedIconPanel::showChildren()
{
    if (!_model)
        return;

    /*
     * RE: ImdModel's child iterator virtuals and the showChildren() method are
     * binary-recovered. Enumerate through those original virtual slots now.
     * ImdModelBasedIconView construction is the next restored seam; do not
     * synthesize a different view type here.
     */
    void *iterator = _model->createChildIterator();
    if (!iterator)
        return;

    for (ImdModel *child = _model->getNextChild(iterator);
         child;
         child = _model->getNextChild(iterator))
        ++_portableChildCount;

    _model->destroyChildIterator(iterator);

    std::fprintf(stderr,
                 "ImdModelBasedIconPanel: enumerated %u model children\n",
                 _portableChildCount);
}
