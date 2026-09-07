#ifndef IRIX_RE_LIBIMDICONPANEL_ARGLIST_H
#define IRIX_RE_LIBIMDICONPANEL_ARGLIST_H

#include <X11/Intrinsic.h>

class ImdArgListImpl;

/* ImdArgList - convenience wrapper for Xt argument lists.
 *
 * the class and method names are recovered from libimdIconPanel.so.
 * libimdApp.so further confirms setArg(char *, long) and
 * setValues(Widget) at the binary interface.
 */
class ImdArgList
{
public:
    ImdArgList();
    ~ImdArgList();

    void clear();
    ArgList getArgList();
    Cardinal getNumArgs() const;

    void getValues(Widget widget);
    void setArg(const char *name, XtArgVal value);
    void setValues(Widget widget);

private:
    ImdArgListImpl *_impl;
};

#endif
