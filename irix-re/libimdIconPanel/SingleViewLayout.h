#ifndef IRIX_RE_LIBIMDICONPANEL_SINGLEVIEWLAYOUT_H
#define IRIX_RE_LIBIMDICONPANEL_SINGLEVIEWLAYOUT_H

#include "ImdIconPanel.h"

/*
 * ImdSingleViewLayout
 *
 * Binary anchor: libimdIconPanel.so
 *   ctor    0x0968db1c, 0x78 bytes
 *   resize  0x0968dae8, 0x34 bytes
 *   update  0x09689e78, 0x370 bytes
 *
 * The original constructor chains to ImdLayout(Widget), installs the derived
 * vtable and fetches one short-valued ViewKit resource into the derived state.
 */
class ImdSingleViewLayout : public ImdLayout
{
public:
    explicit ImdSingleViewLayout(Widget widget);
    virtual ~ImdSingleViewLayout();

    void resize();
    void update(bool force);

private:
    short _resourceValue;
};

#endif
