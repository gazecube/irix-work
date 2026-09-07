#ifndef IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H
#define IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H

#include <string>

/*
 * Source-level execution of MATCH rules used to build desktop.otr.
 * Rules are consumed from the original local/install/system/default .ftr tree
 * in shipped database order; unresolved primitives fail back through the
 * compatibility resolver rather than introducing host MIME semantics.
 */
bool ImdEvaluateFtrType(const std::string &filetypeRoot,
                        const std::string &path,
                        bool knownDirectory,
                        std::string &typeName);

#endif /* IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H */
