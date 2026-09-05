#ifndef IRIX_RE_LIBIMDICONPANEL_ARGLIST_H
#define IRIX_RE_LIBIMDICONPANEL_ARGLIST_H

#include <X11/Intrinsic.h>

#include <vector>

/*
 * Clean-room reconstruction of ImdArgList.
 *
 * Binary/DWARF evidence from the original libimdIconPanel.so confirms the
 * original source unit userenv/desktop/lib/libimdIconPanel/ArgList.c++ and
 * the public method names ImdArgList::{ImdArgList,clear,getArgList,
 * getNumArgs,getValues,setArg,setValues,~ImdArgList}.  The exact C++
 * signatures are not present in the recovered public-name index, so the
 * signatures below are reconstruction choices and must not be treated as
 * ABI claims about the IRIX object.
 */
class ImdArgList
{
public:
    ImdArgList();
    ~ImdArgList();

    void clear();
    ArgList getArgList();
    Cardinal getNumArgs() const;

    /* Reconstructed signature; method name is binary-confirmed. */
    void setArg(const char *name, XtArgVal value);

private:
    std::vector<Arg> _args;
};

#endif
