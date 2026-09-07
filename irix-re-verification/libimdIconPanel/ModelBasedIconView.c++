#include "ModelBasedIconView.h"

#include "Broker.h"
#include "FtrResolver.h"
#include "Model.h"

#include <vector>

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

    createContents(false);
}

ImdModelBasedIconView::~ImdModelBasedIconView()
{
}

ImdDNA
ImdModelBasedIconView::getDNA() const
{
    return _dna;
}

ImdDNA
ImdModelBasedIconView::getType()
{
    /*
     * Binary-proven original path:
     *
     *   getDNA()
     *    -> broker->getModel()
     *    -> dynamic_cast<ImdTypedModel *>()
     *    -> model attribute 0xEFFF0080 (ImdAttrTypeDNA)
     *
     * ImdTypedModel::getAttr(0xEFFF0080) lazily computes that attribute via
     * typeFromName("imdFTR_Type", getName()), and typeFromName() calls
     * imdFtrIconTypeFromFileName() against
     * "/usr/lib/filetype/desktop.otr".
     *
     * ImdTypedModel and the MIPS FTR DB evaluator are not executable host-side
     * yet. ImdPortableFtrTypeDNAForFile() is the explicitly isolated source
     * compatibility seam that evaluates only rules recovered from the shipped
     * SGI .ftr tree. It returns the same type-DNA orientation as the original:
     *
     *   ImdDNA(typeName, "/usr/lib/filetype/desktop.otr")
     */
    return ImdPortableFtrTypeDNAForFile(getDNA());
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
    /*
     * Original createContents(bool), @ 0x0967f478, performs:
     *
     *   specifyDNA();
     *   ImdDNA dna = getDNA();
     *   ImdModel *model = ImdBroker::contextBroker()->getModel(dna);
     *   ImdDNA typeDNA = getType();
     *   imdFtrLoadDB(typeDNA.selector());
     *   imdFtrIconTypeFromTypeName(db, typeDNA.type());
     *   new ImdFtrGalleryX(type);
     *   gallery->setGallery(force);
     *   gallery->setGalleryFileName(ImdFileModel::fileNameFromDNA(dna));
     *
     * It can also construct an optional ImdFtrIconX from the binary-proven
     * ImdTypedModel secondary DB/type DNA attributes.
     *
     * The host does not yet have ABI-compatible ImdFtrGalleryX/ImdFtrIconX,
     * so preserve the recovered boundary by asking the FTR resolver for the
     * ordered FTI include stack belonging to the resolved type. The renderer
     * composites that stack in order, which is required for SGI base+overlay
     * icons such as Script and AsciiTextFile.
     */
    (void)force;
    specifyDNA();

    const ImdDNA typeDNA = getType();
    std::vector<ImdString> resources;
    if (ImdPortableFtrIconResources(typeDNA, resources))
        portableSetIconResources(resources);
    else
        portableSetIconResources(std::vector<ImdString>());
}

void
ImdModelBasedIconView::setChildCapabilities()
{
    /* Exact model capability flag propagation remains pending. */
}
