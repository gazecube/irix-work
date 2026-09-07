#ifndef IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H
#define IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H

#include <X11/Intrinsic.h>
#include <string>

class ImdIconPanel;

/*
 * Portable first reconstruction of the original ImdIconFinder application
 * component. The class name, compilation-unit name, widget ownership, and
 * public method names below are recovered from the original IRIX libimdApp.so.
 *
 * The original constructor accepts const ImdString&, derives from VkComponent,
 * and constructs ImdModelBasedIconPanel. Those dependencies are not complete
 * in the host tree yet, so this stage accepts const char *, owns the equivalent
 * Xt base widget directly, and uses the reconstructed ImdIconPanel as the
 * panel-side stand-in. This is source-level reconstruction, not an SGI C++ ABI
 * compatibility declaration.
 */
class ImdIconFinder
{
public:
    ImdIconFinder(
        const char *name,
        Widget parent,
        int constructorValue = 0);
    virtual ~ImdIconFinder();

    void setContent(const char *content, bool notify = true);
    std::string getContent() const;

    /* Original accessor name and ownership result. */
    Widget getWidget() const;

    /* Portable inspection accessors; not claimed as original public API. */
    Widget portableTextWidget() const;
    Widget portableIconFinderFrame() const;
    ImdIconPanel *portableIconFinderPanel() const;

private:
    ImdIconFinder(const ImdIconFinder &);
    ImdIconFinder &operator=(const ImdIconFinder &);

    Widget _baseWidget;
    Widget _textWidget;
    Widget _iconFinderFrame;
    ImdIconPanel *_iconFinderPanel;
    bool _changingContent;
    int _constructorValue;
};

#endif /* IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H */
