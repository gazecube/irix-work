#include "IconFinder.h"

#include "ArgList.h"
#include "ModelBasedIconPanel.h"
#include <Sgm/Finder.h>

#include <Xm/Form.h>
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
    /* make the finder base widget
     *
     * RE: the original constructor clears a local ImdArgList and creates
     * the SgFinder with no arguments at 0x0963edc8..0x0963edf4.
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

    /* make the frame
     */
    _iconFinderFrame = XtVaCreateManagedWidget(
        "iconFinderFrame",
        xmFrameWidgetClass,
        _baseWidget,
        NULL);

    /* attach the frame to three sides of the finder
     *
     * RE: libimdApp was built with FAST_QUARKS. the encoded resource
     * pointers -66, -102, -70 and -103 decode through SGI FastStrDefs.h
     * to the four attachment resources below. the values are exactly
     * 1, 1, 1 and 0 in the original constructor.
     */
    args.setArg(XmNtopAttachment, XmATTACH_FORM);
    args.setArg(XmNbottomAttachment, XmATTACH_FORM);
    args.setArg(XmNleftAttachment, XmATTACH_FORM);
    args.setArg(XmNrightAttachment, XmATTACH_NONE);
    args.setValues(_iconFinderFrame);

    /* make the model panel. this argument order is taken directly from the
     * imported constructor symbol in libimdApp.so:
     *
     * ImdModelBasedIconPanel(const char *, Widget, ImdModel *, Arg *, int)
     */
    args.clear();

    /* do not traverse into the icon panel
     *
     * RE: encoded resource pointer -49 is XmNtraversalOn in SGI
     * FastStrDefs.h and is passed with value 0. the original constructor
     * adds one more string-named Boolean resource immediately after this;
     * its spelling is not recovered yet, so it remains omitted here.
     */
    args.setArg(XmNtraversalOn, False);

    _iconFinderPanel = new ImdModelBasedIconPanel(
        "iconFinderPanel",
        _iconFinderFrame,
        0,
        args.getArgList(),
        args.getNumArgs());

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
    if (content) {
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
