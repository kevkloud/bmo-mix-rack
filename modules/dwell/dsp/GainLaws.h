#pragma once

#include <algorithm>
#include <cmath>

/*  The two loop-gain laws, on their own so the panel can draw with them.

    BMO Dwell's screen pictures the repeats -- how many, how fast they fall,
    whether the lane holds or builds -- and a picture computed any other way
    than the engine computes it would drift from the sound the first time
    either changed. So the laws live here, JUCE-free and with no state, and
    `DspCore.h` and `panel/DwellPanel.cpp` both include this one header. The
    DSP tests are what prove the move changed nothing. */

namespace bmo::dwell
{

//==============================================================================
/** docs/delay/10 §3's FEEDBACK law, `g = (1.05 . fb^1.6) / P_c`.

    A free function because the **law is not the engine's** (10 §11.1): the
    main delay maps FEEDBACK through this one, the lane maps `lane_gain`
    through §11.2's bipolar law, and an engine that knew both would be an
    engine that knew which instance it was. `P_c` comes from the engine the
    gain is for, which computes it by sweep -- it is never a constant here.

    The exponent puts resolution in the 2-8-repeat region; the 1.05 puts the
    loop's peak magnitude at 1.000 at fb = 97.0 % and 1.05 at full travel, on
    every character, which is what lets the panel carry one self-oscillation
    tick rather than one per character. */
inline float feedbackGainFor (float feedbackPercent, double loopPeak) noexcept
{
    const auto fb = std::clamp ((double) feedbackPercent * 0.01, 0.0, 1.0);
    return (float) (1.05 * std::pow (fb, 1.6) / std::max (loopPeak, 1.0e-6));
}

//==============================================================================
/** **`g_max`, the lane's build ceiling -- CALIBRATE** (10 §11.2, §12).

    Frosty settles this by ear in `14` §3, and it is **not** settled here. 1.10
    is the provisional this build runs at, taken because 10 §11.2 works the
    arithmetic for exactly three candidates and this is the middle one: at the
    default 250 ms lane time 1.05 is ~1.7 dB/s and about twelve seconds from
    unity to the ceiling, which is slow for a control called BUILD; 1.10 is
    ~3.3 dB/s and about six seconds, which is a swell a player can ride; 1.3 is
    ~10 dB/s and is over before a bar is. Six seconds is the one that can be
    *performed*, so it is the defensible place to start a listening round from.

    It is a **starting gain and not a bound**: 10 §11.6's in-loop safety clip
    governs whatever this number is, which is why moving it after a listening
    pass cannot destabilise anything. */
inline constexpr double kLaneBuildCeiling = 1.10;

/** 10 §11.2's bipolar tail, as a free function for the same reason
    `feedbackGainFor` is one: **the law is not the engine's**. The main delay
    maps FEEDBACK through one law and the lane maps `lane_gain` through this
    one, and an engine that knew both would be an engine that knew which
    instance it was.

    | region | loop gain |
    |---|---|
    | `L < 0` -- THROW  | `(1 + L)^1.6 / P_c` |
    | `L = 0` -- FREEZE | `1 / P_c` **exactly** |
    | `L > 0` -- BUILD  | `(1 + (g_max - 1) L^2) / P_c` |

    The decay region is §3's feedback law rescaled so that unity sits at the
    top of the region rather than at 97 % of it -- same exponent, same feel,
    same resolution in the 2-8-repeat region. The build region is quadratic so
    that the curve leaves the detent with **zero slope**, which is what makes
    the sticky centre feel like part of the travel rather than a notch cut into
    it, and so that the useful resolution sits just above the detent where the
    difference between 1.01 and `g_max` lives.

    **The detent is a literal, not a limit.** `L == 0` returns `1 / P_c`
    without going through either curve, and the caller **snaps** the smoother
    onto it (10 §11.2): a smoothed approach leaves 0.9999 circulating, which is
    a hold that quietly decays, and a control labelled FREEZE is a promise that
    centre is unity. §3's normalisation is the other half of that promise --
    without it, unity would sit at a different knob position on every
    character. */
inline float laneGainFor (float laneGainPercent, double loopPeak) noexcept
{
    const auto p = 1.0 / std::max (loopPeak, 1.0e-6);
    const auto l = std::clamp ((double) laneGainPercent * 0.01, -1.0, 1.0);

    if (l < 0.0)
        return (float) (std::pow (1.0 + l, 1.6) * p);

    if (l > 0.0)
        return (float) ((1.0 + (kLaneBuildCeiling - 1.0) * l * l) * p);

    return (float) p;
}

} // namespace bmo::dwell
