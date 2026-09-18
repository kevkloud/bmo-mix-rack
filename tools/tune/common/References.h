#pragma once

/*
    Other tuners, measured: the numbers BMO Tune RT is held to. Each one is a
    render of the reference stimulus (tools/common/Stimulus.h) through that
    tuner, scored by bmo-tune-ref -- the same code that scores BMO -- and each
    says where, how, and with what settings, so it can be measured again.

    How they were rendered: bmo-tune-hostrender, which loads the VST3 the way
    a host does and applies NO delay compensation, at 48 kHz in blocks of
    128, sample 0 out against sample 0 in. What a plugin reports to the host
    is recorded beside what it does: neither of these reports what it does.

    Re-measure when a reference's version changes, and say so in the commit.
    The renders and tables: testing-notes/latency-and-lag-2026-09-11.md.
*/

#include <array>

namespace bmo::tune::references
{

/** One tuner's delay while correcting, at one note. The latency rule compares
    BMO with Waves, and what it compares is pitch-dependent for both of them:
    Waves' delay tracks the period (about 1.73 x T, with almost no fixed
    floor), BMO's rest does not. So a single worst-case scalar taken at one
    note says nothing about any other, and comparing across notes gets the
    answer wrong in both directions -- it hid BMO being later than Waves at
    A4 and A5, and it invented a violation at the bottom of the range where
    BMO is comfortably under. Hold the rule to this curve, not to a number.
    testing-notes/tune-latency-review-2026-09-11.md. */
struct CorrectingDelay { double hz, ms; };

struct Reference
{
    const char* who;
    const char* version;
    const char* settings;
    const char* measured;          ///< where and when
    double reportedLatencyMs;      ///< what it tells the host
    double trueLatencyMs;          ///< Stimulus score: worst delay over the whole stimulus
    double meanLagMs;              ///< Stimulus score: correction lag, mean over the vibratos
    double worstLagMs;             ///< and the worst of them
    double meanRmsCents;           ///< Stimulus score: RMS of out - target over the vibratos, mean

    /** Delay while correcting, per marked segment, lowest note first. */
    std::array<CorrectingDelay, 6> correcting;
};

inline constexpr Reference kAntares {
    "Antares Auto-Tune Artist", "VST3 dated 2024-10-15, as installed on AURORA",
    "Input Type Low Male, Key C, Scale Chromatic, Retune Speed 0, Humanize 0, Natural Vibrato 0, "
    "Flex-Tune 0, Tracking 50 (default)",
    "AURORA, 2026-09-11",
    2.33, 10.74, -0.24, 1.66, 1.30,
    { { { 82.41, 10.736 }, { 110.0, 8.058 }, { 146.83, 5.920 },
        { 220.0, 5.487 }, { 440.0, 3.587 }, { 880.0, 2.956 } } } };

// Speed and Note Transition bottom out at 0.1 ms: set to 0, they read 0.1.
inline constexpr Reference kWaves {
    "Waves Tune Real-Time (Mono)", "16.0.23.24",
    "Speed 0.1 ms, Note Transition 0.1 ms (their minimum), Correction 100 %, Scale Chromatic, "
    "Vibrato off, everything else default",
    "AURORA, 2026-09-11",
    0.0, 19.21, 1.32, 2.13, 1.73,
    { { { 82.41, 19.215 }, { 110.0, 13.804 }, { 146.83, 10.090 },
        { 220.0, 7.048 }, { 440.0, 3.821 }, { 880.0, 0.709 } } } };

/** The latency rule's ceiling at `hz`: Waves' measured delay while
    correcting, read off its curve.

    Interpolated in the PERIOD, which is what it is nearly linear in: 19.215
    ms at E2 down to 0.709 at A5 is 1.68 ms per ms of period, with an
    intercept of -1.2. Held flat outside that span at both ends -- above A5
    because extrapolating goes negative, below E2 because **E2 is the bottom
    of what this is a tuner for** (Frosty, 2026-09-11: "it's a vocal tuner so
    no need to drop below E2"), so nothing below it is measured and nothing
    below it is judged.

    Two things that leaves open, neither of them this file's to settle:

      - Bass and Instrument declare a 55 Hz floor (params.h, Frosty's
        2026-09-10 call), which is two and a half tones below E2. Cells down
        there are held to E2's ceiling, which is generous rather than
        measured. Either the ranges come up to E2 or the curve goes down to
        A1; until then those cells are not really judged.
      - bmo-tune-latency's own figures go soft below E2 anyway. It holds a
        note 35 cents sharp for 0.6 s, and at A1 the read needs about 0.9 s
        to drift a whole period, so the sweep reports ~15 ms where the window
        actually allows rest + T = 22.2 ms. The low cells understate.

    Waves being almost purely proportional to the period, and BMO's rest being
    a constant, is the whole of the latency disagreement between them: BMO is
    under Waves below C3 and over it above, by 3.9 ms at A5. */
inline double ceilingMsAt (double hz) noexcept
{
    const auto& c = kWaves.correcting;
    const auto t = 1000.0 / hz;
    const auto periodOf = [] (double f) { return 1000.0 / f; };

    if (hz >= c.back().hz)
        return c.back().ms;

    if (hz <= c.front().hz)
        return c.front().ms;       // below E2: out of scope, not extrapolated

    const auto at = [&] (size_t lo, size_t hi)
    {
        const auto t0 = periodOf (c[lo].hz), t1 = periodOf (c[hi].hz);
        return c[lo].ms + (t - t0) / (t1 - t0) * (c[hi].ms - c[lo].ms);
    };

    for (size_t i = 1; i < c.size(); ++i)
        if (hz <= c[i].hz)
            return at (i - 1, i);

    return c.back().ms;
}

} // namespace bmo::tune::references
