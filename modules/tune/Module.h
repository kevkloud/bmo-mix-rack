#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::tune
{

/** BMO Tune RT as the suite's products see a module: its specs, its DSP and
    its panel. */
const ModuleDef& module();

/** Lime, the product's own colour (Frosty, 2026-09-10). Chosen fresh rather
    than from the rack's reserved list, because this product is not a rack
    module; everything else about it is derived by the suite's rules. */
inline const juce::Colour kAccent { 0xffb6e35d };

} // namespace bmo::tune
