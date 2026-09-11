#ifndef IRIX_RE_LIBIMDMODEL_FILEMODEL_H
#define IRIX_RE_LIBIMDMODEL_FILEMODEL_H

#include "ImdDNA.h"
#include "ImdString.h"

/*
 * Native source-level reconstruction of the original ImdFileModel helpers.
 *
 * The original N32 implementation exports:
 *   ImdFileModel::dnaSelectorFromPath(const ImdString &, const ImdString &)
 *   ImdFileModel::fileNameFromDNA(const ImdDNA &)
 *   ImdFileModel::dnaTypeFromPath(const ImdString &)
 * and owns the static ImdString kUnknownType.
 *
 * This host declaration preserves the recovered source-level API only. It is
 * not intended to match the original SGI object layout or ABI.
 */
class ImdFileModel
{
public:
    static ImdString dnaSelectorFromPath(const ImdString &path,
                                         const ImdString &host);
    static ImdString fileNameFromDNA(const ImdDNA &dna);
    static ImdString dnaTypeFromPath(const ImdString &path);
    static const char *unknownTypeName();
};

#endif /* IRIX_RE_LIBIMDMODEL_FILEMODEL_H */
