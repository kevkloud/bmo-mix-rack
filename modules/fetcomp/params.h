#pragma once

#include "core/state/ParamSpec.h"
#include <cmath>
#include <cstdio>

namespace bmo::fetcomp
{

//==============================================================================
// Parameter IDs. Permanent and append-only -- see modules/eq/params.h for why,
// and tests/plugin/FetcompTests.cpp for the table that holds them.
//
// The list, its order, the ranges, the steps, the defaults and the two choice
// lists are the table in docs/1176-comp/11-integration-and-test-plan.md 2.
// They freeze at first ship; a later control appends at the end.
//==============================================================================

inline constexpr auto kModuleId   = "fetcomp";
inline constexpr auto kModuleName = "BMO FET";

// INPUT drives the compressor and OUTPUT is makeup. **There is no threshold
// knob** -- the hardware family this is modelled on has none, and how hard you
// drive the input is how much reduction you get. Gain reduction is reported
// through currentGainReductionDb(), never as a parameter.
inline constexpr auto kInput  = "input";
inline constexpr auto kOutput = "output";

// ATTACK and RELEASE are the *knob position*, 1..7 continuous, 7 fastest --
// see the block below. Not milliseconds, deliberately.
inline constexpr auto kAttack  = "attack";
inline constexpr auto kRelease = "release";

// RATIO, including the all-buttons position. It is one choice rather than four
// switches plus a mode, because that is what it is: five states, one of which
// is every button pushed in at once.
inline constexpr auto kRatio = "ratio";

// MIX ships in v1 -- decided, not conditional. Its dry path is delay-matched
// to the oversampler, or a partial blend combs.
inline constexpr auto kMix = "mix";

// VOICING: two constant sets over one topology, gain-matched, identical
// latency. Black is the default. The panel shows which one is running as the
// border around the VU meter and nowhere else.
inline constexpr auto kVoicing = "voicing";

// OVERSAMPLING: Off / 2x / 4x through the shared core/dsp/Oversampler.h, Off
// by default so the module reports zero latency where it rests. It clicks and
// re-syncs host delay compensation, so it is a setup control rather than an
// automation target.
inline constexpr auto kOversampling = "oversampling";

enum Index { input, output, attack, release, ratio, mix, voicing, oversampling, count };

inline constexpr int kVersionHint   = 1;
inline constexpr int kStateVersion  = 1;
inline constexpr int kSchemaVersion = 1;

//==============================================================================
// Attack and release are the knob position, and they run backwards
//
// **The parameter *is* the hardware's printed position** -- 1 slowest, 7
// fastest, continuous between them -- and the DSP maps position to time. The
// reason is automation: had the parameter stayed in milliseconds ascending
// with only the knob drawn reversed, the host's lane and the knob would move
// in opposite directions, and a panel cannot fix that, because the lane *is*
// the parameter.
//
// It costs nothing elsewhere. Position is linear and the law below is
// exponential in position, so the logarithmic time sweep falls out with no
// parameter skew at all -- which is why these are floatParam and not logParam.
//
// **This departs from the one house precedent**, LTV Comp's attack/release,
// which are logParam in ms ascending (modules/vcomp/params.h). Taken
// knowingly: that module models no hardware knob and has no direction to
// honour, this one does.
//
// The laws are docs/1176-comp/10-dsp-spec.md 10, and they live here rather
// than in the DSP because they are the permanent *definition* of what these
// two parameters mean -- the value string and the coefficient both derive
// from them, and the two may not be allowed to disagree.
//==============================================================================

inline constexpr float kPositionMin = 1.0f;   ///< slowest
inline constexpr float kPositionMax = 7.0f;   ///< fastest
inline constexpr float kPositionDefault = 4.0f;

/** Attack time in microseconds for a knob position. 800 / 126.5 / 20 us at
    positions 1 / 4 / 7. */
inline float attackMicrosecondsFor (float position) noexcept
{
    const auto p = position < kPositionMin ? kPositionMin
                                           : (position > kPositionMax ? kPositionMax : position);
    return 800.0f * std::pow (20.0f / 800.0f, (p - 1.0f) / 6.0f);
}

/** Release time in milliseconds for a knob position. 1100 / 234.5 / 50 ms at
    positions 1 / 4 / 7. */
inline float releaseMillisecondsFor (float position) noexcept
{
    const auto p = position < kPositionMin ? kPositionMin
                                           : (position > kPositionMax ? kPositionMax : position);
    return 1100.0f * std::pow (50.0f / 1100.0f, (p - 1.0f) / 6.0f);
}

namespace detail
{
    /** The position, printed as an integer where it lands on one. A knob that
        has been dragged sits at 4.37 and should say so; one that has been
        typed, automated to an end, or left alone sits exactly on a detent and
        should not say "4.0". */
    inline void printPosition (char* buf, size_t size, float position)
    {
        const auto whole = std::round (position);

        if (std::abs (position - whole) < 0.005f)
            std::snprintf (buf, size, "%d", (int) whole);
        else
            std::snprintf (buf, size, "%.2f", (double) position);
    }

    /** "4 (126 us)". The format is Plain -- the number alone is a position and
        means nothing to anyone reading a host's automation lane -- so the
        string carries the time the position selects as well.

        "us" and not the micro sign: the two display faces are licensed
        individually and live outside this repository (see .bmo-fontdir), so a
        glyph outside ASCII is one this suite cannot promise it can draw. The
        only non-ASCII glyph in the tree is the polarity mark, which is drawn
        by BmoLookAndFeel from a known face. */
    inline std::string attackText (float position)
    {
        char at[16];
        printPosition (at, sizeof (at), position);

        char buf[64];
        std::snprintf (buf, sizeof (buf), "%s (%.0f us)", at,
                       (double) attackMicrosecondsFor (position));
        return buf;
    }

    /** "4 (235 ms)". Whole units in both, because both ranges are whole-unit
        wide: attack runs 20 to 800 us and release 50 to 1100 ms, so neither
        ever wants the decimal ParamFormat::Milliseconds keeps for times under
        10.

        docs/1176-comp/11-integration-and-test-plan.md 2 illustrates this as
        "4 (234 ms)" against the 234.5 ms the law gives; that is the figure
        truncated where this rounds it. The example is an illustration of the
        shape, not of the rounding. */
    inline std::string releaseText (float position)
    {
        char at[16];
        printPosition (at, sizeof (at), position);

        char buf[64];
        std::snprintf (buf, sizeof (buf), "%s (%.0f ms)", at,
                       (double) releaseMillisecondsFor (position));
        return buf;
    }
}

//==============================================================================
// The ratio and voicing choice lists. Index order is permanent with the ids.
//==============================================================================

enum RatioChoice { ratio4 = 0, ratio8, ratio12, ratio20, ratioAll, numRatios };
enum VoicingChoice { blue = 0, black, numVoicings };
enum OversamplingChoice { osOff = 0, os2x, os4x, numOversampling };

inline const ParamSpecs& specs()
{
    using S = ParamSpec;
    using F = ParamFormat;

    static const ParamSpecs s
    {
        // INPUT: how hard the programme arrives at the cell, and therefore how
        // much reduction there is. It reaches +60 dB rather than the +45 an
        // earlier draft proposed, because 45 is about 5 dB short of 30 dB of
        // reduction at 4:1 from a -18 dBFS source -- and 30 dB is the design
        // target, not an extreme. See 11 section 2; the level-range DSP test
        // is what fails if this is ever reverted.
        S::floatParam (kInput, "Input", -20.0f, 60.0f, 0.01f, 0.0f, F::Decibels),

        // OUTPUT: makeup after the cell, hand-set. +/-36 for the same reason,
        // one stage later: +/-24 cannot restore 30 dB of reduction.
        S::floatParam (kOutput, "Output", -36.0f, 36.0f, 0.01f, 0.0f, F::Decibels),

        // The knob positions. No skew: the law is already exponential in
        // position, so a linear sweep is the logarithmic time sweep. See the
        // block above, and the value strings that carry the time.
        S::textParam (kAttack,  "Attack",  kPositionMin, kPositionMax, 0.01f,
                      kPositionDefault, &detail::attackText),
        S::textParam (kRelease, "Release", kPositionMin, kPositionMax, 0.01f,
                      kPositionDefault, &detail::releaseText),

        // The four ratios and the all-buttons position. All-buttons is not a
        // fifth ratio -- it is every button pushed in together, and what comes
        // out is a different curve with a standing reduction of its own.
        S::choiceParam (kRatio, "Ratio", { "4:1", "8:1", "12:1", "20:1", "All" }, ratio4),

        S::floatParam (kMix, "Mix", 0.0f, 100.0f, 0.1f, 100.0f, F::Percent),

        // Black is the default of the two. The labels are the two faceplate
        // colours the hardware family is known by, which is the one thing
        // anybody calls them; they name a *voicing*, not a revision.
        S::choiceParam (kVoicing, "Voicing", { "Blue", "Black" }, black),

        // Off, 2x, 4x -- 0 / 40 / 60 samples through the shared oversampler,
        // zero at the default. No 8x: the cell is the cost here, and 10 section 9
        // stops at 4x.
        S::choiceParam (kOversampling, "Oversampling", { "Off", "2x", "4x" }, osOff),
    };

    return s;
}

} // namespace bmo::fetcomp
