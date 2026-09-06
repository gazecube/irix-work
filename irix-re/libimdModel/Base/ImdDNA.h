#ifndef IRIX_RE_LIBIMDMODEL_IMDDNA_H
#define IRIX_RE_LIBIMDMODEL_IMDDNA_H

#include "ImdString.h"

#include <map>

/*
 * Native source-level reconstruction of ImdDNA.
 *
 * Original IRIX N32 layout recovered from libimdModel.so:
 *   +0x00 ImdString field0 (semantic name unresolved)
 *   +0x08 ImdString type
 *   +0x10 ImdString selector
 *   +0x18 attribute container/pointer
 *
 * This host class preserves the recovered value/serialization behavior, but
 * intentionally does not claim N32 object-layout compatibility.
 */
class ImdDNA
{
public:
    ImdDNA();
    ImdDNA(const ImdDNA &other);
    ImdDNA(const ImdString &type, const ImdString &selector);
    explicit ImdDNA(const ImdString &serialized);
    explicit ImdDNA(const char *serialized);
    ~ImdDNA();

    ImdDNA &operator=(const ImdDNA &other);

    const ImdString &field0() const;
    const ImdString &type() const;
    const ImdString &selector() const;

    bool getAttr(const ImdString &key, ImdString &value) const;
    void setAttr(const ImdString &key, const ImdString &value);
    void removeAttr(const ImdString &key);
    void clearAttrs();

    ImdString serialize() const;

    bool operator==(const ImdDNA &other) const;
    bool operator!=(const ImdDNA &other) const;

private:
    typedef std::map<std::string, std::string> AttributeMap;

    static std::string escape(const std::string &value);
    static std::string unescape(const std::string &value);
    void parseValidSerialized(const std::string &serialized);

    ImdString _field0;
    ImdString _type;
    ImdString _selector;
    AttributeMap _attributes;
};

#endif /* IRIX_RE_LIBIMDMODEL_IMDDNA_H */
