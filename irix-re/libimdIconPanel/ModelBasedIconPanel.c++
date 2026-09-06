#include "ModelBasedIconPanel.h"

#include "Attribute.h"
#include "Broker.h"
#include "Model.h"
#include "ModelBasedIconView.h"

ImdModelBasedIconPanel::NotifyReceiver::NotifyReceiver(
    ImdModelBasedIconPanel *panel,
    ImdModelBasedIconView *view,
    ImdModel *model,
    bool childCapabilities)
    : _panel(panel),
      _view(view),
      _model(0),
      _childCapabilities(childCapabilities)
{
    /*
     * Original constructor @ 0x096b111c stores model->getSelf() and then
     * registers receiveNotification twice, normal first and child second.
     * getSelf() owns one model ref and each callback registration owns one.
     */
    if (model)
        _model = model->getSelf();

    if (_model) {
        _model->addClientCallback(receiveNotification, this, false);
        _model->addClientCallback(receiveNotification, this, true);
    }
}

ImdModelBasedIconPanel::NotifyReceiver::~NotifyReceiver()
{
    if (!_model)
        return;

    /*
     * Exact destructor tail @ 0x0968cd74: child callback is removed first,
     * then normal callback, then the getSelf() reference is returned through
     * ImdBroker::releaseModel().
     */
    _model->removeClientCallback(receiveNotification, this, true);
    _model->removeClientCallback(receiveNotification, this, false);
    ImdBroker::contextBroker()->releaseModel(_model);
    _model = 0;
}

void
ImdModelBasedIconPanel::NotifyReceiver::receiveNotification(
    const ImdDNA &dna,
    const ImdAttribute **attributes,
    int attributeCount,
    void *clientData)
{
    (void)dna;

    NotifyReceiver *receiver = static_cast<NotifyReceiver *>(clientData);
    if (!receiver || !receiver->_panel || !attributes || attributeCount <= 0)
        return;

    bool changed = false;

    for (int i = 0; i < attributeCount; ++i) {
        const ImdAttribute *attribute = attributes[i];
        if (!attribute)
            continue;

        switch (attribute->getName()) {
        case 0xEFFF004B:
            /*
             * Exact receiveNotification existence-batch branch:
             * beginExists(), then ImdViewPanelBeginBlockChanges().
             */
            receiver->_panel->beginExists();
            ImdViewPanelBeginBlockChanges(receiver->_panel->baseWidget());
            break;

        case 0xEFFF004C:
        {
            ImdDNA childDNA;
            if (!imdGetAttrValue(*attribute, childDNA))
                break;

            /*
             * Original allocates 88 bytes and constructs
             * ImdModelBasedIconView(childDNA, childCapabilities), then calls
             * ImdIconPanel::adoptView(newView, receiver->_view).
             *
             * For the root desktop receiver the constructor flag is false and
             * the original relationship-derived branch also resolves false.
             * A true receiver flag is proven to force true. The remaining
             * nested false-case derivation is intentionally not guessed.
             */
            bool childCapabilities = receiver->_childCapabilities;
            if (!receiver->_view && !receiver->_childCapabilities)
                childCapabilities = false;

            ImdModelBasedIconView *view =
                new ImdModelBasedIconView(childDNA, childCapabilities);
            receiver->_panel->adoptView(view, receiver->_view);
            changed = true;
            break;
        }

        case 0xEFFF004D:
            /* Exact end ordering from the original callback. */
            ImdViewPanelEndBlockChanges(receiver->_panel->baseWidget());
            receiver->_panel->endExists();
            break;

        default:
            /*
             * Other recovered 0xEFFF.... notification branches cover
             * rename/delete/change/invalidation and remain under archaeology.
             * Do not synthesize their behavior into the initial-population
             * checkpoint.
             */
            break;
        }
    }

    if (changed)
        receiver->_panel->addOrDelete();
}

ImdModelBasedIconPanel::ImdModelBasedIconPanel(
    const char *name,
    Widget parent,
    ImdModel *model,
    Arg *args,
    int count)
    : ImdIconPanel(name, parent, args, count),
      _model(0)
{
    if (model)
        changeModel(model);
}

ImdModelBasedIconPanel::~ImdModelBasedIconPanel()
{
    destroyReceivers();
    _model = 0;
}

void
ImdModelBasedIconPanel::destroyReceivers()
{
    for (std::vector<NotifyReceiver *>::iterator it = _receivers.begin();
         it != _receivers.end(); ++it)
        delete *it;
    _receivers.clear();
}

void
ImdModelBasedIconPanel::changeModel(ImdModel *model)
{
    /*
     * Binary-backed lifecycle from original changeModel() @ 0x09685f6c:
     * block changes, tear down receiver/model relationships, transition views,
     * setModel(), install a root NotifyReceiver when a model exists, unblock.
     *
     * The exact existing-view transition virtual is not yet fully restored;
     * portableResetViews() remains explicitly host-only at that seam.
     */
    ImdViewPanelBeginBlockChanges(baseWidget());

    destroyReceivers();
    portableResetViews(true);
    setModel(model);

    if (_model) {
        NotifyReceiver *receiver =
            new NotifyReceiver(this, 0, _model, false);
        _receivers.push_back(receiver);
    }

    ImdViewPanelEndBlockChanges(baseWidget());
}

void
ImdModelBasedIconPanel::setModel(ImdModel *model)
{
    /*
     * Original setModel() stores the model at N32 object offset +0x64 and also
     * maintains an embedded ImdString beginning at +0x68. The latter remains
     * unresolved and is not invented in the portable object.
     */
    _model = model;
}

void
ImdModelBasedIconPanel::showChildren(ImdIconView &view)
{
    (void)view;

    /*
     * Original @ 0x096a0fa4 is completely receiver-based:
     * obtain view DNA, reject duplicate receiver DNA, broker->getModel(dna),
     * construct NotifyReceiver(this, &view, model, false), insert receiver,
     * release broker model. The root population checkpoint does not need this
     * path. Its base ImdIconView DNA virtual seam remains to be restored before
     * implementing it rather than using a cast-based substitute.
     */
}

void
ImdModelBasedIconPanel::beginExists()
{
    /* Original body dispatches the panel's begin-exists callback hook. */
}

void
ImdModelBasedIconPanel::endExists()
{
    /* Original body dispatches the panel's end-exists callback hook. */
}

void
ImdModelBasedIconPanel::addOrDelete()
{
    /* Original body dispatches the panel's add/delete callback hook. */
}
