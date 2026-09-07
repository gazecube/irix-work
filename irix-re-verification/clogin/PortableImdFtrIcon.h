#ifndef IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H
#define IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H

#include "FtiVectorIcon.h"

#include <X11/Intrinsic.h>
#include <algorithm>
#include <string>

/*
 * Portable bridge shaped after the binary-proven ImdFtrIcon API used by
 * ImdIconRenderer::drawIcons(). The original renderer performs, in order:
 *
 *   setOffset(x, y)
 *   setScale(scale, scale)
 *   setIconState(state)
 *   virtual draw(...)
 *
 * This preserves that object-level call shape while FtiVectorIcon remains the
 * temporary drawing backend. It does not claim the original ImdFtrIcon ABI.
 */
class PortableImdFtrIcon
{
public:
    PortableImdFtrIcon()
        : _x(0), _y(0), _xScale(1.0f), _yScale(1.0f), _iconState(0)
    {
    }

    bool load(const std::string &path) { return _backend.load(path); }
    bool loaded() const { return _backend.loaded(); }

    void setOffset(int x, int y)
    {
        _x = x;
        _y = y;
    }

    void setScale(float xScale, float yScale)
    {
        _xScale = xScale;
        _yScale = yScale;
    }

    void setIconState(int state) { _iconState = state; }

    void draw(Widget widget) const
    {
        if (!widget || !_backend.loaded())
            return;

        const float scale = std::min(_xScale, _yScale);
        if (scale <= 0.0f)
            return;

        const unsigned int extent =
            static_cast<unsigned int>(scale * 100.0f + 0.5f);
        if (extent == 0)
            return;

        /*
         * State is retained because setIconState() is binary-proven. Do not
         * invent selected/disabled rendering until ImdFtrIconX state handling
         * is decoded.
         */
        (void)_iconState;
        _backend.draw(widget, _x, _y, extent);
    }

private:
    FtiVectorIcon _backend;
    int _x;
    int _y;
    float _xScale;
    float _yScale;
    int _iconState;
};

#endif /* IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H */
