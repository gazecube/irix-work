#include "ImdString.h"

ImdString::ImdString()
    : _value()
{
}

ImdString::ImdString(const char *value)
    : _value(value ? value : "")
{
}

ImdString::ImdString(const std::string &value)
    : _value(value)
{
}

ImdString::ImdString(const ImdString &other)
    : _value(other._value)
{
}

ImdString::~ImdString()
{
}

ImdString &
ImdString::operator=(const ImdString &other)
{
    _value = other._value;
    return *this;
}

ImdString &
ImdString::operator=(const char *value)
{
    _value = value ? value : "";
    return *this;
}

const char *
ImdString::c_str() const
{
    return _value.c_str();
}

const std::string &
ImdString::stdString() const
{
    return _value;
}

bool
ImdString::empty() const
{
    return _value.empty();
}

unsigned int
ImdString::length() const
{
    return static_cast<unsigned int>(_value.size());
}

bool
ImdString::operator==(const ImdString &other) const
{
    return _value == other._value;
}

bool
ImdString::operator!=(const ImdString &other) const
{
    return _value != other._value;
}

bool
ImdString::operator<(const ImdString &other) const
{
    return _value < other._value;
}

bool
ImdString::operator==(const char *other) const
{
    return _value == (other ? other : "");
}

bool
ImdString::operator!=(const char *other) const
{
    return !(*this == other);
}
