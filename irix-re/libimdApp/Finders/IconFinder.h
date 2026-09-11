#ifndef IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H
#define IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H

#include <X11/Intrinsic.h>
#include <string>

class ImdIconPanel;
class VkMenuBar;
class VkMenuItem;

/*
 * Portable reconstruction of the original ImdIconFinder application
 * component. The class name, compilation-unit name, widget ownership, and
 * public method names below are recovered from the original IRIX libimdApp.so.
 *
 * The original constructor accepts const ImdString& and derives from
 * VkComponent. This host reconstruction still accepts const char * and owns
 * the equivalent Xt base widget directly, but the original internal ViewKit
 * menu-bar path is now restored where the surviving binary proves it.
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

    /*
     * Original constructor storage corresponding to the recovered ViewKit
     * menu bar and the item returned by findNamedItem("finderSelectionMenu").
     * Host members do not claim the original N32 object offsets.
     */
    VkMenuBar *_menuBar;
    VkMenuItem *_finderSelectionMenu;

    bool _changingContent;
    int _constructorValue;
};

#endif /* IRIX_RE_LIBIMDAPP_FINDERS_ICONFINDER_H */
