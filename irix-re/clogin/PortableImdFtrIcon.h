#ifndef IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H
#define IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H

#include "FtiVectorIcon.h"

#include <X11/Intrinsic.h>
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
 * This class preserves that object-level call shape while FtiVectorIcon remains
 * the temporary drawing backend. It is not claimed to reproduce the original
 * ImdFtrIcon/ImdFtrIconX ABI or object layout.
 */
class PortableImdFtrIcon
{
public:
    PortableImdFtrIcon();

    bool load(const std::string &path);
    bool loaded() const;

    void setOffset(int x, int y);
    void setScale(float xScale, float yScale);
    void setIconState(int state);

    void draw(Widget widget) const;

private:
    FtiVectorIcon _backend;
    int _x;
    int _y;
    float _xScale;
    float _yScale;
    int _iconState;
};

#endif /* IRIX_RE_CLOGIN_PORTABLE_IMD_FTR_ICON_H */
