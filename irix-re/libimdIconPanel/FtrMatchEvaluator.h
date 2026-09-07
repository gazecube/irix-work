#ifndef IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H
#define IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H

#include <string>

/*
 * Source-level evaluator for the MATCH subset used by desktop.otr.
 * Rules are consumed from the original local/install/system/default .ftr tree.
 */
bool ImdEvaluateFtrType(const std::string &filetypeRoot,
                        const std::string &path,
                        bool knownDirectory,
                        std::string &typeName);

#endif /* IRIX_RE_LIBIMDICONPANEL_FTRMATCHEVALUATOR_H */
