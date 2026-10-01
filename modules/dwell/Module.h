#pragma once

#include "core/product/ModuleDef.h"

namespace bmo::dwell
{

/** The module, as the products and the rack see it. */
const ModuleDef& module();

/** **Charcoal, BMO Dwell's ink on a light ground** (Frosty, 2026-10-01: "use
    charcoal for the tracks and labels"). The ink `ui::accentInk` derives from
    the Pikachu yellow on the pale plate is olive, and that was turned down on
    renders against amber, deep brown and cheek red. Declared from `module()`
    through `ui::declareLightInk`, so every caption, dotted track, mark and
    legend on a light ground takes it without the panel naming a colour. The
    dark plate keeps the yellow. 9.85:1 on `#efefef`. */
inline constexpr juce::uint32 kLightInk = 0xff3a3a3e;

/** **Cheek red, for the module's own lit buttons** (Frosty, 2026-10-01:
    "cheek red for the buttons") -- SEND, HOLD, CHOP, FX, the lane's ON and
    LINK, and the glow behind them, in both appearances. **And the top button
    row on every page** ("the top button row on each tab should be red"):
    CHARACTER on TONE, the gates on LANE, and the main FX types on FX.

    **The choice rows light in the accent, not the suite's azure** (Frosty,
    2026-10-01, "A, but with yellow instead of azure"): STEREO and the lane's FX
    types select in Pikachu yellow; CHARACTER and the main FX types are red, as
    the top row of their pages. BMO Dwell is the one module whose
    selection rows are not `switchAlt`, and that is a decision, not drift. SYNC
    keeps `switchAlt`; it ships disabled, so it is never lit. */
inline constexpr juce::uint32 kGateColour = 0xffb3261e;

/** How strongly the lane's FX row and AMOUNT show the accent while FX LINK
    holds them to the main delay's: dimmed by transparency, so they read as the
    same yellow, quieter, rather than an olive step toward grey (Frosty,
    2026-10-01, "dim instead of grey"). */
inline constexpr float kFollowAlpha = 0.55f;

} // namespace bmo::dwell
