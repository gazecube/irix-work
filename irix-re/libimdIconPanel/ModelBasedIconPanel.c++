#include "ModelBasedIconPanel.h"

ImdModelBasedIconPanel::ImdModelBasedIconPanel(
    const char *name,
    Widget parent,
    ImdModel *model,
    Arg *args,
    int count)
    : ImdIconPanel(name, parent, args, count),
      _model(model)
{
    /* the original class builds model views here. defer that work until
     * the callback and child-enumeration paths have been recovered.
     */
}

ImdModelBasedIconPanel::~ImdModelBasedIconPanel()
{
    _model = 0;
}

void
ImdModelBasedIconPanel::setModel(ImdModel *model)
{
    /* remember the model. model callbacks are installed by the original
     * implementation and will be restored with changeModel()/showChildren().
     */
    _model = model;
}
