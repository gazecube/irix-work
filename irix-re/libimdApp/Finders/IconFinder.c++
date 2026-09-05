#include "IconFinder.h"

#include "ImdIconPanel.h"
#include "ArgList.h"
#include <Sgm/Finder.h>

#include <Xm/Frame.h>
#include <X11/StringDefs.h>

ImdIconFinder::ImdIconFinder(
    const char *name,
    Widget parent,
    int constructorValue)
    : _baseWidget(0),
      _textWidget(0),
      _iconFinderFrame(0),
      _iconFinderPanel(0),
      _changingContent(false),
      _constructorValue(constructorValue)
{
    /* make the finder base widget. the original constructor uses an
     * ImdArgList here. no resources are supplied until their values have
     * been recovered from the original libimdApp.so constructor.
     *
     * RE: ImdIconFinder::ImdIconFinder, 0x0963ec5c.
     */
    ImdArgList args;
    _baseWidget = XtCreateWidget(
        const_cast<char *>(name ? name : "iconFinder"),
        sgFinderWidgetClass,
        parent,
        args.getArgList(),
        args.getNumArgs());

    if (!_baseWidget)
        return;

    /* remember the text child. selector 1 and the SgFinder record offset
     * are taken from the original binary.
     */
    _textWidget = SgFinderGetChild(_baseWidget, SgFINDER_TEXT);

    /* make the icon finder panel. the original object is an
     * ImdModelBasedIconPanel. use ImdIconPanel until that class is
     * reconstructed; do not infer the missing model behaviour here.
     */
    _iconFinderFrame = XtVaCreateManagedWidget(
        "iconFinderFrame",
        xmFrameWidgetClass,
        _baseWidget,
        NULL);

    _iconFinderPanel = new ImdIconPanel(
        "iconFinderPanel",
        _iconFinderFrame,
        0,
        0);

    if (_iconFinderPanel && _iconFinderPanel->baseWidget())
        XtManageChild(_iconFinderPanel->baseWidget());
}

ImdIconFinder::~ImdIconFinder()
{
    delete _iconFinderPanel;
    _iconFinderPanel = 0;
    _iconFinderFrame = 0;
    _textWidget = 0;

    if (_baseWidget && XtIsWidget(_baseWidget))
        XtDestroyWidget(_baseWidget);
    _baseWidget = 0;
}

void ImdIconFinder::setContent(const char *content, bool notify)
{
    (void)notify;

    if (!_baseWidget)
        return;

    /* suppress the normal content-change path while setting the field.
     * ImdCompletionField notification is restored with that class.
     */
    _changingContent = true;
    SgFinderSetTextString(
        _baseWidget,
        const_cast<char *>(content ? content : ""));
    _changingContent = false;
}

std::string ImdIconFinder::getContent() const
{
    char *content;
    std::string result;

    if (!_baseWidget)
        return result;

    content = SgFinderGetTextString(_baseWidget);
    if (content)
    {
        result = content;
        XtFree(content);
    }

    return result;
}

Widget ImdIconFinder::getWidget() const
{
    return _baseWidget;
}

Widget ImdIconFinder::portableTextWidget() const
{
    return _textWidget;
}

Widget ImdIconFinder::portableIconFinderFrame() const
{
    return _iconFinderFrame;
}

ImdIconPanel *ImdIconFinder::portableIconFinderPanel() const
{
    return _iconFinderPanel;
}
