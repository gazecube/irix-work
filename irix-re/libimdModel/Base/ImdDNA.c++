#include "ImdDNA.h"

#include <cstddef>
#include <string>

ImdDNA::ImdDNA()
    : _field0(), _type(), _selector(), _attributes()
{
}

ImdDNA::ImdDNA(const ImdDNA &other)
    : _field0(other._field0),
      _type(other._type),
      _selector(other._selector),
      _attributes(other._attributes)
{
}

ImdDNA::ImdDNA(const ImdString &type, const ImdString &selector)
    : _field0(), _type(type), _selector(selector), _attributes()
{
}

ImdDNA::ImdDNA(const ImdString &serialized)
    : _field0(), _type(), _selector(), _attributes()
{
    parseValidSerialized(serialized.stdString());
}

ImdDNA::ImdDNA(const char *serialized)
    : _field0(), _type(), _selector(), _attributes()
{
    parseValidSerialized(serialized ? serialized : "");
}

ImdDNA::~ImdDNA()
{
}

ImdDNA &
ImdDNA::operator=(const ImdDNA &other)
{
    _field0 = other._field0;
    _type = other._type;
    _selector = other._selector;
    _attributes = other._attributes;
    return *this;
}

const ImdString &
ImdDNA::field0() const
{
    return _field0;
}

const ImdString &
ImdDNA::type() const
{
    return _type;
}

const ImdString &
ImdDNA::selector() const
{
    return _selector;
}

bool
ImdDNA::getAttr(const ImdString &key, ImdString &value) const
{
    AttributeMap::const_iterator it = _attributes.find(key.stdString());
    if (it == _attributes.end())
        return false;

    value = ImdString(it->second);
    return true;
}

void
ImdDNA::setAttr(const ImdString &key, const ImdString &value)
{
    _attributes[key.stdString()] = value.stdString();
}

void
ImdDNA::removeAttr(const ImdString &key)
{
    _attributes.erase(key.stdString());
}

void
ImdDNA::clearAttrs()
{
    _attributes.clear();
}

ImdString
ImdDNA::serialize() const
{
    std::string out;
    out += escape(_field0.stdString());
    out += ':';
    out += escape(_type.stdString());
    out += ':';
    out += escape(_selector.stdString());
    out += '!';

    for (AttributeMap::const_iterator it = _attributes.begin();
         it != _attributes.end(); ++it)
    {
        out += escape(it->first);
        out += '=';
        out += escape(it->second);
        out += ':';
    }

    return ImdString(out);
}

bool
ImdDNA::operator==(const ImdDNA &other) const
{
    return _field0 == other._field0 &&
           _type == other._type &&
           _selector == other._selector &&
           _attributes == other._attributes;
}

bool
ImdDNA::operator!=(const ImdDNA &other) const
{
    return !(*this == other);
}

std::string
ImdDNA::escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());

    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        switch (*it)
        {
        case '!': out += "%21"; break;
        case ':': out += "%3a"; break;
        case '=': out += "%3d"; break;
        case '%': out += "%25"; break;
        default:  out += *it; break;
        }
    }

    return out;
}

std::string
ImdDNA::unescape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size())
        {
            const std::string code = value.substr(i, 3);
            if (code == "%21") { out += '!'; i += 2; continue; }
            if (code == "%3a") { out += ':'; i += 2; continue; }
            if (code == "%3d") { out += '='; i += 2; continue; }
            if (code == "%25") { out += '%'; i += 2; continue; }
        }

        out += value[i];
    }

    return out;
}

void
ImdDNA::parseValidSerialized(const std::string &serialized)
{
    _field0 = "";
    _type = "";
    _selector = "";
    _attributes.clear();

    const std::size_t firstColon = serialized.find(':');
    if (firstColon == std::string::npos)
        return;

    const std::size_t secondColon = serialized.find(':', firstColon + 1);
    if (secondColon == std::string::npos)
        return;

    const std::size_t bang = serialized.find('!', secondColon + 1);
    if (bang == std::string::npos)
        return;

    _field0 = ImdString(unescape(serialized.substr(0, firstColon)));
    _type = ImdString(unescape(serialized.substr(firstColon + 1,
                                                  secondColon - firstColon - 1)));
    _selector = ImdString(unescape(serialized.substr(secondColon + 1,
                                                      bang - secondColon - 1)));

    std::size_t pos = bang + 1;
    while (pos < serialized.size())
    {
        const std::size_t equals = serialized.find('=', pos);
        if (equals == std::string::npos)
            break;

        const std::size_t colon = serialized.find(':', equals + 1);
        if (colon == std::string::npos)
            break;

        const std::string key = unescape(serialized.substr(pos, equals - pos));
        const std::string value = unescape(serialized.substr(equals + 1,
                                                              colon - equals - 1));
        _attributes[key] = value;
        pos = colon + 1;
    }
}
