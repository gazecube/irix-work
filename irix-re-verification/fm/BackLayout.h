#ifndef IRIX_RE_FM_BACKLAYOUT_H
#define IRIX_RE_FM_BACKLAYOUT_H

#include "IconLayout.h"

/*
 * BackLayout
 *
 * RE: original fm exports BackLayout as the desktop/background layout and
 * its constructor begins with ImdIconLayout(panel). Keep the portable class
 * deliberately small until more of the original virtual surface is proven.
 */
class BackLayout : public ImdIconLayout
{
public:
    explicit BackLayout(Widget panel);
    virtual ~BackLayout();

    void useShapes(bool enable);
    void shapePretty(bool enable);

    bool portableUsingShapes() const { return _useShapes; }
    bool portableShapePretty() const { return _shapePretty; }

private:
    bool _useShapes;
    bool _shapePretty;
};

#endif /* IRIX_RE_FM_BACKLAYOUT_H */
