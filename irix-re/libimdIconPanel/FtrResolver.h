#ifndef IRIX_RE_LIBIMDICONPANEL_FTRRESOLVER_H
#define IRIX_RE_LIBIMDICONPANEL_FTRRESOLVER_H

#include "ImdDNA.h"
#include "ImdString.h"

#include <vector>

/*
 * Portable execution seam for the binary-proven ImdTypedModel/FTR path.
 *
 * Original IRIX binaries prove:
 *
 *   ImdTypedModel::getAttr(0xEFFF0080)
 *     -> ImdTypedModel::typeFromName(ftrNameSpace, getName())
 *     -> imdFtrLoadDB(defaultDB)
 *     -> imdFtrIconTypeFromFileName(db, filename)
 *     -> imdFtrTypeName(type)
 *
 * with the original static strings:
 *
 *   ftrNameSpace = "imdFTR_Type"
 *   defaultDB    = "/usr/lib/filetype/desktop.otr"
 *
 * ImdModelBasedIconView::getType() then consumes that type DNA and
 * createContents() resolves it through imdFtrIconTypeFromTypeName().
 *
 * The preserved N32/MIPS FTR engine cannot execute directly on the host. The
 * portable path therefore evaluates MATCH rules from the shipped source tree
 * in desktop.otr source order (local, install, system, default), and reads the
 * matching TYPE's original ICON include list from those same files. The old
 * core classifier remains only as a compatibility fallback when the source
 * tree is unavailable or a rule cannot yet be evaluated. No host MIME or
 * extension table is introduced.
 *
 * mactype/maccreator currently retain the original failure value, "????",
 * until the binary-proven AppleSingle/AppleDouble/.HSAncillary precedence is
 * fully recovered.
 */

ImdDNA ImdPortableFtrTypeDNAForFile(const ImdDNA &fileDNA);

bool ImdPortableFtrIconResources(const ImdDNA &typeDNA,
                                 std::vector<ImdString> &resources);

#endif /* IRIX_RE_LIBIMDICONPANEL_FTRRESOLVER_H */
