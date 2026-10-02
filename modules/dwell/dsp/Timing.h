#pragma once

#include "modules/dwell/dsp/DelayEngine.h"
#include "modules/dwell/dsp/GainLaws.h"
#include "modules/dwell/params.h"

#include <algorithm>
#include <cmath>

/*  Tempo and tail arithmetic, JUCE-free and stateless, so the adapter, the
    panel and the tests can all ask the same question the same way.

    - `syncedMs` is docs/delay/10 §7's mapping from a NOTE index at a tempo to
      a delay time, halved until it fits the ring.
    - `tailSecondsFor` is §9's tail, widened by §11.6 to the larger of the two
      engines', computed from parameters only so a host can be told before
      the audio thread has caught up (core/dsp/ModuleDsp.h). */

namespace bmo::dwell
{

/** Beats -- quarter notes -- per NOTE index, in `kNoteNames` order: 1/32 is an
    eighth of a beat, dotted is x1.5, triplet x2/3 (§7). Sixteen, ascending,
    because the index *is* the automation lane. */
inline constexpr double kNoteBeats[]
{
    0.125, 0.25 * 2.0 / 3.0, 0.1875, 0.25, 0.5 * 2.0 / 3.0, 0.375, 0.5, 1.0 * 2.0 / 3.0,
    0.75,  1.0,              2.0 * 2.0 / 3.0, 1.5, 2.0,     4.0 * 2.0 / 3.0, 3.0, 4.0
};

static_assert (std::size (kNoteBeats) == std::size (kNoteNames),
               "one beat figure per NOTE index");

/** NOTE index `choice` at `bpm`, in ms. **Halved until it fits** the
    `kMaxTimeMs` ring (§7): a whole note at 60 bpm is 4 s, which plays as 2 s
    -- the same note an octave of time shorter, on the grid, rather than a
    clamp that would put it off the grid. `bpm` is whatever the host's
    validity window let through (core/product/HostTempo.h, 10-999), which is
    wider than §7's 20-999; halving is what makes the bottom of it safe. */
inline double syncedMs (int choice, double bpm) noexcept
{
    const auto i = std::clamp (choice, 0, (int) std::size (kNoteBeats) - 1);
    auto ms = kNoteBeats[i] * 60000.0 / std::max (bpm, 1.0);

    while (ms > (double) kMaxTimeMs)
        ms *= 0.5;

    return ms;
}

/** The ceiling and floor §9 puts on a reported tail, in seconds. */
inline constexpr double kTailFloorSeconds   = 0.5;
inline constexpr double kTailCeilingSeconds = 30.0;

/** How far above its input a loop of gain `g` can stand, in dB: a sustained
    input in phase with the loop settles at `1 / (1 - g)` of itself, every
    repeat landing on the next. Bounded at 120 dB, which only a gain within a
    millionth of unity reaches and which the 30 s ceiling outruns anyway. */
inline double buildUpDb (double g) noexcept
{
    if (g <= 0.0)
        return 0.0;

    return std::min (120.0, -20.0 * std::log10 (std::max (1.0 - g, 1.0e-6)));
}

/** Laps for a loop of gain `g` to fall from where a sustained input left it
    to -60 dB of that input: `ceil((60 + buildUpDb(g)) / -20 log10 g)`, or -1
    when the loop holds or builds and never gets there. A loop with no
    feedback still plays one repeat.

    **The build-up is counted** (DECIDED, Frosty 2026-10-01). Counted from the
    first repeat alone, the figure covered a short burst and not a held note:
    one second of a tone in phase with the loop rang 3.750 s at TIME 375 ms and
    FEEDBACK 60 % against 3.391 s reported (measured on AURORA). The input the
    -60 dB is drawn from is the one the host stopped sending, whatever it was.

    **The gain is not capped.** §9 wrote `min(g, 0.97)`, and that cap counted
    the laps for a loop faster than the one running: FEEDBACK 96.9 % at TIME
    20 ms reported 4.54 s and was still ringing after 40. Uncapped, a gain near
    unity asks for thousands of laps, and the 30 s ceiling in
    `tailSecondsFor` bounds the answer, as it does for a loop at or past
    unity. */
inline double lapsToSixtyDb (double g) noexcept
{
    if (g >= 1.0)
        return -1.0;

    if (g <= 0.0)
        return 1.0;

    return std::ceil ((60.0 + buildUpDb (g)) / (-20.0 * std::log10 (g)));
}

//==============================================================================
/** One lap of the loop's filters at one frequency, as the tail sees them: how
    much of the signal survives the lap, and how late the lap makes it.

    **A lap is not exactly TIME long.** Every filter in the loop delays what
    passes through it, by its group delay, and the delay compounds per repeat
    exactly as the loss does -- so the slowest-decaying frequency is the one
    whose loss per *second* is least, not its loss per lap. On clean that is a
    few microseconds and counts for nothing; on bucket-brigade at long TIME the
    clock puts both Butterworths at 800 Hz, about 0.7 ms a lap, and the old
    figure was a few milliseconds short (measured on AURORA 2026-10-01: 9.004 s
    against 9.000 at FEEDBACK 60 % and TIME 1000 ms); on tape the head bump puts
    the loop's peak at 63 Hz, where the two high-passes delay a lap by about a
    millisecond.

    The response is the **analog prototype** of each stage at its working
    corner, in closed form. That is what makes the figure a function of the
    parameters alone -- the sample rate is not one -- and it errs the safe way
    where it errs: below a few kHz the digital filters' magnitudes and delays
    sit within a fraction of a percent of these, and above that, where the
    analog low-passes over-state the loss, they also state the least delay, so
    it is never the slowest frequency. */
struct LapResponse
{
    double magnitude = 1.0;   ///< |H(jw)| of the whole lap
    double delay     = 0.0;   ///< its group delay, in seconds
};

namespace tail_detail
{
    inline constexpr double kTwoPi = 2.0 * kPiD;

    inline void onePoleLowPass (LapResponse& r, double w, double hz) noexcept
    {
        const auto c = kTwoPi * hz;
        r.magnitude *= c / std::sqrt (w * w + c * c);
        r.delay     += c / (w * w + c * c);
    }

    inline void onePoleHighPass (LapResponse& r, double w, double hz) noexcept
    {
        const auto c = kTwoPi * hz;
        r.magnitude *= w / std::sqrt (w * w + c * c);
        r.delay     += c / (w * w + c * c);
    }

    /** `gain . LP + HP` with the corner at the pole -- `TptOnePole::lowShelf`
        -- which is `(s + G wc) / (s + wc)`. */
    inline void lowShelf (LapResponse& r, double w, double hz, double gain) noexcept
    {
        const auto c = kTwoPi * hz, z = gain * c;
        r.magnitude *= std::sqrt ((w * w + z * z) / (w * w + c * c));
        r.delay     += c / (w * w + c * c) - z / (w * w + z * z);
    }

    /** Second-order Butterworth low-pass, `wc^2 / (s^2 + sqrt(2) wc s + wc^2)`. */
    inline void butterworthLowPass (LapResponse& r, double w, double hz) noexcept
    {
        const auto c = kTwoPi * hz, w2 = w * w, c2 = c * c;
        r.magnitude *= 1.0 / std::sqrt (1.0 + (w2 / c2) * (w2 / c2));
        r.delay     += std::sqrt (2.0) * c * (c2 + w2) / (c2 * c2 + w2 * w2);
    }
}

/** One lap at angular frequency `w`: LOW CUT, HIGH CUT and the 10 Hz blocker,
    then the character's own mode filters, with `DelayEngine`'s constants --
    the same chain `DelayEngine::character` runs, in the same order. The
    compander is unity by construction and the interpolator is a pure delay,
    already counted in TIME; the shaper and the clip can only lose. */
inline LapResponse lapResponseAt (double w, int character, double timeSeconds,
                                  double lowCutHz, double highCutHz) noexcept
{
    using E = DelayEngine;
    using namespace tail_detail;

    LapResponse r;
    onePoleHighPass (r, w, std::clamp (lowCutHz, 20.0, 1000.0));
    onePoleLowPass  (r, w, std::clamp (highCutHz, 1000.0, 18000.0));
    onePoleHighPass (r, w, 10.0);

    if (character == kTape)
    {
        onePoleLowPass (r, w, E::kTapeLowPassHz);
        lowShelf (r, w, E::kHeadBumpHz, std::pow (10.0, E::kHeadBumpDb / 20.0));
    }
    else if (character == kBucketBrigade)
    {
        const auto clockHz = E::kBbdStages / (2.0 * std::max (timeSeconds, 0.001));
        const auto cornerHz = std::clamp (E::kBbdCutoffFactor * clockHz * 0.5,
                                          E::kBbdCutoffMinHz, E::kBbdCutoffMaxHz);
        butterworthLowPass (r, w, cornerHz);
        butterworthLowPass (r, w, cornerHz);
    }

    return r;
}

/** The lowest sample rate the suite runs at. A delay counted in samples is
    longest in seconds there, so it is the rate a bound in seconds is taken
    at. */
inline constexpr double kLowestSampleRate = 44100.0;

/** **What an in-loop FX stage adds to every lap, in seconds, as a bound**
    (10 §11a). The stage's magnitude never adds to a lap -- every type is
    non-expanding -- but two of them delay it, and the delay compounds per
    repeat like the filters'.

    - **Diffuse: the six allpasses' peak group delays, summed.** A Schroeder
      allpass of length `D` and coefficient `a` delays by
      `D (1 - a^2) / (1 - 2a cos wD + a^2)`, at most `D (1 + a) / (1 - a)`;
      the six peaks line up wherever every length divides the period, and at
      AMOUNT 100 the lengths are whole milliseconds, so every multiple of
      1 kHz takes the full sum -- 0.71 s a lap at AMOUNT 100, against the 0.13 s
      the lengths add up to. Each length is counted half a sample long, for
      its rounding. **Conservative by design** (2026-10-01; Frosty to confirm):
      measured on AURORA over every character, FEEDBACK 35-96.9 % and TIME
      1-2000 ms at 48 kHz, the real decay ran between 8 % and 88 % of this
      figure, 27 % of it on average at AMOUNT 100 and 53 % at AMOUNT 35. A
      figure built from the slowest frequency there is cannot be beaten by a
      render; a figure fitted to the renders could be, at a frequency the grid
      did not try.
    - **Crush: the sample-and-hold**, which holds a sample for up to
      `divisor - 1` more, at the lowest rate.
    - **Pan/Tremolo** is a memoryless gain and adds nothing. */
inline double fxLapDelaySeconds (bool on, int type, float amountPercent) noexcept
{
    if (! on)
        return 0.0;

    const auto amount = std::clamp ((double) amountPercent * 0.01, 0.0, 1.0);

    if (type == kDiffuse)
    {
        if (amount <= 0.0)
            return 0.0;

        const auto a = FxStage::kDiffuseCoefficient * amount;
        auto sum = 0.0;

        for (const auto ms : FxStage::kStageDelaysMs)
            sum += (ms * 0.001 * amount + 0.5 / kLowestSampleRate) * (1.0 + a) / (1.0 - a);

        return sum;
    }

    if (type == kCrush)
        return (std::ceil (1.0 + amount * FxStage::kCrushHoldSpan) - 1.0) / kLowestSampleRate;

    return 0.0;
}

/** One engine's tail: at each frequency, the laps its loop gain needs to fall
    from the build-up a held input can leave there to 60 dB under that input
    (`lapsToSixtyDb`), times the lap that frequency actually takes; the
    longest wins.

    - **The loss is the reference chain's** -- the cuts on their rails, as
      §3 defines `P_c` -- and the gain law's `g` is the loop's gain at that
      chain's peak, so the peak frequency decays at exactly `g` a lap and
      every other one faster. The user's cuts are given **no credit** for the
      loss they add: modelled in analog they over-state it, and a figure that
      took that credit came out short when measured (a 1 kHz LOW CUT at
      FEEDBACK 96.9 % rang 24 % longer than such a figure).
    - **The delay is the larger of the two chains'**, the cuts as set and as
      on their rails, because a cut adds delay even where it adds no loss.
    - `extraDelay` is what an in-loop FX adds to a lap; see
      `fxLapDelaySeconds`. **It is charged to every lap but the first**: the
      engine's output is the raw read, tapped before the loop's chain, so the
      first repeat is the input TIME late and has never been through the FX
      stage. Charging it too over-stated the figure by one whole FX delay --
      0.71 s at Diffuse 100 (fixed 2026-10-01; "keep it safe", Frosty: the
      per-lap bound itself is unchanged).

    Swept over 512 points, 10 Hz to 20 kHz, log-spaced; no allocation. */
inline double engineTailSeconds (double seconds, double g, int character,
                                 double lowCutHz, double highCutHz, double extraDelay) noexcept
{
    if (g >= 1.0)
        return kTailCeilingSeconds;

    if (g <= 0.0)
        return seconds;

    constexpr int kPoints = 512;
    const auto omegaAt = [] (int i)
    {
        return tail_detail::kTwoPi * 10.0 * std::pow (2000.0, (double) i / (double) (kPoints - 1));
    };

    auto peak = 0.0;

    for (int i = 0; i < kPoints; ++i)
        peak = std::max (peak, lapResponseAt (omegaAt (i), character, seconds, 20.0, 18000.0).magnitude);

    auto worst = 0.0;

    for (int i = 0; i < kPoints; ++i)
    {
        const auto w = omegaAt (i);
        const auto rails = lapResponseAt (w, character, seconds, 20.0, 18000.0);
        const auto asSet = lapResponseAt (w, character, seconds, lowCutHz, highCutHz);
        const auto laps  = lapsToSixtyDb (g * rails.magnitude / std::max (peak, 1.0e-12));

        worst = std::max (worst, laps * (seconds + std::max (rails.delay, asSet.delay))
                                     + (laps - 1.0) * extraDelay);
    }

    return worst;
}

/** How long Dwell rings on after its input stops, in seconds, for the
    parameter values `v`: the time from the last input sample to the last
    output above -60 dB of that input -- **whatever the input was**, a short
    burst or a held note that has built the loop up to `1 / (1 - g)` of itself
    (from 2026-10-01; before, the count started at the first repeat and a held
    note rang up to 11 % past it).

    **The larger of the two engines'** (§11.6), each from `engineTailSeconds`.
    The main delay's runs at FEEDBACK's loop gain. The lane's counts only while
    HOLD is on: a THROW decays like the main delay, at its own time and its
    tail's gain; a FREEZE or a BUILD never decays and reports the ceiling.
    Clamped to [0.5 s, 30 s]; a loop at or past unity reports 30.

    **The figure is never shorter than the measured decay, up to the 30 s
    ceiling** (`11` §4j), and `DwellDspTests::testTheReportedTailIsNeverShorter
    ThanTheDecay` renders it to hold it there. The one thing it does not
    cover is a loop past 30 s, which rings past the ceiling by decision.

    **Bucket-brigade at a fractional-sample delay is not an exception.** Until
    the fourth round (2026-10-01) a one-sample impulse there rang up to 34 %
    past the figure, and it was written down here as the tail's exception.
    It was not the tail: the expander divided one interpolated ring by the
    other, an impulse on the first sample after `prepare` landed on the
    gain ring's step from 1.0 to a companded gain, and the read came out as a
    single sample of up to 9.9e5 that the loop then rang down from. With the
    read expanded tap by tap (`DelayEngine::readExpanded`), the 14,952-row
    never-shorter grid has no short row under the ceiling, and the same
    impulse given a silent
    second first was inside the figure before the fix as well (measured on
    AURORA; `testing-notes/dwell-review-fixes-2026-10-01.md`).

    Crush is no longer an exception: with its hold holding each block's mean
    (2026-10-01) no in-loop effect holds a level under unity FEEDBACK, and
    `DwellDspTests::testEveryInLoopEffectLosesEnergyUnderUnity` holds every
    FX type to that.

    **With SYNC on, each engine's time is taken at the ring's full 2 s.** A
    tail comes from parameters alone, and the tempo is not a parameter; the
    adapter's held tempo lives on the audio thread and a host may ask for the
    tail from any thread. 2 s is the longest any division can play once
    halved to fit, so the figure is conservative, never short. */
inline double tailSecondsFor (const float* v, int count) noexcept
{
    if (v == nullptr || count < Index::count)
        return 0.0;

    const auto synced = kSyncIsEnabled && v[Index::sync] > 0.5f;
    const auto mainT = (synced ? (double) kMaxTimeMs : (double) v[Index::time]) / 1000.0;
    const auto laneT = (synced ? (double) kMaxTimeMs : (double) v[Index::laneTime]) / 1000.0;

    const auto character = (int) v[Index::character];
    const auto lowCut = (double) v[Index::lowCut], highCut = (double) v[Index::highCut];

    const auto mainFx = fxLapDelaySeconds (v[Index::fx] > 0.5f, (int) v[Index::fxType], v[Index::fxAmount]);

    auto tail = engineTailSeconds (mainT, (double) feedbackGainFor (v[Index::feedback], 1.0),
                                   character, lowCut, highCut, mainFx);

    if (v[Index::hold] > 0.5f)
    {
        // `fx_link` hands the lane the main's trio or its own, exactly as
        // `DspCore::applyParams` does.
        const auto linked = v[Index::fxLink] > 0.5f;
        const auto laneFx = linked ? mainFx
                                   : fxLapDelaySeconds (v[Index::laneFx] > 0.5f, (int) v[Index::laneFxType],
                                                        v[Index::laneFxAmount]);

        // The detent is a literal: lane_gain at 0 is FREEZE (laneGainFor).
        const auto laneTail = v[Index::laneGain] >= 0.0f
                                ? kTailCeilingSeconds
                                : engineTailSeconds (laneT, (double) laneGainFor (v[Index::laneGain], 1.0),
                                                     character, lowCut, highCut, laneFx);
        tail = std::max (tail, laneTail);
    }

    return std::clamp (tail, kTailFloorSeconds, kTailCeilingSeconds);
}

} // namespace bmo::dwell
