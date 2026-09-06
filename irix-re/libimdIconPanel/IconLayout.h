#ifndef IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H
#define IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H

#include "ImdIconPanel.h"

/*
 * ImdIconLayout
 *
 * Public names/signatures below are recovered from the original
 * libimdIconPanel.so symbol table. The portable implementation keeps only
 * state whose meaning is directly corroborated by binary/resource evidence.
 */
class ImdIconLayout : public ImdLayout
{
public:
    explicit ImdIconLayout(Widget widget);
    virtual ~ImdIconLayout();

    void setSnapToGrid(bool value);
    void truncate(bool value);
    void setScrollIncrement();

    bool portableSnapToGrid() const { return _snapToGrid; }
    int portableGridWidth() const { return _gridWidth; }
    int portableStaggerOffset() const { return _staggerOffset; }

private:
    bool _snapToGrid;
    bool _truncateRequested;

    int _gridWidth;
    int _staggerOffset;
    int _minimumRightIconPadding;
    int _baseOfIconPadding;
    int _topMargin;
    int _bottomMargin;
    int _leftMargin;
    int _rightMargin;
    int _overlapLimit;
    int _minimumIconWidth;
    int _maximumIconWidth;
};

#endif /* IRIX_RE_LIBIMDICONPANEL_ICONLAYOUT_H */
