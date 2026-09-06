/*
 * FmIconSurface.c++ - temporary native view surface for fm
 *
 * The original libimdIconPanel binary proves that ImdIconPanel owns
 * ImdIconView objects and that ImdViewPanel handles drawing, selection and
 * layout. Until those renderer bodies are fully reconstructed, keep directory
 * entries as real ImdIconView objects and draw them directly on the live IMD
 * panel widget. This deliberately avoids inserting another Motif child widget.
 */

#include "FmIconSurface.h"

#include <X11/Xlib.h>
#include <algorithm>
#include <cstring>

namespace
{
const int kTopMargin = 8;
const int kLeftMargin = 8;
const int kTextOffset = 18;
const int kRowHeight = 20;
const Time kDoubleClickTime = 400;
}

class FmIconSurface::View : public ImdIconView
{
public:
    View(const std::string &name, bool directory, size_t index)
        : _name(name), _directory(directory), _index(index)
    {
    }

    const std::string &name() const { return _name; }
    bool directory() const { return _directory; }
    size_t index() const { return _index; }

private:
    std::string _name;
    bool _directory;
    size_t _index;
};

FmIconSurface::FmIconSurface(ImdIconPanel *panel, Widget widget)
    : _panel(panel),
      _widget(widget),
      _activateProc(0),
      _activateClosure(0),
      _lastClickTime(0),
      _lastClickIndex(-1)
{
    XtAddEventHandler(_widget,
                      ExposureMask | ButtonPressMask,
                      False,
                      eventHandler,
                      (XtPointer)this);
}

FmIconSurface::~FmIconSurface()
{
    if (_widget)
        XtRemoveEventHandler(_widget,
                             ExposureMask | ButtonPressMask,
                             False,
                             eventHandler,
                             (XtPointer)this);

    if (_panel)
        _panel->portableResetViews(true);
}

void
FmIconSurface::setActivateProc(FmIconSurfaceActivateProc proc, void *closure)
{
    _activateProc = proc;
    _activateClosure = closure;
}

void
FmIconSurface::setEntries(const std::vector<FmIconSurfaceEntry> &entries)
{
    _entries = entries;
    _selected.assign(_entries.size(), false);
    _lastClickTime = 0;
    _lastClickIndex = -1;

    /*
     * RE: original libimdIconPanel exports both ImdIconPanel::adoptView() and
     * ImdViewPanelDeleteAllViews(). The full delete path is not online yet, so
     * portableResetViews() is the temporary ownership seam around that binary-
     * proven lifecycle.
     */
    _panel->portableResetViews(true);

    ImdViewPanelBeginBlockChanges(_widget);
    for (size_t i = 0; i < _entries.size(); ++i)
        _panel->adoptView(new View(_entries[i].name, _entries[i].directory, i));
    ImdViewPanelEndBlockChanges(_widget);

    if (XtIsRealized(_widget))
    {
        XClearWindow(XtDisplay(_widget), XtWindow(_widget));
        draw();
    }
}

bool
FmIconSurface::selectedIndex(size_t *index) const
{
    for (size_t i = 0; i < _selected.size(); ++i)
    {
        if (_selected[i])
        {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

void
FmIconSurface::selectAll()
{
    std::fill(_selected.begin(), _selected.end(), true);
    draw();
}

int
FmIconSurface::indexAtY(int y) const
{
    if (y < kTopMargin)
        return -1;

    int index = (y - kTopMargin) / kRowHeight;
    if (index < 0 || (size_t)index >= _entries.size())
        return -1;
    return index;
}

void
FmIconSurface::eventHandler(
    Widget widget,
    XtPointer client,
    XEvent *event,
    Boolean *dispatch)
{
    FmIconSurface *surface = (FmIconSurface *)client;
    (void)widget;
    (void)dispatch;
    surface->handleEvent(event);
}

void
FmIconSurface::handleEvent(XEvent *event)
{
    if (!event)
        return;

    if (event->type == Expose)
    {
        if (event->xexpose.count == 0)
            draw();
        return;
    }

    if (event->type == ButtonPress && event->xbutton.button == Button1)
    {
        int index = indexAtY(event->xbutton.y);
        if (index < 0)
            return;

        bool doubleClick =
            (_lastClickIndex == index &&
             event->xbutton.time >= _lastClickTime &&
             event->xbutton.time - _lastClickTime <= kDoubleClickTime);

        std::fill(_selected.begin(), _selected.end(), false);
        _selected[(size_t)index] = true;
        _lastClickIndex = index;
        _lastClickTime = event->xbutton.time;
        draw();

        if (doubleClick && _activateProc)
        {
            _lastClickTime = 0;
            _lastClickIndex = -1;
            _activateProc((size_t)index, _activateClosure);
        }
    }
}

void
FmIconSurface::draw()
{
    if (!_widget || !XtIsRealized(_widget))
        return;

    Display *display = XtDisplay(_widget);
    Window window = XtWindow(_widget);
    int screen = DefaultScreen(display);
    GC gc = XCreateGC(display, window, 0, 0);
    XSetForeground(display, gc, BlackPixel(display, screen));

    Dimension width = 1;
    XtVaGetValues(_widget, XtNwidth, &width, NULL);

    for (size_t i = 0; i < _entries.size(); ++i)
    {
        int y = kTopMargin + (int)i * kRowHeight;
        std::string label = _entries[i].name;
        if (_entries[i].directory)
            label += "/";

        if (i < _selected.size() && _selected[i])
        {
            unsigned int rowWidth = width > 2 * kLeftMargin
                ? (unsigned int)width - 2 * kLeftMargin
                : 1;
            XDrawRectangle(display, window, gc,
                           kLeftMargin - 2, y,
                           rowWidth, kRowHeight - 2);
        }

        if (_entries[i].directory)
        {
            XDrawRectangle(display, window, gc,
                           kLeftMargin, y + 4, 9, 8);
            XDrawLine(display, window, gc,
                      kLeftMargin + 1, y + 3,
                      kLeftMargin + 5, y + 3);
        }
        else
        {
            XDrawRectangle(display, window, gc,
                           kLeftMargin + 1, y + 2, 7, 10);
        }

        XDrawString(display, window, gc,
                    kLeftMargin + kTextOffset,
                    y + 13,
                    label.c_str(),
                    (int)label.size());
    }

    XFreeGC(display, gc);
}
