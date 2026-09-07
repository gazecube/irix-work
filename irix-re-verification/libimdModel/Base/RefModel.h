#ifndef IRIX_RE_LIBIMDMODEL_REFMODEL_H
#define IRIX_RE_LIBIMDMODEL_REFMODEL_H

#include "ImdDNA.h"

/*
 * Source-level reconstruction of the original ImdRefModel static DNA helpers.
 *
 * Only the recovered static API is exposed here. This is not an N32 ABI
 * declaration for the full original ImdRefModel class.
 */
class ImdRefModel
{
public:
    static ImdDNA desktopContainerDNA();
};

#endif /* IRIX_RE_LIBIMDMODEL_REFMODEL_H */
