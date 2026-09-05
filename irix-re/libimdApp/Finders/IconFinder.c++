#include "IconFinder.h"

#include "ImdIconPanel.h"
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
    /*
     * Binary-confirmed at ImdIconFinder::ImdIconFinder, 0x0963ec5c:
     * XtCreateWidget(name, sgFinderWidgetClass, parent, args, count) supplies
     * the VkComponent base widget. The original args come from ImdArgList;
     * until that type is reconstructed this stage passes an empty list. We
     * retain the exact creation route instead of hiding it behind
     * SgCreateFinder.
     */
    _baseWidget = XtCreateWidget(
        const_cast<char *>(name ? name : "iconFinder"),
        sgFinderWidgetClass,
        parent,
        0,
        0);

    if (!_baseWidget)
        return;

    /*
     * libimdApp imports SgFinderGetChild and later keeps the text child used
     * by setContent/getContent. Selector 1 is binary-confirmed as the child at
     * SgFinder record offset +0xf4.
     */
    _textWidget = SgFinderGetChild(_baseWidget, SgFINDER_TEXT);

    /*
     * The original constructor creates these exact widget-instance names in
     * this order. ImdModelBasedIconPanel is not reconstructed yet; the current
     * ImdIconPanel preserves the panel/view-widget boundary without claiming
     * the missing model behavior.
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

    /*
     * The original routine sets its changing-content guard before updating
     * the text field. Notification through ImdCompletionField is deferred
     * until that class is reconstructed.
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
