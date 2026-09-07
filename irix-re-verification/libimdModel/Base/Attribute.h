#ifndef IRIX_RE_LIBIMDMODEL_ATTRIBUTE_H
#define IRIX_RE_LIBIMDMODEL_ATTRIBUTE_H

#include "ImdDNA.h"

/*
 * Source-level reconstruction of the attribute classes used by the desktop
 * model notification path.
 *
 * Binary evidence:
 *   libimdUtil.so
 *     ImdAttribute::ImdAttribute(unsigned int)  @ 0x09839a90
 *     ImdAttribute::getName() const             @ 0x09839c88
 *     ImdAttrBool                               vtable @ 0x09851198
 *     imdGetAttrValue(ImdAttribute const&,bool&)@ 0x098399a8
 *
 *   libimdModel.so
 *     ImdAttrDNA::ImdAttrDNA(unsigned int,
 *                            ImdDNA const&)     @ 0x09741310
 *     imdGetAttrValue(ImdAttribute const&,
 *                     ImdDNA&)                 @ 0x097410fc
 *
 * Original N32 ImdAttribute layout begins with:
 *   +0x00 unsigned int attribute id/name
 *   +0x04 vptr
 *
 * These portable classes preserve the recovered source-level value semantics;
 * they intentionally do not claim N32 object-layout compatibility.
 */
class ImdAttribute
{
public:
    explicit ImdAttribute(unsigned int name) : _name(name) {}
    virtual ~ImdAttribute() {}

    virtual unsigned int getName() const { return _name; }

private:
    unsigned int _name;
};

class ImdAttrBool : public ImdAttribute
{
public:
    ImdAttrBool(unsigned int name, bool value)
        : ImdAttribute(name), _value(value) {}

    bool value() const { return _value; }

private:
    bool _value;
};

class ImdAttrDNA : public ImdAttribute
{
public:
    ImdAttrDNA(unsigned int name, const ImdDNA &value)
        : ImdAttribute(name), _value(value) {}

    const ImdDNA &value() const { return _value; }

private:
    ImdDNA _value;
};

inline bool
imdGetAttrValue(const ImdAttribute &attribute, bool &value)
{
    const ImdAttrBool *typed = dynamic_cast<const ImdAttrBool *>(&attribute);
    if (!typed)
        return false;
    value = typed->value();
    return true;
}

inline bool
imdGetAttrValue(const ImdAttribute &attribute, ImdDNA &value)
{
    const ImdAttrDNA *typed = dynamic_cast<const ImdAttrDNA *>(&attribute);
    if (!typed)
        return false;
    value = typed->value();
    return true;
}

#endif /* IRIX_RE_LIBIMDMODEL_ATTRIBUTE_H */
