/*
 * BackLayoutPortable.c++ - runnable desktop layout seam
 *
 * The class relationship and initial shape-state values are binary-proven
 * from the original IRIX fm BackLayout constructor. Shape-region mutation is
 * kept out of this portable body until the remaining original calls are
 * represented through stable reconstructed ImdViewPanel APIs.
 */

#include "BackLayout.h"

BackLayout::BackLayout(Widget panel)
    : ImdIconLayout(panel),
      _useShapes(false),
      _shapePretty(true)
{
}

BackLayout::~BackLayout()
{
}

void
BackLayout::useShapes(bool enable)
{
    _useShapes = enable;
}

void
BackLayout::shapePretty(bool enable)
{
    if (!_useShapes)
        return;
    _shapePretty = enable;
}
