#pragma once

#include "core/state/ParamSpec.h"

namespace bmo::opto
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/OptoTests.cpp for the table that holds them.
//==============================================================================

inline constexpr auto kModuleId   = "opto";
inline constexpr auto kModuleName = "BMO Opto";

// Two controls the panel's ear cares about, on purpose: the real hardware
// this is modelled on has no threshold, ratio, attack or release knobs
// either -- see Detector.h for what CRUSH actually drives per mode.
inline constexpr auto kCrush = "crush";
inline constexpr auto kLevel = "level";

// Which hardware this instance behaves like. Two genuinely different
// circuits, not a shared curve with different numbers -- see Detector.h.
// "Tele" (Opto unit A) / "Stressed" (Comp unit B) are placeholder labels, not a
// final naming decision -- that's still open. The Mode enum in DspCore.h
// keeps the OptoUnitA/CompUnitB names internally regardless of what these
// display strings end up being.
inline constexpr auto kMode = "mode";

// Stereo link: shares one detector's gain reduction across both channels
// instead of letting them compress independently. Independent of kMode.
inline constexpr auto kLink = "link";

// Color: an on/off harmonic stage modelled on whichever hardware kMode
// currently selects -- see Detector.h. Only meaningful as a *choice* in
// Stressed mode; Tele mode always runs it (the panel disables/hides the
// switch there, and DspCore ignores this parameter's value in that mode) --
// see DspCore::process().
inline constexpr auto kColor = "color";

enum Index { crush, level, mode, link, color, count };

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // CRUSH <3: how hard the programme drives the cell. Ratio and knee
        // are fixed per mode (neither real unit has a ratio control) --
        // CRUSH only moves the effective threshold, the way the real Peak
        // Reduction knob does. Defaults to 0 (no gain reduction) -- a freshly
        // inserted instance should be heard doing nothing until the ear asks
        // for it, not compressing out of the box.
        S::floatParam (kCrush, "Crush", 0.0f, 100.0f, 0.1f, 0.0f, F::Percent), // default 35 -> 0 in 0.2.0

        // LEVEL: makeup gain after the cell. Not automatic -- the ear sets
        // it. A source-dependent auto-makeup (compensating the *average*
        // reduction; compensating the instantaneous reduction would cancel
        // the compression exactly and leave a wire) was raised and rejected
        // in 0.2.0: neither the Opto unit A's Gain knob nor the Comp unit B's
        // Output has one, and this module follows the hardware. Revisit only
        // if that fidelity call changes -- kLevel's id, range and default all
        // survive such a change, so it stays possible later.
        S::floatParam (kLevel, "Level", -24.0f, 24.0f, 0.01f, 0.0f, F::Decibels),

        S::choiceParam (kMode, "Mode", { "Tele", "Stressed" }, 0),

        S::boolParam (kLink,  "Link",  true),
        S::boolParam (kColor, "Color", false),
    };

    return s;
}

} // namespace bmo::opto
