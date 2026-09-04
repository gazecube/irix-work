#include "PortableImdFtrIcon.h"

#include <algorithm>

PortableImdFtrIcon::PortableImdFtrIcon()
    : _x(0),
      _y(0),
      _xScale(1.0f),
      _yScale(1.0f),
      _iconState(0)
{
}

bool PortableImdFtrIcon::load(const std::string &path)
{
    return _backend.load(path);
}

bool PortableImdFtrIcon::loaded() const
{
    return _backend.loaded();
}

void PortableImdFtrIcon::setOffset(int x, int y)
{
    _x = x;
    _y = y;
}

void PortableImdFtrIcon::setScale(float xScale, float yScale)
{
    _xScale = xScale;
    _yScale = yScale;
}

void PortableImdFtrIcon::setIconState(int state)
{
    _iconState = state;
}

void PortableImdFtrIcon::draw(Widget widget) const
{
    if (!widget || !_backend.loaded())
        return;

    /*
     * ImdIconRenderer::drawIcons() in libimdIconPanel.so sets both scale axes
     * to the same value for the normal icon path. FtiVectorIcon currently
     * accepts one square extent, so use the smaller axis if they ever differ.
     *
     * The original ImdFtrIcon scale is expressed relative to the 100-unit FTI
     * coordinate space; Clogin's recovered inverted-tee path yields a 50-pixel
     * icon extent, therefore the portable caller supplies 0.5 here.
     */
    float scale = std::min(_xScale, _yScale);
    if (scale <= 0.0f)
        return;

    const unsigned int extent = static_cast<unsigned int>(scale * 100.0f + 0.5f);
    if (extent == 0)
        return;

    /*
     * _iconState is retained because setIconState() is binary-proven in the
     * original call sequence. State-specific rendering is intentionally not
     * invented here; it will be implemented when the enum/state branches in
     * ImdFtrIconX are recovered.
     */
    (void)_iconState;
    _backend.draw(widget, _x, _y, extent);
}
