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
     * Binary-backed lifecycle correction:
     *
     * Original changeModel() brackets the operation with
     * ImdViewPanelBeginBlockChanges()/EndBlockChanges(), tears down the old
     * receiver/model relationships, iterates existing views, calls setModel(),
     * and installs a root NotifyReceiver when a model exists.
     *
     * NotifyReceiver and the broker callback path are not yet reconstructed in
     * the portable build. Keep the block boundary and model handoff, but keep
     * the temporary view reset explicitly marked as a portable seam rather
     * than attributing it to the original implementation.
     */
    ImdViewPanelBeginBlockChanges(baseWidget());

    /* RE portable seam: original code manages views through receiver state. */
    portableResetViews(true);

    setModel(model);
    _portableChildCount = 0;

    /*
     * Preserve the existing diagnostic probe under an explicitly reconstructed
     * name. This is not the SGI showChildren() method.
     */
    if (_model)
        portableEnumerateModelChildren();

    ImdViewPanelEndBlockChanges(baseWidget());
}

void
ImdModelBasedIconPanel::setModel(ImdModel *model)
{
    /*
     * The original setModel() stores the model (original object offset +0x64)
     * and maintains an ImdString selector/type-cache field at +0x68. The
     * portable class does not yet carry that embedded ImdString, so only the
     * proven model association is restored here.
     */
    _model = model;
}

void
ImdModelBasedIconPanel::showChildren(ImdIconView &view)
{
    (void)view;

    /*
     * Original libimdIconPanel.so @ 0x096a0fa4, size 576:
     *
     *   1. obtain the supplied view's ImdDNA through its virtual interface;
     *   2. scan the panel's NotifyReceiver collection for matching DNA;
     *   3. if absent, ImdBroker::contextBroker()->getModel(dna);
     *   4. allocate a 24-byte NotifyReceiver;
     *   5. NotifyReceiver(panel, &view, model, false);
     *   6. insert it into the receiver collection;
     *   7. release the broker model reference.
     *
     * Do not replace this with direct child enumeration. The exact receiver
     * collection/callback implementation is the next reconstruction seam.
     */
}

void
ImdModelBasedIconPanel::portableEnumerateModelChildren()
{
    if (!_model)
        return;

    _portableChildCount = 0;

    void *iterator = _model->createChildIterator();
    if (!iterator)
        return;

    for (ImdModel *child = _model->getNextChild(iterator);
         child;
         child = _model->getNextChild(iterator))
        ++_portableChildCount;

    _model->destroyChildIterator(iterator);

    std::fprintf(stderr,
                 "ImdModelBasedIconPanel: portable probe enumerated %u model children\n",
                 _portableChildCount);
}
