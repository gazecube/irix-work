#include "ModelBasedIconView.h"

#include "Broker.h"
#include "Model.h"

namespace
{
static ImdString
portableGenericFtrResource(const ImdDNA &dna)
{
    /*
     * Binary/resource-backed host seam.
     * The original sgidefault.ftr maps generic directories to
     * ../iconlib/generic.folder.closed.fti and generic documents to
     * ../iconlib/generic.doc.fti. Full ImdFtrDB type matching will replace
     * this fallback once the native FTR database layer is reconstructed.
     */
    if (dna.type() == "Directory")
        return ImdString("iconlib/generic.folder.closed.fti");
    return ImdString("iconlib/generic.doc.fti");
}
}

ImdModelBasedIconView::ImdModelBasedIconView(
    ImdDNA &dna,
    bool childCapabilities)
    : ImdIconView(),
      _dna(dna),
      _childCapabilities(childCapabilities)
{
    /*
     * Original constructor label path for normal file/dir/ref modes is
     * broker->altNameFromDNA(dna). The portable broker does not expose that
     * helper yet, but its model getAltName() path is the same recovered model
     * surface and keeps the label model-backed.
     */
    ImdBroker *broker = ImdBroker::contextBroker();
    ImdModel *model = broker ? broker->getModel(_dna) : 0;
    if (model) {
        setLabel(model->getAltName());
        broker->releaseModel(model);
    }

    portableSetIconResource(portableGenericFtrResource(_dna));
}

ImdModelBasedIconView::~ImdModelBasedIconView()
{
}

ImdDNA
ImdModelBasedIconView::getDNA() const
{
    return _dna;
}

void
ImdModelBasedIconView::update(Widget widget)
{
    (void)widget;
    createContents(true);
}

void
ImdModelBasedIconView::specifyDNA()
{
    /* Original exported zero-argument signature recovered from the binary. */
}

void
ImdModelBasedIconView::createContents(bool force)
{
    (void)force;

    /*
     * The original createContents() constructs ImdFtrGalleryX from the type
     * DNA and calls setGallery(force) plus setGalleryFileName(path). Until the
     * host FTR classes are source-reconstructed, preserve that renderer-facing
     * boundary by selecting the exact generic SGI FTI resources from the
     * original filetype database.
     */
    portableSetIconResource(portableGenericFtrResource(getDNA()));
}

void
ImdModelBasedIconView::setChildCapabilities()
{
    /* Exact model capability flag propagation remains pending. */
}
