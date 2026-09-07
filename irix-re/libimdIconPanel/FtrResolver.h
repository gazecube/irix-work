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
 * We cannot execute the preserved N32/MIPS FTR engine on the host, so this
 * source-level compatibility layer evaluates only the core, directly shipped
 * sgidefault.ftr predicates needed by ordinary files/directories, then reads
 * the matching TYPE's ICON include list from the installed original .ftr
 * sources. It is deliberately not a replacement definition of the FTR
 * language or ABI. Unimplemented MATCH forms fall back to Unknown instead of
 * inventing host MIME/extension mappings.
 */

ImdDNA ImdPortableFtrTypeDNAForFile(const ImdDNA &fileDNA);

bool ImdPortableFtrIconResources(const ImdDNA &typeDNA,
                                 std::vector<ImdString> &resources);

#endif /* IRIX_RE_LIBIMDICONPANEL_FTRRESOLVER_H */
