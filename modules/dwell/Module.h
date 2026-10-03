#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::dwell
{

/** The module, as the products and the rack see it. */
const ModuleDef& module();

/** How strongly the lane's FX row and AMOUNT show the accent while FX LINK
    holds them to the main delay's: dimmed by transparency, so they read as the
    same colour, quieter, rather than a step toward the hairline grey (Frosty,
    2026-10-01, "dim instead of grey"). */
inline constexpr float kFollowAlpha = 0.55f;

} // namespace bmo::dwell
