from pathlib import Path

hp = Path('reconstruction/PandoraPresentation.h')
h = hp.read_text()
h = h.replace('    void setActivateCallback(XtCallbackProc callback, XtPointer clientData);\n',
'''    void setSelectionCallback(XtCallbackProc callback, XtPointer clientData);\n    void setCommandCallback(XtCallbackProc callback, XtPointer clientData);\n''')
h = h.replace('    XtCallbackProc _activateCallback;\n    XtPointer _activateClientData;\n',
'''    XtCallbackProc _selectionCallback;\n    XtPointer _selectionClientData;\n    XtCallbackProc _commandCallback;\n    XtPointer _commandClientData;\n    UserView *_selectedView;\n    Time _lastClickTime;\n''')
hp.write_text(h)

pp = Path('reconstruction/PandoraPresentation.c++')
s = pp.read_text()
s = s.replace('''      _fontProbe(NULL),\n      _activateCallback(NULL),\n      _activateClientData(NULL)\n''',
'''      _fontProbe(NULL),\n      _selectionCallback(NULL),\n      _selectionClientData(NULL),\n      _commandCallback(NULL),\n      _commandClientData(NULL),\n      _selectedView(NULL),\n      _lastClickTime(0)\n''')
s = s.replace('''void PandoraLayout::setActivateCallback(\n    XtCallbackProc callback,\n    XtPointer clientData)\n{\n    _activateCallback = callback;\n    _activateClientData = clientData;\n}\n''',
'''void PandoraLayout::setSelectionCallback(\n    XtCallbackProc callback,\n    XtPointer clientData)\n{\n    _selectionCallback = callback;\n    _selectionClientData = clientData;\n}\n\nvoid PandoraLayout::setCommandCallback(\n    XtCallbackProc callback,\n    XtPointer clientData)\n{\n    _commandCallback = callback;\n    _commandClientData = clientData;\n}\n''')
old = '''    UserView *view = self->viewAt(\n        static_cast<Position>(event->xbutton.x),\n        static_cast<Position>(event->xbutton.y));\n    if (view && self->_activateCallback)\n    {\n        self->_activateCallback(\n            widget,\n            self->_activateClientData,\n            (XtPointer)view);\n    }\n'''
new = '''    UserView *view = self->viewAt(\n        static_cast<Position>(event->xbutton.x),\n        static_cast<Position>(event->xbutton.y));\n    if (!view)\n        return;\n\n    const bool selectionChanged = view != self->_selectedView;\n    if (selectionChanged)\n    {\n        self->_selectedView = view;\n        if (self->_selectionCallback)\n            self->_selectionCallback(\n                widget, self->_selectionClientData, (XtPointer)view);\n    }\n\n    /*\n     * Portable bridge for the binary-proven CheckForDoubleClick()/command\n     * channel. The original ImdIconPanel owns this state and emits\n     * kCommandCallback separately from kSelectionChangedCallback. Keep the\n     * channels separate here until the complete Imd callback/list ABI is\n     * reconstructed.\n     */\n    const Time now = event->xbutton.time;\n    const int multiClick = XtGetMultiClickTime(XtDisplay(widget));\n    const bool command = !selectionChanged && self->_lastClickTime != 0 &&\n        now - self->_lastClickTime <= static_cast<Time>(multiClick);\n    self->_lastClickTime = now;\n\n    if (command && self->_commandCallback)\n        self->_commandCallback(\n            widget, self->_commandClientData, (XtPointer)view);\n'''
if old not in s:
    raise SystemExit('old panelButtonEvent callback block not found')
s = s.replace(old, new, 1)
pp.write_text(s)

vh = Path('reconstruction/PandoraViewKit.h')
h = vh.read_text()
h = h.replace('    static void userViewActivateCB(Widget, XtPointer, XtPointer);\n',
'''    static void userViewSelectionCB(Widget, XtPointer, XtPointer);\n    static void userViewCommandCB(Widget, XtPointer, XtPointer);\n''')
vh.write_text(h)

vp = Path('reconstruction/PandoraViewKit.c++')
s = vp.read_text()
s = s.replace('''        _layout = new PandoraLayout(panelWidget);\n        _layout->setActivateCallback(\n            &PandoraComponent::userViewActivateCB,\n            (XtPointer)this);\n''',
'''        _layout = new PandoraLayout(panelWidget);\n        _layout->setSelectionCallback(\n            &PandoraComponent::userViewSelectionCB,\n            (XtPointer)this);\n        _layout->setCommandCallback(\n            &PandoraComponent::userViewCommandCB,\n            (XtPointer)this);\n''')
s = s.replace('void PandoraComponent::userViewActivateCB(\n',
              'void PandoraComponent::userViewSelectionCB(\n', 1)
marker = '''void PandoraComponent::userTextActivateCB(\n'''
idx = s.index(marker)
command = '''void PandoraComponent::userViewCommandCB(\n    Widget,\n    XtPointer clientData,\n    XtPointer callData)\n{\n    PandoraComponent *self = static_cast<PandoraComponent *>(clientData);\n    UserView *view = static_cast<UserView *>(callData);\n    if (!self || !view)\n        return;\n\n    /*\n     * Binary-proven commandCB resolves the command view's login and passes it\n     * to pandora::doubleClicked(). That state machine is the next RE target.\n     * Preserve the separate command boundary now; the portable action remains\n     * limited to keeping the selected login/prompt active until doubleClicked\n     * is reconstructed from the binary.\n     */\n    if (self->_userText)\n        XmTextSetString(\n            self->_userText, const_cast<char *>(view->name().c_str()));\n    self->showPasswordPrompt();\n}\n\n'''
s = s[:idx] + command + s[idx:]
vp.write_text(s)
