#pragma once

#include "core/state/ParamSpec.h"
#include <cmath>
#include <cstdio>

namespace bmo::deesser
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/DeesserTests.cpp for the table that holds them.
//
// The list, its order, the ranges, the steps, the defaults and the one choice
// list are the table in docs/deesser/11-integration-and-test-plan.md 3, which
// is the single authoritative copy: 10-dsp-spec.md 9 deliberately does not
// restate it, because duplicating it is how the two documents drifted apart
// the first time. They freeze at first ship; a later control appends at the
// end.
//
// **Five parameters, and the absences are decisions rather than omissions.**
// There is no ADAPT, no MIX, no ATTACK or RELEASE, no LOOKAHEAD, no
// oversampling and no stereo-link switch. Each is argued in 11 section 3 and
// each can only ever be appended after `shape`, never inserted.
//==============================================================================

inline constexpr auto kModuleId   = "deesser";
inline constexpr auto kModuleName = "BMO Defang";

// The band the detector watches and the filter cuts: centre in bell mode, the
// corner in shelf mode. 2-10 kHz covers male ~3-6 kHz and female ~6-8 kHz
// concentrations without reaching into the vowel region.
inline constexpr auto kFreq = "freq";

// Q, one parameter whatever the shape. A bell runs at it, with the engine
// clamping 0.1-40 behind it; a shelf runs at `kShelfQ` whatever it says, by
// the owner's decision of 2026-10-04 (see `kShelfQ` below).
inline constexpr auto kQ = "q";

// THRESHOLD in **prominence dB, not dBFS** -- how far the band stands out
// above the reference, anchored to the detector's `P_ref`. That is why it
// prints itself: see `detail::threshText` below.
inline constexpr auto kThresh = "thresh";

// How deep the cut is allowed to go. The 18 dB ceiling and the 1 dB floor both
// freeze: the floor is what keeps this a depth control rather than an on/off.
inline constexpr auto kRange = "range";

// Bell or high shelf, DEQ's id and DEQ's two labels. A shelf is the split-band
// mode without a crossover -- one minimum-phase filter that cannot
// mis-reconstruct.
inline constexpr auto kShape = "shape";

enum Index { freq, q, thresh, range, shape, count };

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// The shape choice list. Index order is permanent with the ids, and the labels
// are BMO DEQ's own (modules/deq/params.h, kShapeNames) so that two modules
// doing the same thing to a band do not call it two different things.
//==============================================================================

enum ShapeChoice { bell = 0, highShelf, numShapes };

/** The one Q a shelf runs at, whatever the knob says. **The owner's decision,
    2026-10-04: in SHELF shape there is no boost, and the cut never goes past
    RANGE.**

    The panel's band sketch found the first problem -- at the default Q of 2.5
    the shelf drew a resonant dip below its corner and a climb back above it --
    and a cap of 2, BMO DEQ's figure, was put behind the knob. A cap of 2 still
    let the shelf resonate: at 6.5 kHz and 48 kHz, RANGE 8 rose +3.53 dB at
    4.5 kHz and cut 11.49 dB, and RANGE 18 rose +5.41 dB and cut 23.25. A
    de-esser turning the presence region up while it works, and cutting 5 dB
    past the control that claims to be its ceiling, is not what RANGE says.

    **Why 0.707.** Scanned over corners 2-10 kHz, every depth to 18 dB, and
    44.1, 48, 96 and 192 kHz, the largest rise above unity anywhere from 20 Hz
    to 20 kHz is +0.00026 dB at 0.707, and the deepest cut never passes the
    depth. The largest Q that keeps the rise under 0.05 dB is about 0.751, but
    there the cut already goes 0.05 dB past RANGE; 0.707, the maximally flat
    shelf, does neither.

    **Fixed, not a cap.** The knob's own minimum is 0.7, so a cap at 0.707
    would leave the bottom 1 % of the knob still moving the shelf by an amount
    nothing could measure, and a knob the panel dims as inert would not quite
    be. So the shelf ignores the knob outright, and Q is a control the
    shelf makes inert (modules/AGENTS.md, inactive controls).

    **Q stays one parameter whatever the shape** -- 0.7 to 6 on the knob, in
    both shapes, permanently -- and the shelf's own value is applied behind it.
    This is a change to what the knob does to the audio, by decision; it is
    not a schema change.

    It lives here rather than in the DSP so that there is one definition. The
    engine's cut, its detector, the sketch, and anything else that designs or
    draws this band all read a shelf's Q through `effectiveQ`, and so they
    cannot disagree. BMO DEQ keeps its own cap of 2 (`modules/deq/params.h`);
    that one is not changed by this decision. */
inline constexpr float kShelfQ = 0.707f;

/** The Q this band actually runs at: its knob for a bell, `kShelfQ` for a
    shelf. `shapeChoice` is the stored choice index (`ShapeChoice`). */
inline constexpr float effectiveQ (int shapeChoice, float q) noexcept
{
    return shapeChoice == highShelf ? kShelfQ : q;
}

namespace detail
{
    /** "+3.0 dB over". **Prominence over the threshold, not dBFS** -- and the
        string is why this is a `textParam` rather than a plain Decibels one.

        A bare "+3.0 dB" reads as a level, and a level is exactly what this
        number is not: it is how far the band stands above the detector's
        reference, which is normalised so that an input-gain change cancels
        (docs/deesser/10-dsp-spec.md 3). Printed as dBFS it invites the
        threshold-riding across a take that this detection style exists to
        abolish, so the word "over" is carried in the value itself, where a
        host's automation lane will show it too.

        ParamFormat is Plain by construction (core/state/ParamSpec.h), which is
        what the textParam factory guarantees.

        The sign follows ParamFormat::Decibels exactly -- a leading "+" above
        zero and nothing at or below it -- so this reads like every other dB
        figure in the suite apart from the trailing word. */
    inline std::string threshText (float prominenceDb)
    {
        // **Round first, then take the sign from the rounded value.**
        //
        // This took the sign off the raw float and printed the digits with
        // %.1f, which is two derivations of one fact, and they disagreed: any
        // value in (0, 0.05) came out "+0.0 dB over" and anything in
        // (-0.05, 0) came out "-0.0". A threshold that is zero to the precision
        // being shown has to read as zero.
        //
        // macOS is where CI caught it, and only because the default lands a
        // hair above zero there where Windows and Linux land exactly on it:
        // `thresh` round-trips through a host's 32-bit normalised float, and
        // -24 + 0.5 * 48 is not obliged to come back as exactly 0 on every
        // target. The formatter was wrong on all three; one of them was honest
        // about it.
        auto rounded = std::round ((double) prominenceDb * 10.0) / 10.0;

        // Not a no-op assignment: -0.0 == 0.0 is true, so this branch is
        // reached for negative zero, and it clears the sign bit that would
        // otherwise print "-0.0".
        if (rounded == 0.0)
            rounded = 0.0;

        char buf[64];
        std::snprintf (buf, sizeof (buf), "%s%.1f dB over",
                       rounded > 0.0 ? "+" : "", rounded);
        return buf;
    }
}

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // FREQ. Logarithmic, because equal turns should be equal ratios on a
        // frequency control. **The 0.1 Hz step is BMO DEQ's reason**, not a
        // precision anyone turns a knob to: a host carries the value as a
        // 32-bit normalised float, and a continuous log law will not
        // round-trip exactly through one (modules/deq/params.h:144-148). The
        // step is what makes a saved session come back the frequency it was
        // saved at.
        S::logParam (kFreq, "Freq", 2000.0f, 10000.0f, 0.1f, 6500.0f, F::Hertz),

        // Q. Log for the same reason, and Plain because a Q has no unit --
        // octaves would be a legend, since the detector and the cut are
        // constant-Q (10 sections 3, 5) and the bandwidth in octaves is a
        // reading of Q rather than a second control.
        S::logParam (kQ, "Q", 0.7f, 6.0f, 0.01f, 2.5f, F::Plain),

        // THRESHOLD, in prominence dB. See detail::threshText for why it
        // prints itself rather than taking ParamFormat::Decibels. Zero sits at
        // typical vocal balance, which is what `P_ref` is fitted to.
        S::textParam (kThresh, "Threshold", -24.0f, 24.0f, 0.1f, 0.0f, &detail::threshText),

        // RANGE, the depth ceiling. 1 dB rather than 0 at the bottom: 01
        // section 4's working point is 2-6 dB, and a floor keeps this a depth
        // control instead of a way to switch the module off without saying so.
        // 18 at the top was chosen to sit under the shared needle's fixed
        // 24 dB scale so it could not pin in use. The panel now scales its bar
        // to this number instead (DeesserPanel.cpp, kMaxReductionDb), which is
        // the same relationship read the other way round -- the instrument
        // follows the parameter rather than the parameter dodging the
        // instrument. The value is frozen either way.
        S::floatParam (kRange, "Range", 1.0f, 18.0f, 0.1f, 8.0f, F::Decibels),

        // SHAPE. Bell is the default: it is the surgical one, and the shelf is
        // the shape that takes down everything above its corner.
        S::choiceParam (kShape, "Shape", { "Bell", "High Shelf" }, bell),
    };

    return s;
}

} // namespace bmo::deesser
