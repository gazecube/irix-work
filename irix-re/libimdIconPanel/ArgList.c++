#include "ArgList.h"

#include <vector>

class ImdArgListImpl
{
public:
    ImdArgListImpl()
        : argList(0)
    {
    }

    ~ImdArgListImpl()
    {
        clear();
    }

    void clear()
    {
        for (std::vector<Arg *>::iterator it = args.begin();
             it != args.end(); ++it)
            delete *it;

        args.clear();

        if (argList) {
            delete [] argList;
            argList = 0;
        }
    }

    ArgList getArgList()
    {
        if (argList) {
            delete [] argList;
            argList = 0;
        }

        if (args.empty())
            return 0;

        argList = new Arg[args.size()];

        for (std::vector<Arg *>::size_type i = 0; i < args.size(); ++i)
            argList[i] = *args[i];

        return argList;
    }

    Cardinal getNumArgs() const
    {
        return static_cast<Cardinal>(args.size());
    }

    std::vector<Arg *> args;
    ArgList argList;
};

ImdArgList::ImdArgList()
    : _impl(new ImdArgListImpl)
{
}

ImdArgList::~ImdArgList()
{
    delete _impl;
}

void
ImdArgList::clear()
{
    _impl->clear();
}

ArgList
ImdArgList::getArgList()
{
    return _impl->getArgList();
}

Cardinal
ImdArgList::getNumArgs() const
{
    return _impl->getNumArgs();
}

void
ImdArgList::getValues(Widget widget)
{
    if (!widget)
        return;

    /* RE: the original obtains the temporary Arg array first, then passes
     * that array and the current argument count directly to XtGetValues.
     */
    ArgList args = getArgList();
    Cardinal numArgs = getNumArgs();

    XtGetValues(widget, args, numArgs);
}

void
ImdArgList::setArg(const char *name, XtArgVal value)
{
    /* RE: the original stores the name and value verbatim in a separately
     * allocated Arg record.  It neither duplicates names nor replaces an
     * earlier record with the same name.
     */
    Arg *arg = new Arg;
    arg->name = const_cast<char *>(name);
    arg->value = value;
    _impl->args.push_back(arg);
}

void
ImdArgList::setValues(Widget widget)
{
    if (!widget)
        return;

    /* RE: libimdIconPanel.so calls getArgList(), getNumArgs(), and then
     * XtSetValues() in this order.
     */
    ArgList args = getArgList();
    Cardinal numArgs = getNumArgs();

    XtSetValues(widget, args, numArgs);
}
