#include "ArgList.h"

#include <cstring>

ImdArgList::ImdArgList()
{
}

ImdArgList::~ImdArgList()
{
    clear();
}

void
ImdArgList::clear()
{
    for (std::vector<Arg>::iterator it = _args.begin(); it != _args.end(); ++it) {
        if (it->name)
            XtFree(it->name);
        it->name = 0;
    }

    _args.clear();
}

ArgList
ImdArgList::getArgList()
{
    return _args.empty() ? 0 : &_args[0];
}

Cardinal
ImdArgList::getNumArgs() const
{
    return static_cast<Cardinal>(_args.size());
}

void
ImdArgList::setArg(const char *name, XtArgVal value)
{
    if (!name)
        return;

    /* replace an argument already present in the list
     */
    for (std::vector<Arg>::iterator it = _args.begin(); it != _args.end(); ++it) {
        if (it->name && std::strcmp(it->name, name) == 0) {
            it->value = value;
            return;
        }
    }

    Arg arg;
    arg.name = XtNewString(name);
    arg.value = value;
    _args.push_back(arg);
}

void
ImdArgList::setValues(Widget widget)
{
    if (!widget || _args.empty())
        return;

    /* apply the accumulated arguments to an existing widget
     */
    XtSetValues(widget, &_args[0], static_cast<Cardinal>(_args.size()));
}
