#include "SingleViewLayout.h"

ImdSingleViewLayout::ImdSingleViewLayout(Widget widget)
    : ImdLayout(widget),
      _resourceValue(0)
{
    /*
     * Original ctor @ 0x0968db1c calls:
     *   ImdLayout(widget)
     *   VkGetResource(widget, <name>, -380, -104, <default>)
     * and stores the returned short at derived offset +0x0c.
     *
     * The exact resource spelling/default are not yet decoded from the SGI
     * FAST_QUARK/ViewKit tables, so do not invent them here.  The class and
     * constructor ownership are nevertheless binary-proven and are required
     * by ImdIconFinder's original construction path.
     */
}

ImdSingleViewLayout::~ImdSingleViewLayout()
{
}

void ImdSingleViewLayout::resize()
{
    /* Original 0x0968dae8; body lands with the layout pass. */
}

void ImdSingleViewLayout::update(bool force)
{
    (void)force;
    /* Original 0x09689e78; body lands with the layout pass. */
}
