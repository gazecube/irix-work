#include "ModelBasedIconPanel.h"

#include "Attribute.h"
#include "Broker.h"
#include "IconLayout.h"
#include "Model.h"
#include "ModelBasedIconView.h"

#include <X11/Xlib.h>

extern "C" void ImdViewPanelPortableBeginBlockChanges(Widget);
extern "C" void ImdViewPanelPortableEndBlockChanges(Widget);
extern "C" bool ImdViewPanelPortableChangesBlocked(Widget);

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
            receiver->_panel->beginExists();
            ImdViewPanelBeginBlockChanges(receiver->_panel->baseWidget());
            ImdViewPanelPortableBeginBlockChanges(receiver->_panel->baseWidget());
            break;

        case 0xEFFF004C:
        {
            ImdDNA childDNA;
            if (!imdGetAttrValue(*attribute, childDNA))
                break;

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
            ImdViewPanelEndBlockChanges(receiver->_panel->baseWidget());
            ImdViewPanelPortableEndBlockChanges(receiver->_panel->baseWidget());
            receiver->_panel->endExists();
            break;

        default:
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
    ImdViewPanelBeginBlockChanges(baseWidget());
    ImdViewPanelPortableBeginBlockChanges(baseWidget());

    destroyReceivers();
    portableResetViews(true);
    setModel(model);

    if (_model) {
        NotifyReceiver *receiver =
            new NotifyReceiver(this, 0, _model, false);
        _receivers.push_back(receiver);
    }

    ImdViewPanelEndBlockChanges(baseWidget());
    ImdViewPanelPortableEndBlockChanges(baseWidget());
}

void
ImdModelBasedIconPanel::setModel(ImdModel *model)
{
    _model = model;
}

void
ImdModelBasedIconPanel::showChildren(ImdIconView &view)
{
    (void)view;
}

void
ImdModelBasedIconPanel::beginExists()
{
}

void
ImdModelBasedIconPanel::endExists()
{
}

void
ImdModelBasedIconPanel::addOrDelete()
{
    Widget widget = baseWidget();
    if (!widget || !XtIsRealized(widget))
        return;

    /*
     * Existence notifications are explicitly bracketed by the original
     * ViewPanel block-changes API. While that batch is open, queue one redraw
     * instead of clearing the X window for every child. The portable ViewPanel
     * block seam performs the consolidated clear/redraw when the outermost
     * block closes.
     */
    if (ImdViewPanelPortableChangesBlocked(widget)) {
        ImdViewPanelRedraw(widget);
        return;
    }

    XClearArea(XtDisplay(widget), XtWindow(widget), 0, 0, 0, 0, False);
    ImdViewPanelRedraw(widget);
}

void
ImdModelBasedIconPanel::dragEnd()
{
    /*
     * Binary correction: original ImdModelBasedIconPanel::dragEnd()
     * @ 0x096a0a30 first calls ImdIconPanel::dragEnd(), then walks a
     * model collection owned by the drag operation and releases each model
     * through ImdBroker::releaseModel(). It does NOT itself write positions.
     *
     * Original desktop position persistence lives in the presentation-state
     * path instead: ImdBackWindow::savePresentationState() calls
     * ImdViewPanelSaveMappings(), and restorePresentationState() calls
     * ImdViewPanelLoadMappings().
     *
     * The reconstructed host does not yet have ImdInitFile/session callbacks,
     * so flush the already-captured ImdIconLayout mappings here as a temporary
     * lifecycle seam. This gives restart persistence without attributing the
     * write to the original dragEnd implementation.
     */
    ImdIconPanel::dragEnd();

    ImdLayout *layout = ImdViewPanelGetLayout(baseWidget());
    ImdIconLayout *iconLayout = dynamic_cast<ImdIconLayout *>(layout);
    if (iconLayout)
        iconLayout->portableSaveMappings();
}
