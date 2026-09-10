#ifndef IRIX_RE_LIBIMDMODEL_IMDSTRING_H
#define IRIX_RE_LIBIMDMODEL_IMDSTRING_H

#include <string>

/*
 * Native source-level reconstruction of ImdString.
 *
 * This intentionally does not claim SGI N32 object-layout compatibility.
 * The original class is recovered from libimdUtil.so and is used pervasively
 * by libimdModel.  The host implementation preserves the value semantics
 * needed by the reconstructed model/DNA APIs.
 */
class ImdString
{
public:
    ImdString();
    ImdString(const char *value);
    ImdString(const std::string &value);
    ImdString(const ImdString &other);
    ~ImdString();

    ImdString &operator=(const ImdString &other);
    ImdString &operator=(const char *value);

    const char *c_str() const;
    const std::string &stdString() const;
    bool empty() const;
    unsigned int length() const;

    bool operator==(const ImdString &other) const;
    bool operator!=(const ImdString &other) const;
    bool operator<(const ImdString &other) const;

    bool operator==(const char *other) const;
    bool operator!=(const char *other) const;

private:
    std::string _value;
};

#endif /* IRIX_RE_LIBIMDMODEL_IMDSTRING_H */
