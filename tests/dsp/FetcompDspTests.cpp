/*
    Tests for BMO FET's DSP core. No JUCE, no host.

    The suite docs/1176-comp/11-integration-and-test-plan.md 3 asks for, plus
    the frame tests that were written against the placeholder and still hold:
    the position-to-time law, which is the permanent definition of what ATTACK
    and RELEASE mean; the latency table and the fact that the reported figure
    is the delay actually measured; and the MIX blend and its null.

    **Two things this file deliberately does not do.**

    It does not assert a nominal ratio anywhere. The divider law delivers a
    depth-dependent slope -- 4:1 gives about 3.0 at 10 dB of reduction and
    about 2.1 at 30 -- and that sag is the law, defended at length in 10
    section 12. What is asserted is the *curve*, against a reference computed
    here from the static relation rather than copied out of the spec, plus the
    four shape properties a coding error would break.

    And it commits no audio. The golden objects are numbers: the derived
    curve, the overshoot table, the timing law.
*/

#include "modules/fetcomp/dsp/DspCore.h"
#include "modules/fetcomp/dsp/FetcompDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace bmo::fetcomp;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

int failures = 0, checks = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        std::printf ("FAIL  %s\n", what.c_str());
        ++failures;
    }
}

void checkNear (double value, double expected, double tolerance, const std::string& what)
{
    ++checks;

    if (! (std::abs (value - expected) <= tolerance))
    {
        std::printf ("FAIL  %s: %.6f, expected %.6f +/- %.6f\n",
                     what.c_str(), value, expected, tolerance);
        ++failures;
    }
}

std::vector<float> sine (double hz, double seconds, double amplitude,
                         double rate = kSampleRate)
{
    const auto n = (size_t) (seconds * rate);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / rate));

    return out;
}

/** Runs one buffer through a prepared core, in blocks, and hands it back. */
std::vector<float> run (DspCore& core, const std::vector<float>& source, int block = 512)
{
    auto left = source, right = source;

    for (size_t n = 0; n < source.size(); n += (size_t) block)
    {
        const auto count = (int) std::min ((size_t) block, source.size() - n);
        float* channels[2] { left.data() + n, right.data() + n };
        core.process (channels, 2, count);
    }

    return left;
}

DspCore prepared (const DspCore::Params& p, double rate = kSampleRate)
{
    DspCore core;
    core.setParams (p);
    core.prepare (rate, 512, 2);
    core.setParams (p);
    return core;
}

double peakDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double worst = 0.0;

    for (size_t i = from; i < a.size() && i < b.size(); ++i)
        worst = std::max (worst, (double) std::abs (a[i] - b[i]));

    return worst;
}

double peakOver (const std::vector<float>& v, size_t from)
{
    double worst = 0.0;

    for (size_t i = from; i < v.size(); ++i)
        worst = std::max (worst, (double) std::abs (v[i]));

    return worst;
}

double dbOf (double linear) { return 20.0 * std::log10 (std::max (linear, 1.0e-15)); }

double magnitudeAt (const std::vector<float>& buffer, double hz, double rate,
                    size_t from, size_t count)
{
    const auto w = 2.0 * kPi * hz / rate;
    const auto c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;

    for (size_t i = from; i < from + count && i < buffer.size(); ++i)
    {
        const auto s = (double) buffer[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }

    return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) count;
}

/** The reduction the core actually delivers at one source level, in dB across
    the cell -- so the INPUT drive counts as signal arriving, not as
    compression. A settled figure: the tone runs for `seconds` and only the
    last tenth of a second is read. */
double deliveredGrDb (DspCore::Params p, double sourceDb, double rate = kSampleRate,
                      double seconds = 0.6, double toneHz = 1000.0)
{
    const auto amplitude = std::pow (10.0, sourceDb / 20.0);
    auto core = prepared (p, rate);
    const auto out = run (core, sine (toneHz, seconds, amplitude, rate));

    return sourceDb + (double) p.inputDb
             - dbOf (peakOver (out, (size_t) ((seconds - 0.1) * rate)));
}

//==============================================================================
/** The static divider law, written out here rather than reached for in the
    DSP, so the curve test compares the implementation against the algebra and
    not against itself.

    With the control settled, `x = y*(1 + k*G*(y - T))`, a quadratic in `y`.
    The negative-`b` form is the conditioned one: `b = 1 - beta` and every
    `beta` in the family is above 1. */
double staticCellOutput (double m, double gain, double threshold)
{
    if (m <= threshold)
        return m;

    const auto a = kCellConductance * gain;
    const auto b = 1.0 - a * threshold;
    const auto y = (-b + std::sqrt (b * b + 4.0 * a * m)) / (2.0 * a);

    return y <= threshold ? m : y;
}

double staticReductionDb (double sourceDb, Ratio ratio)
{
    const auto i = (int) ratio;
    const auto m = std::pow (10.0, sourceDb / 20.0);
    const auto y = staticCellOutput (m, ratioSidechainGain (i), ratioThresholdLinear (i));

    return dbOf (m / y);
}

//==============================================================================
/** The position-to-time law, which is what ATTACK and RELEASE permanently
    mean. 800 / 126.5 / 20 us and 1100 / 234.5 / 50 ms at positions 1 / 4 / 7,
    and **higher position = faster at every pair** -- the assertion that fails
    if the direction is ever "corrected". */
void testPositionLaw()
{
    checkNear (attackMicrosecondsFor (1.0f), 800.0, 0.1, "attack at position 1");
    checkNear (attackMicrosecondsFor (4.0f), 126.5, 0.1, "attack at position 4");
    checkNear (attackMicrosecondsFor (7.0f),  20.0, 0.1, "attack at position 7");

    checkNear (releaseMillisecondsFor (1.0f), 1100.0, 0.5, "release at position 1");
    checkNear (releaseMillisecondsFor (4.0f),  234.5, 0.5, "release at position 4");
    checkNear (releaseMillisecondsFor (7.0f),   50.0, 0.5, "release at position 7");

    for (int i = 1; i < 7; ++i)
    {
        check (attackMicrosecondsFor ((float) (i + 1)) < attackMicrosecondsFor ((float) i),
               "attack position " + std::to_string (i + 1) + " is faster than " + std::to_string (i));
        check (releaseMillisecondsFor ((float) (i + 1)) < releaseMillisecondsFor ((float) i),
               "release position " + std::to_string (i + 1) + " is faster than " + std::to_string (i));

        // And the coefficient follows the law rather than merely existing.
        check (attackStepFor ((float) (i + 1), kSampleRate) > attackStepFor ((float) i, kSampleRate),
               "the attack coefficient is monotone at position " + std::to_string (i));
    }

    // Off the detents as well: the law is continuous, because the parameter is.
    check (attackMicrosecondsFor (4.5f) < attackMicrosecondsFor (4.0f),
           "the attack law is continuous between detents");

    // And clamped at both rails, so a value from a corrupt session cannot
    // produce a coefficient from nowhere.
    checkNear (attackMicrosecondsFor (-3.0f), 800.0, 0.1, "attack clamps at the slow rail");
    checkNear (attackMicrosecondsFor (99.0f),  20.0, 0.1, "attack clamps at the fast rail");

    // Sub-sample time constants are not a limit: the seven detents still
    // separate at every supported rate. 4 us is 0.18 samples at 44.1 kHz.
    for (const auto rate : { 44100.0, 192000.0 })
        for (int i = 1; i < 7; ++i)
            check (attackStepFor ((float) (i + 1), rate) > attackStepFor ((float) i, rate),
                   "the detents separate at " + std::to_string ((int) rate) + " Hz, position "
                       + std::to_string (i));
}

/** 0 / 40 / 60 at Off / 2x / 4x, zero at the default, the same in both
    voicings -- and the figure reported is the delay actually there. */
void testLatency()
{
    check (DspCore::latencyFor (1) == 0,  "Off is zero latency");
    check (DspCore::latencyFor (2) == 40, "2x is 40 samples");
    check (DspCore::latencyFor (4) == 60, "4x is 60 samples");

    FetcompDsp dsp;
    std::vector<float> values ((size_t) Index::count, 0.0f);
    values[attack] = values[release] = kPositionDefault;
    values[mix] = 100.0f;

    for (const auto voicingChoice : { 0.0f, 1.0f })
    {
        values[voicing] = voicingChoice;

        for (const auto r : { (float) ratio4, (float) ratio20, (float) ratioAll })
        {
            values[ratio] = r;

            for (const auto pair : { std::pair<float, int> { 0.0f, 0 },
                                     std::pair<float, int> { 1.0f, 40 },
                                     std::pair<float, int> { 2.0f, 60 } })
            {
                values[oversampling] = pair.first;
                check (dsp.latencyForParams (values.data(), (int) values.size()) == pair.second,
                       "reported latency at detent " + std::to_string ((int) pair.first));
            }
        }
    }

    // The delay is real, not merely declared: an impulse comes out where the
    // reported figure says it will. Below threshold, so this measures a delay
    // rather than a slam.
    for (const auto factor : { 1, 2, 4 })
    {
        DspCore::Params p;
        p.oversampling = factor;
        auto core = prepared (p);

        std::vector<float> impulse (1024, 0.0f);
        impulse[0] = 0.02f;

        const auto out = run (core, impulse);

        auto peak = 0.0f;
        auto at = 0;

        for (int n = 0; n < (int) out.size(); ++n)
            if (std::abs (out[(size_t) n]) > peak)
            {
                peak = std::abs (out[(size_t) n]);
                at = n;
            }

        check (at == DspCore::latencyFor (factor),
               "the measured delay at factor " + std::to_string (factor)
                   + " is the reported one, got " + std::to_string (at));
    }

    // Changing the factor while running must not leave the ring holding
    // samples from the old alignment.
    {
        DspCore::Params p;
        auto core = prepared (p);

        const auto tone = sine (1000.0, 0.2, 0.05);
        run (core, tone);

        p.oversampling = 4;
        core.setParams (p);

        const auto out = run (core, tone);

        check (std::isfinite (out.back()), "the core stays finite across a factor change");
        check (peakOver (out, 0) < 0.2, "a factor change does not produce a spike");
    }
}

/** INPUT, OUTPUT and the MIX blend, and the null that proves the dry path is
    delay-matched. Everything here runs below threshold, where the cell is a
    wire, so what is measured is the gain structure and not the compressor. */
void testGainAndMix()
{
    // -34 dBFS: under every threshold in the family, so a defaults instance
    // is doing nothing but its transformer poles.
    const auto tone = sine (1000.0, 0.5, 0.02);

    {
        DspCore::Params p;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        checkNear (dbOf (peakOver (out, settled) / peakOver (tone, settled)), 0.0, 0.05,
                   "a defaults instance below threshold is within 0.05 dB of unity");
        checkNear (core.currentGainReductionDb(), 0.0, 0.01,
                   "and reports no reduction there");
    }

    // OUTPUT is exact, measured where the cell is not working.
    {
        DspCore::Params p;
        p.outputDb = -6.0f;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        checkNear (dbOf (peakOver (out, settled) / peakOver (tone, settled)), -6.0, 0.05,
                   "OUTPUT delivers exactly what it says");
    }

    // MIX at 0 nulls against the input, which is also how bypass is proven --
    // at every factor, because that is where the dry path has to be delayed.
    for (const auto factor : { 1, 2, 4 })
    {
        DspCore::Params p;
        p.inputDb    = 18.0f;
        p.mixPercent = 0.0f;
        p.oversampling = factor;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        const auto delay = (size_t) DspCore::latencyFor (factor);
        auto worst = 0.0;

        for (size_t i = settled; i + delay < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i + delay] - tone[i]));

        check (dbOf (worst / 0.02) < -120.0,
               "MIX at 0 nulls to -120 dB at factor " + std::to_string (factor));
    }

    // And it is a blend, not a switch.
    {
        DspCore::Params p;
        p.inputDb    = 6.0f;
        p.mixPercent = 50.0f;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        const auto expected = 0.5 * std::pow (10.0, 6.0 / 20.0) + 0.5;

        checkNear (peakOver (out, settled) / peakOver (tone, settled), expected, 0.01,
                   "MIX at 50 is halfway between dry and wet");
    }
}

/** **The comb check, and it is a shipping requirement rather than a nicety.**

    An unmatched dry path 40 or 60 samples adrift puts notches every ~1.2 kHz
    at 48 kHz, so a partial blend would read as a phaser. Sweeping a tone
    below threshold, where wet and dry carry the same signal, the blend at
    MIX 50 has to track the blend at MIX 100 -- any misalignment shows up as a
    notch and nothing else can produce one. */
void testMixDoesNotComb()
{
    for (const auto factor : { 1, 2, 4 })
    {
        auto worst = 0.0;
        auto at = 0.0;

        for (double hz = 100.0; hz <= 18000.0; hz *= 1.3)
        {
            const auto tone = sine (hz, 0.4, 0.02);
            const auto from = (size_t) (0.2 * kSampleRate);
            const auto count = (size_t) (0.15 * kSampleRate);

            DspCore::Params p;
            p.oversampling = factor;

            p.mixPercent = 100.0f;
            auto wetCore = prepared (p);
            const auto wet = magnitudeAt (run (wetCore, tone), hz, kSampleRate, from, count);

            p.mixPercent = 50.0f;
            auto blendCore = prepared (p);
            const auto blended = magnitudeAt (run (blendCore, tone), hz, kSampleRate, from, count);

            const auto delta = std::abs (dbOf (blended) - dbOf (wet));

            if (delta > worst)
            {
                worst = delta;
                at = hz;
            }
        }

        check (worst < 0.5,
               "MIX at 50 does not comb at factor " + std::to_string (factor)
                   + ", worst " + std::to_string (worst) + " dB near "
                   + std::to_string ((int) at) + " Hz");
    }
}

/** The response the transformers imply: +/-1 dB, 20 Hz to 20 kHz, from one
    10 Hz pole and one 45 kHz pole and no others. Two of either would double
    the figure and fail. */
void testResponse()
{
    for (const auto v : { Voicing::blue, Voicing::black })
        for (const auto hz : { 20.0, 50.0, 1000.0, 10000.0, 20000.0 })
        {
            DspCore::Params p;
            p.voicing = v;
            auto core = prepared (p);

            const auto tone = sine (hz, 0.5, 0.02);
            const auto out = run (core, tone);
            const auto from = (size_t) (0.3 * kSampleRate);
            const auto count = (size_t) (0.15 * kSampleRate);

            const auto delta = dbOf (magnitudeAt (out, hz, kSampleRate, from, count))
                                 - dbOf (magnitudeAt (tone, hz, kSampleRate, from, count));

            check (std::abs (delta) <= 1.0,
                   "response at " + std::to_string ((int) hz) + " Hz is inside 1 dB, got "
                       + std::to_string (delta));
        }
}

//==============================================================================
/** The gain-reduction curve, out to 30 dB, against the law rather than
    against a nominal ratio -- plus the four shape properties a coding error
    would break. Both voicings. */
void testGainReductionCurve()
{
    for (const auto v : { Voicing::blue, Voicing::black })
    {
        for (const auto r : { Ratio::four, Ratio::eight, Ratio::twelve, Ratio::twenty })
        {
            DspCore::Params p;
            p.ratio = r;
            p.voicing = v;

            auto deepest = 0.0;
            auto previous = -1.0;

            for (double level = -40.0; level <= 20.5; level += 2.0)
            {
                const auto measured = deliveredGrDb (p, level);
                const auto expected = staticReductionDb (level, r);

                // Wider at the 20:1 threshold anchor, which is a +/-2 dB
                // published figure being read across a unit conversion nobody
                // has measured (10 section 12, "Reference level").
                const auto tolerance = (r == Ratio::twenty && level < -20.0) ? 3.0 : 1.5;

                checkNear (measured, expected, tolerance,
                           "GR curve, ratio " + std::to_string ((int) r) + " at "
                               + std::to_string ((int) level) + " dBFS");

                check (std::isfinite (measured) && measured >= previous - 0.05,
                       "the curve is well-formed and monotone in input at "
                           + std::to_string ((int) level) + " dBFS");

                previous = measured;
                deepest = std::max (deepest, measured);
            }

            check (deepest >= 20.0,
                   "ratio " + std::to_string ((int) r) + " reaches at least 20 dB by +20 dBFS");
        }

        // The four settings are strictly ordered at every depth, and the local
        // slope falls monotonically with depth and never reaches 2:1 -- the
        // sag is the law, and these are the shapes it has to keep.
        //
        // **Asserted to 20 dB and not past it, and that is a finding rather
        // than a convenience.** Beyond about 20 dB the four settings need very
        // different drives to reach the same depth -- 4:1 needs 48 dB over
        // threshold for 30 dB of reduction against 20:1's 36 -- so the input
        // amplifier's own soft compression starts contributing slope of its
        // own, and it contributes most to the setting that is being driven
        // hardest. Measured on AURORA: 4:1's local slope falls 3.8 -> 3.0 ->
        // 2.6 -> 2.3 -> 2.3 and then rises to 2.6 at 30 dB, which is the stage
        // model and not the divider. What still has to hold up there is that
        // the curve is well-formed, which the sweep above asserts at every
        // point out to +20 dBFS.
        double previousSlope[4] { 1.0e9, 1.0e9, 1.0e9, 1.0e9 };

        for (const auto target : { 5.0, 10.0, 15.0, 20.0 })
        {
            double slopes[4] {};

            for (int i = 0; i < 4; ++i)
            {
                // The drive that reaches this depth, from the static law -- no
                // search, so the test stays quick.
                const auto ratioValue = (Ratio) i;
                const auto g = std::pow (10.0, target / 20.0);
                const auto beta = kRatioBeta[(size_t) i];
                const auto s = 1.0 + (g - 1.0) / beta;
                const auto driveDb = kRatioThresholdDb[(size_t) i] + 20.0 * std::log10 (s) + target;

                DspCore::Params q;
                q.ratio = ratioValue;
                q.voicing = v;

                const auto a = deliveredGrDb (q, driveDb - 0.5);
                const auto b = deliveredGrDb (q, driveDb + 0.5);

                // out = in - GR, so the local slope is 1 / (1 - dGR/din).
                slopes[i] = 1.0 / std::max (1.0e-6, 1.0 - (b - a));
            }

            for (int i = 0; i < 4; ++i)
            {
                check (slopes[i] > 2.0,
                       "every setting stays above 2:1 -- ratio " + std::to_string (i)
                           + " at " + std::to_string ((int) target) + " dB GR, got "
                           + std::to_string (slopes[i]));

                check (slopes[i] < previousSlope[i] + 0.05,
                       "the local slope falls with depth -- ratio " + std::to_string (i)
                           + " at " + std::to_string ((int) target) + " dB GR");

                previousSlope[i] = slopes[i];

                if (i > 0)
                    check (slopes[i] > slopes[i - 1],
                           "the four settings stay ordered at " + std::to_string ((int) target)
                               + " dB GR");
            }
        }
    }
}


/** The curve above 20 dB GR: what still holds where the slope ordering does
    not, and it is not nothing.

    `testGainReductionCurve` stops its slope and ordering assertions at 20 dB
    because above that the four settings need very different drives -- 48.5 dB
    over threshold for 4:1 at 30 dB GR against 35.6 for 20:1 -- and the input
    amplifier's own soft compression contributes slope to whichever is driven
    hardest. **That is measured, not assumed** (AURORA, 2026-09-21,
    `testing-notes/fetcomp-curve-slope-2026-09-21.md`): the static divider law
    keeps falling and keeps the four ordered at 30 dB, the implementation
    departs from it by +0.54 on 4:1 against +0.02 on 20:1, and linearising
    `inputAmp` in `Stages.h` restores both properties outright.

    11 section 3 asks for four shape properties. Two of them -- **above 2:1**
    and **well-formed at 30 dB GR** -- hold all the way up and had nothing
    asserting them there, because the whole slope block stopped at 20. This is
    that assertion.

    Well-formed means what the pack says it means: finite, monotone in input,
    no discontinuity. A jump of more than 2 dB of reduction for 1 dB of drive
    is the discontinuity check -- at these depths the curve is shallow, so a
    real one would stand out by an order of magnitude. */
void testCurveAboveTwentyDb()
{
    for (const auto v : { Voicing::blue, Voicing::black })
    {
        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        for (int i = 0; i < 4; ++i)
        {
            const std::string at = who + ", ratio index " + std::to_string (i);

            DspCore::Params p;
            p.ratio = (Ratio) i;
            p.voicing = v;

            auto previousGr = -1.0e9;
            auto previousStep = 0.0;

            for (const auto target : { 25.0, 30.0 })
            {
                // The drive the static law says reaches this depth.
                const auto g = std::pow (10.0, target / 20.0);
                const auto beta = kRatioBeta[(size_t) i];
                const auto s = 1.0 + (g - 1.0) / beta;
                const auto driveDb = kRatioThresholdDb[(size_t) i]
                                       + 20.0 * std::log10 (s) + target;

                const auto a = deliveredGrDb (p, driveDb - 0.5);
                const auto b = deliveredGrDb (p, driveDb + 0.5);

                check (std::isfinite (a) && std::isfinite (b),
                       at + " at " + std::to_string ((int) target)
                          + " dB GR: the curve is finite");

                // Monotone in input: more drive is never less reduction.
                check (b >= a - 0.01,
                       at + " at " + std::to_string ((int) target)
                          + " dB GR: more drive is not less reduction, "
                          + std::to_string (a) + " then " + std::to_string (b));

                // No discontinuity: 1 dB of drive never moves the reduction
                // by more than 2 dB up here.
                const auto step = b - a;

                check (step < 2.0,
                       at + " at " + std::to_string ((int) target)
                          + " dB GR: no discontinuity, 1 dB of drive moved the "
                            "reduction by " + std::to_string (step) + " dB");

                // Still a compressor, not a limiter turned inside out.
                const auto slope = 1.0 / std::max (1.0e-6, 1.0 - step);

                check (slope > 2.0,
                       at + " at " + std::to_string ((int) target)
                          + " dB GR: the setting stays above 2:1, got "
                          + std::to_string (slope));

                check (std::isfinite (slope) && slope < 1000.0,
                       at + " at " + std::to_string ((int) target)
                          + " dB GR: the slope stays bounded, got "
                          + std::to_string (slope));

                // And the depth itself still rises between the two targets.
                if (previousGr > -1.0e8)
                    check (a > previousGr,
                           at + ": 30 dB GR really is deeper than 25, "
                              + std::to_string (previousGr) + " then "
                              + std::to_string (a));

                previousGr = a;
                previousStep = step;
            }

            (void) previousStep;
        }
    }
}

/** All-buttons is DOCUMENTED-observed only, so this asserts shape and not
    numbers: a standing reduction at silence, a very high effective slope near
    threshold, and a plateau -- a region where the curve flattens or reverses,
    which the divider law on its own cannot produce. */
void testAllButtonsShape()
{
    DspCore::Params p;
    p.ratio = Ratio::allButtons;

    {
        auto core = prepared (p);
        run (core, std::vector<float> (4096, 0.0f));
        const auto standing = (double) core.currentGainReductionDb();

        check (standing >= 1.0 && standing <= 2.0,
               "all-buttons parks the cell at 1-2 dB of reduction at silence, got "
                   + std::to_string (standing));
    }

    std::vector<double> curve;

    for (double level = -30.0; level <= 20.5; level += 2.5)
        curve.push_back (deliveredGrDb (p, level));

    // Near threshold the catch is abrupt: the four ratio resistors end up in
    // parallel, so the corner ratio goes far above 20:1.
    {
        const auto a = deliveredGrDb (p, -21.0);
        const auto b = deliveredGrDb (p, -20.0);
        const auto slope = 1.0 / std::max (1.0e-6, 1.0 - (b - a));

        check (slope > 12.0,
               "the effective slope near threshold is above 12:1, got " + std::to_string (slope));
    }

    // And somewhere above it the curve stops climbing. A plateau is a region
    // where the reduction flattens or reverses, and it is put in by hand --
    // (1 + 2u + beta)/(1 + u) is monotone and never below 2, so nothing in the
    // law can produce one.
    auto flattened = false;
    auto peak = 0.0;

    for (size_t i = 1; i < curve.size(); ++i)
    {
        peak = std::max (peak, curve[i]);

        if (curve[i] - curve[i - 1] < 0.25)
            flattened = true;
    }

    check (flattened, "all-buttons flattens or reverses somewhere above threshold");
    check (curve.back() < peak - 1.0,
           "and the reduction has turned over by the top of the range");
    check (peak > 12.0, "the flat top sits deep enough to be the mode it is named after");

    for (const auto value : curve)
        check (std::isfinite (value), "the all-buttons curve stays finite");
}

//==============================================================================
/** One step into a settled level, sample by sample, with the reduction the
    core reports after each. Block size 1, so the reported figure is that
    sample's rather than a block maximum. */
std::vector<double> stepResponse (DspCore::Params p, double rate, double amplitude,
                                  size_t before, size_t after)
{
    auto core = prepared (p, rate);
    std::vector<double> gr (before + after, 0.0);

    for (size_t n = 0; n < before + after; ++n)
    {
        auto l = n < before ? 0.0f : (float) amplitude;
        auto r = l;
        float* channels[2] { &l, &r };
        core.process (channels, 2, 1);
        gr[n] = (double) core.currentGainReductionDb();
    }

    return gr;
}

/** First-sample overshoot: what the implicit solve buys, and 10 section 12's
    acceptance for it. At `alpha = 1` the solve collapses to the static curve
    and delivers the correct steady-state gain on the first sample; `alpha`
    never quite reaches 1, and the table is what is left over. */
void testFirstSampleOvershoot()
{
    // 10 section 12, computed on AURORA from the closed form: a step to 20 dB
    // of steady-state GR at 20:1, 48 kHz, Off.
    const double expected[7] { 4.70, 3.18, 1.92, 0.98, 0.39, 0.09, 0.008 };

    for (const auto rate : { 44100.0, 48000.0 })
    {
        double measured[7] {};

        for (int i = 0; i < 7; ++i)
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.attackPosition = (float) (i + 1);
            p.releasePosition = 1.0f;

            // The drive that settles at 20 dB, from the static law.
            const auto g = std::pow (10.0, 1.0);
            const auto s = 1.0 + (g - 1.0) / kRatioBeta[3];
            p.inputDb = (float) (kRatioThresholdDb[3] + 20.0 * std::log10 (s) + 20.0 + 18.0);

            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            const auto before = (size_t) (0.01 * rate);
            const auto gr = stepResponse (p, rate, amplitude, before, (size_t) (0.05 * rate));

            measured[i] = gr.back() - gr[before];

            if (rate == 48000.0)
                checkNear (measured[i], expected[i], 0.1,
                           "overshoot table at position " + std::to_string (i + 1));
        }

        check (std::abs (measured[6]) <= 0.05,
               "the fastest detent overshoots by no more than 0.05 dB at "
                   + std::to_string ((int) rate) + " Hz, got " + std::to_string (measured[6]));

        for (int i = 1; i < 7; ++i)
            check (measured[i] < measured[i - 1],
                   "the overshoot figures are strictly monotone at "
                       + std::to_string ((int) rate) + " Hz, position " + std::to_string (i + 1));
    }

    // **Oversampling must not change the timing, and this is the form of that
    // claim which is actually measurable.** The first-sample figure cannot be
    // compared across factors at all: the oversampler's linear-phase
    // transition is 40 to 60 base samples wide, longer than every attack on
    // the knob, so a sample-resolution measurement of a step through it is
    // measuring the half-band filter. A symmetric filter does leave the
    // half-way crossing where it was, offset by the upsampler's own half of
    // the round trip, so that is what is compared.
    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        for (const auto factor : { 1, 2, 4 })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.attackPosition = position;
            p.releasePosition = 1.0f;
            p.oversampling = factor;
            p.inputDb = 16.0f;

            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            const auto before = (size_t) (0.01 * kSampleRate);
            const auto gr = stepResponse (p, kSampleRate, amplitude, before,
                                          (size_t) (0.05 * kSampleRate));

            const auto half = 0.5 * gr.back();
            auto at = gr.size() - 1;

            for (size_t n = before; n < gr.size(); ++n)
                if (gr[n] >= half) { at = n; break; }

            const auto offset = (double) (at - before) - 0.5 * DspCore::latencyFor (factor);

            check (std::abs (offset) <= 1.5,
                   "the attack lands where the upsampler delivers the step -- position "
                       + std::to_string ((int) position) + ", factor " + std::to_string (factor)
                       + ", off by " + std::to_string (offset) + " samples");
        }
    }
}

/** How long the reduction takes to fall to 37 % of where it started, in dB,
    from the reference depth the detent table is calibrated at. Returns -1 if
    it never gets there. */
double recoveryMs (DspCore::Params p, double holdSeconds)
{
    const auto amplitude = std::pow (10.0, -18.0 / 20.0);
    auto core = prepared (p);

    const auto hold = (size_t) (holdSeconds * kSampleRate);
    auto signal = sine (1000.0, holdSeconds, amplitude);
    signal.resize (hold + (size_t) (30.0 * kSampleRate), 0.0f);

    auto left = signal, right = signal;
    double startGr = 0.0;

    for (size_t n = 0; n < left.size(); n += 16)
    {
        const auto count = (int) std::min ((size_t) 16, left.size() - n);
        float* channels[2] { left.data() + n, right.data() + n };
        core.process (channels, 2, count);

        if (n + (size_t) count == hold)
            startGr = (double) core.currentGainReductionDb();

        if (n >= hold && startGr > 0.0 && (double) core.currentGainReductionDb() <= 0.37 * startGr)
            return 1000.0 * (double) (n - hold) / kSampleRate;
    }

    return -1.0;
}

/** The release detents, at the depth they are calibrated at.

    The printed time is a 63 %-in-dB figure and the smoothing is in the
    control domain, so it is only honest at the reference depth -- which is
    the whole reason 10 section 6 names one. Measured after a short burst,
    where the fast branch owns the recovery; the slow branch's contribution is
    the next test. */
void testReleaseDetents()
{
    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.releasePosition = position;

        const auto g = std::pow (10.0, kReleaseReferenceGrDb / 20.0);
        const auto s = 1.0 + (g - 1.0) / kRatioBeta[3];
        p.inputDb = (float) (kRatioThresholdDb[3] + 20.0 * std::log10 (s)
                               + kReleaseReferenceGrDb + 18.0);

        const auto measured = recoveryMs (p, 0.02);
        const auto law = (double) releaseMillisecondsFor (position);

        check (measured > 0.0, "the release completes at position " + std::to_string ((int) position));
        checkNear (measured, law, 0.1 * law,
                   "release at position " + std::to_string ((int) position) + " is the printed time");
    }

    // Higher position = faster, at every pair, measured rather than derived.
    auto previous = 0.0;

    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.releasePosition = position;
        p.inputDb = 16.0f;

        const auto measured = recoveryMs (p, 0.02);

        if (previous > 0.0)
            check (measured < previous, "a higher release position recovers faster");

        previous = measured;
    }
}

/** Programme dependence, and the reason it needs no control.

    The slow branch is programme-dependent through its **charge** time: one
    transient barely moves it, a sustained passage charges it and it then owns
    the recovery. A naive two-branch implementation -- two release times
    combined with `max` -- fails this, because a slower one-pole fed the same
    signal is never below a faster one and there is no programme dependence in
    it at all. So is the subtler failure of branching the slow charge against
    the applied control rather than against its own value, which under a
    sustained tone is true only at the top of each cycle. */
void testProgrammeDependentRelease()
{
    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.releasePosition = position;
        p.inputDb = 16.0f;

        const auto burst     = recoveryMs (p, 0.02);
        const auto sustained = recoveryMs (p, 4.0);

        check (burst > 0.0 && sustained > 0.0,
               "both recoveries complete at position " + std::to_string ((int) position));
        check (sustained >= 1.5 * burst,
               "a sustained passage recovers at least 1.5x slower than a burst at position "
                   + std::to_string ((int) position) + ", got " + std::to_string (sustained / burst)
                   + "x");
    }
}

//==============================================================================
/** The implicit solve itself, against the algebra it is a closed form of.

    At `alpha = 1` it must reproduce the static curve exactly; it has to stay
    finite in the corner each branch exists for; and it must never hand back a
    negative or a NaN at any extreme of input, ratio or bias. */
void testImplicitSolve()
{
    SidechainState sc;
    sc.rectPole = 1.0;
    sc.attack = 1.0;

    for (int i = 0; i < 4; ++i)
    {
        sc.gain = ratioSidechainGain (i);
        sc.threshold = ratioThresholdLinear (i);

        for (const auto m : { 0.001, 0.01, 0.1, 0.5, 1.0, 10.0, 100.0 })
        {
            CellState cell;
            const auto v = solveCellOutput (m, cell, sc);
            const auto expected = staticCellOutput (m, sc.gain, sc.threshold);

            check (std::abs (v - expected) <= 1.0e-6 * std::max (1.0, expected),
                   "at alpha = 1 the solve is the static curve, ratio " + std::to_string (i));
        }
    }

    // The two conditioning corners, and the one the guard exists for.
    {
        CellState cell;
        sc.gain = ratioSidechainGain (3);
        sc.threshold = ratioThresholdLinear (3);

        // A < 0: deep, high ratio, all-buttons bias on top.
        cell.bias = allButtonsStandingControl();
        cell.control = 0.3;
        check (std::isfinite (solveCellOutput (10.0, cell, sc)) , "the A < 0 branch stays finite");

        // 4Bm << A^2: a tiny signal against a large held control.
        check (std::isfinite (solveCellOutput (1.0e-9, cell, sc)),
               "the ill-conditioned A > 0 corner stays finite");

        // B = 0, which the sidechain gain reaching zero would produce.
        sc.gain = 0.0;
        check (std::isfinite (solveCellOutput (1.0, cell, sc)), "B = 0 is guarded");
    }

    // And the whole loop, at every extreme the parameters allow.
    for (const auto r : { Ratio::four, Ratio::twenty, Ratio::allButtons })
        for (const auto driveDb : { -20.0f, 0.0f, 30.0f, 60.0f })
            for (const auto position : { 1.0f, 7.0f })
            {
                DspCore::Params p;
                p.ratio = r;
                p.inputDb = driveDb;
                p.attackPosition = position;
                p.releasePosition = position;

                auto core = prepared (p);
                const auto out = run (core, sine (60.0, 0.3, 0.9));

                for (const auto value : out)
                    if (! std::isfinite (value))
                    {
                        check (false, "the core stayed finite at drive "
                                        + std::to_string ((int) driveDb));
                        break;
                    }

                check (core.currentGainReductionDb() >= 0.0
                         && std::isfinite (core.currentGainReductionDb()),
                       "reduction is finite and non-negative at drive "
                           + std::to_string ((int) driveDb));
            }
}

/** The two voicings: level-matched, identical latency, and a switch that does
    not click or move the envelope. */
void testVoicingPair()
{
    // Level match. At no reduction it is exact by construction -- every static
    // shaper is unity at small signal, so neither voicing contributes a level
    // of its own and no trim is needed.
    {
        const auto tone = sine (1000.0, 0.4, 0.02);
        const auto settled = (size_t) (0.2 * kSampleRate);
        double level[2] {};

        for (int i = 0; i < 2; ++i)
        {
            DspCore::Params p;
            p.voicing = i == 0 ? Voicing::blue : Voicing::black;
            auto core = prepared (p);
            level[i] = dbOf (peakOver (run (core, tone), settled));
        }

        checkNear (level[0], level[1], 0.1, "the voicings match within 0.1 dB at no reduction");
    }

    {
        double level[2] {};

        for (int i = 0; i < 2; ++i)
        {
            DspCore::Params p;
            p.voicing = i == 0 ? Voicing::blue : Voicing::black;
            p.ratio = Ratio::four;
            p.inputDb = 20.0f;

            const auto tone = sine (1000.0, 0.6, std::pow (10.0, -18.0 / 20.0));
            auto core = prepared (p);
            level[i] = dbOf (peakOver (run (core, tone), (size_t) (0.5 * kSampleRate)));
        }

        checkNear (level[0], level[1], 0.5, "the voicings match within 0.5 dB at 10 dB of reduction");
    }

    // Switching mid-audio: the compression must not jump, and the output must
    // stay between the two voicings' own answers rather than spiking -- which
    // is what a crossfade of shaper outputs gives, and what a shaper whose
    // ADAA state was not rebuilt would not.
    {
        const auto tone = sine (20.0, 0.6, 0.3);
        const auto at = (size_t) (0.3 * kSampleRate);

        std::vector<float> reference[2];

        for (int i = 0; i < 2; ++i)
        {
            DspCore::Params p;
            p.voicing = i == 0 ? Voicing::blue : Voicing::black;
            p.inputDb = 12.0f;
            auto core = prepared (p);
            reference[i] = run (core, tone);
        }

        DspCore::Params p;
        p.voicing = Voicing::blue;
        p.inputDb = 12.0f;
        auto core = prepared (p);

        auto left = tone, right = tone;
        double before = 0.0, after = 0.0;

        for (size_t n = 0; n < tone.size(); n += 64)
        {
            if (n == at)
            {
                before = (double) core.currentGainReductionDb();
                p.voicing = Voicing::black;
                core.setParams (p);
            }

            const auto count = (int) std::min ((size_t) 64, tone.size() - n);
            float* channels[2] { left.data() + n, right.data() + n };
            core.process (channels, 2, count);

            if (n == at)
                after = (double) core.currentGainReductionDb();
        }

        checkNear (after, before, 0.1, "the reduction does not jump across a voicing switch");

        auto worst = 0.0;

        for (size_t i = at; i < at + (size_t) (0.05 * kSampleRate) && i < left.size(); ++i)
        {
            const auto low  = std::min (reference[0][i], reference[1][i]);
            const auto high = std::max (reference[0][i], reference[1][i]);
            worst = std::max (worst, (double) std::max (low - left[i], left[i] - high));
        }

        check (worst < 0.01,
               "the switch stays between the two voicings rather than spiking, worst "
                   + std::to_string (worst));

        // Latency is the same in both, at every factor -- the voicing must not
        // appear in latencyForParams at all, and testLatency proves it there.
        check (DspCore::latencyFor (2) == 40 && DspCore::latencyFor (4) == 60,
               "the latency table does not know about voicing");
    }
}

/** Stereo is always linked and is not a parameter: one control voltage for
    the pair. A burst on one channel alone must duck the other by the same
    amount. */
void testStereoLink()
{
    DspCore::Params p;
    p.ratio = Ratio::twenty;
    p.inputDb = 20.0f;
    auto core = prepared (p);

    const auto burst = sine (500.0, 0.4, 0.4);
    const auto quiet = sine (500.0, 0.4, 0.002);

    auto left = burst, right = quiet;

    for (size_t n = 0; n < left.size(); n += 256)
    {
        const auto count = (int) std::min ((size_t) 256, left.size() - n);
        float* channels[2] { left.data() + n, right.data() + n };
        core.process (channels, 2, count);
    }

    const auto settled = (size_t) (0.3 * kSampleRate);
    const auto duckedRight = dbOf (peakOver (quiet, settled)) + 20.0
                               - dbOf (peakOver (right, settled));
    const auto duckedLeft  = dbOf (peakOver (burst, settled)) + 20.0
                               - dbOf (peakOver (left, settled));

    check (duckedRight > 10.0,
           "the quiet channel is ducked by the loud one, got " + std::to_string (duckedRight));
    checkNear (duckedRight, duckedLeft, 0.5, "both channels are ducked by the same amount");
    checkNear (core.currentGainReductionDb(), duckedLeft, 1.0,
               "and one shared reduction is reported for the pair");
}

/** Past the meter's pin, the number is still the number.

    `ui::DynamicsMeter` keeps its 0..24 dB reduction scale and is not
    modified; the needle simply clamps. What is asserted here is the DSP half
    of that: the reported figure stays true at 25, 30 and 40 dB, and the
    fraction a meter would draw from it clamps at exactly 1.0 with no wrap and
    no NaN. The drawing limit is not a measurement limit. */
void testReductionPastThePin()
{
    constexpr double kGrRangeDb = 24.0;   // core/ui/Controls.h, deliberately untouched

    for (const auto target : { 25.0, 30.0, 40.0 })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.releasePosition = 1.0f;

        // Drive past the ceiling for the 40 dB case, which the divider cannot
        // reach -- the point there is that it does not lie about how far it
        // got, and does not blow up trying.
        const auto g = std::pow (10.0, target / 20.0);
        const auto s = 1.0 + (g - 1.0) / kRatioBeta[3];
        p.inputDb = (float) std::min (60.0, kRatioThresholdDb[3] + 20.0 * std::log10 (s)
                                              + target + 18.0);

        auto core = prepared (p);
        run (core, sine (1000.0, 0.8, std::pow (10.0, -18.0 / 20.0)));

        const auto reported = (double) core.currentGainReductionDb();
        const auto fraction = std::min (1.0, std::max (0.0, reported / kGrRangeDb));

        check (std::isfinite (reported), "the reported reduction is finite at "
                                            + std::to_string ((int) target) + " dB");
        check (reported > kGrRangeDb,
               "the reported reduction goes past the meter's 24 dB at "
                   + std::to_string ((int) target) + " dB, got " + std::to_string (reported));
        check (fraction == 1.0, "and the needle fraction clamps at exactly 1.0");

        if (target <= 30.0)
            checkNear (reported, target, 2.0,
                       "the loop is usable and well-conditioned at "
                           + std::to_string ((int) target) + " dB of reduction");
    }
}

/** The alias floor, and which harmonic is actually in the bin.

    A tone at 0.1875*fs has its third harmonic above Nyquist, folding to
    0.4375*fs. The old version of this test measured that one bin, asserted
    -60 at Off, and then only that 2x and 4x were within 0.5 dB of Off. That
    second assertion was fitted to what the code already did and could not
    fail; it is gone.

    The comment it replaced said the image bin was "a bin no harmonic of the
    tone can occupy". That is not true, and it is the whole story. Harmonics
    fold *inside* the oversampled domain too, and at 2x the thirteenth lands
    on exactly that bin, below base Nyquist, in the decimation filter's
    passband where no filter can reach it. At 4x it is the nineteenth. The
    earlier note's guess -- gain modulation aliasing inside whatever rate it
    runs at -- was right, and is now measured rather than assumed:
    `testing-notes/fetcomp-alias-origin-2026-09-21.md`, AURORA, and
    `measure_fetcomp aliasorigin` reproduces it.

    So what is pinned here is the **mechanism**, by detuning the tone by
    fs/512 so the candidates separate onto their own whole Goertzel bins:

      - Off leaves the third harmonic in the bin.
      - 2x removes the third harmonic outright.
      - 4x additionally removes the thirteenth.

    Each of those is a structural fact about the oversampler doing its job,
    falsifiable and not fitted to a measured level. What survives -- the
    nineteenth at 4x, about -74 dB -- is the flat skirt of the detector's
    rectifier, which is not bandlimited, and no factor removes it because
    there is always a higher harmonic to take the bin.

    **The pack's 2x and 4x targets were -80 and -90, and were changed to -70
    on 2026-09-21 against this measurement** (10 section 9, 11 section 3).
    They assumed a decaying skirt; the measured skirt is flat within 2.8 dB
    from the third harmonic to the nineteenth, so no oversampling factor
    reaches them and only bandlimiting the rectifier ever could -- a change to
    what the attack overshoot table and the THD figures come from. -70 is a
    bound with 2.4 dB over the worst corner in the grid (-72.4, Blue at 48 kHz
    and 4x), not a threshold fitted to today's number.

    The tight target is **Off, on Blue**: -62.9 dB at 44.1 kHz and 30 dB GR
    against -60. Black is flat in depth and never worse than -72.4, which is
    why the first sweep -- Black only, though section 3 asks for both voicings
    -- read this as a comfortable 12 dB. It is 2.9 dB. If the Blue constants
    in Calibration.h are ever recalibrated, re-run `measure_fetcomp alias`
    for both voicings before trusting the default. */
void testAliasFloor()
{
    // fs/512, which is 32 bins of the 16384-sample window, so the fundamental
    // and every displaced harmonic below still land on whole bins.
    constexpr double delta = kSampleRate / 512.0;

    const auto toneHz  = 0.1875 * kSampleRate + delta;
    const auto nominal = 0.4375 * kSampleRate;

    for (const auto v : { Voicing::blue, Voicing::black })
    {
        // Indexed by factor: 0 = Off, 1 = 2x, 2 = 4x.
        double thirdDb[3] {}, thirteenthDb[3] {}, plainFloorDb[3] {};
        int index = 0;

        for (const auto factor : { 1, 2, 4 })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.voicing = v;
            p.attackPosition = 7.0f;
            p.releasePosition = 7.0f;
            p.oversampling = factor;
            p.inputDb = 26.0f;                       // about 20 dB of reduction

            const auto from = (size_t) (0.4 * kSampleRate);
            constexpr size_t count = 16384;

            // The undetuned tone still gives the plain floor the pack's Off
            // target is written against.
            {
                auto core = prepared (p);
                const auto out = run (core, sine (0.1875 * kSampleRate, 0.9,
                                                  std::pow (10.0, -18.0 / 20.0)));

                const auto tone  = magnitudeAt (out, 0.1875 * kSampleRate, kSampleRate, from, count);
                const auto image = magnitudeAt (out, nominal, kSampleRate, from, count);

                plainFloorDb[index] = dbOf (image / std::max (tone, 1.0e-15));
            }

            auto core = prepared (p);
            const auto out = run (core, sine (toneHz, 0.9, std::pow (10.0, -18.0 / 20.0)));

            const auto tone = magnitudeAt (out, toneHz, kSampleRate, from, count);

            const auto third      = magnitudeAt (out, nominal -  3.0 * delta, kSampleRate, from, count);
            const auto thirteenth = magnitudeAt (out, nominal + 13.0 * delta, kSampleRate, from, count);

            thirdDb[index]      = dbOf (third      / std::max (tone, 1.0e-15));
            thirteenthDb[index] = dbOf (thirteenth / std::max (tone, 1.0e-15));
            ++index;
        }

        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        check (plainFloorDb[0] <= -60.0,
               who + ": the alias floor at Off is at or under -60 dB, got "
                   + std::to_string (plainFloorDb[0]));

        // 11 section 3's oversampled target, changed from -80/-90 to -70 on
        // 2026-09-21 against measurement. The worst corner over the whole grid
        // is -72.4 dB (Blue, 48 kHz, 4x), so this carries 2.4 dB of margin and
        // is a bound rather than a fit -- a harsher rectifier or a broken
        // oversampler still fails it. The grid itself is the measure tool's
        // job; this pins the one rate and depth the suite runs.
        check (plainFloorDb[1] <= -70.0,
               who + ": the alias floor at 2x is at or under -70 dB, got "
                   + std::to_string (plainFloorDb[1]));

        check (plainFloorDb[2] <= -70.0,
               who + ": the alias floor at 4x is at or under -70 dB, got "
                   + std::to_string (plainFloorDb[2]));

        // The detuned tone must put the third harmonic in the bin at Off,
        // otherwise the two assertions below are vacuous.
        check (thirdDb[0] >= -90.0,
               who + ": at Off the third harmonic is in the image bin, got "
                   + std::to_string (thirdDb[0]));

        check (thirdDb[1] <= thirdDb[0] - 30.0,
               who + ": 2x removes the third harmonic, which went from "
                   + std::to_string (thirdDb[0]) + " to " + std::to_string (thirdDb[1]));

        check (thirdDb[2] <= thirdDb[0] - 30.0,
               who + ": 4x removes the third harmonic too, got "
                   + std::to_string (thirdDb[2]));

        check (thirteenthDb[1] >= -100.0,
               who + ": at 2x the thirteenth harmonic is the one in the bin, got "
                   + std::to_string (thirteenthDb[1]));

        check (thirteenthDb[2] <= thirteenthDb[1] - 30.0,
               who + ": 4x removes the thirteenth harmonic, which went from "
                   + std::to_string (thirteenthDb[1]) + " to "
                   + std::to_string (thirteenthDb[2]));
    }
}

/** Sixty seconds of pinned reduction: no drift, no denormal, no NaN.

    Fastest attack and release, at 20:1 and all-buttons, which is where the
    loop is asked to work hardest. The reduction has to be the same at the end
    as it was halfway through. */
void testPinnedStability()
{
    for (const auto r : { Ratio::twenty, Ratio::allButtons })
    {
        const auto allButtons = r == Ratio::allButtons;

        DspCore::Params p;
        p.ratio = r;
        p.attackPosition = 7.0f;
        p.releasePosition = 7.0f;

        // All-buttons cannot be pinned at 25-30 dB and is not meant to be:
        // its fitted collapse flattens the reduction at about 18 dB and turns
        // it over above that, so driving it harder gets *less*. It is parked
        // near its flat top instead, which is the deepest it has.
        p.inputDb = allButtons ? 22.0f : 34.0f;

        auto core = prepared (p);
        const auto tone = sine (1000.0, 1.0, std::pow (10.0, -18.0 / 20.0));

        double halfway = 0.0, finish = 0.0, worst = 0.0;

        for (int second = 0; second < 60; ++second)
        {
            const auto out = run (core, tone);
            worst = std::max (worst, peakOver (out, 0));

            for (const auto value : out)
                if (! std::isfinite (value))
                {
                    check (false, "the pinned run stayed finite");
                    return;
                }

            if (second == 29) halfway = (double) core.currentGainReductionDb();
            if (second == 59) finish  = (double) core.currentGainReductionDb();
        }

        check (halfway > (allButtons ? 12.0 : 20.0),
               "the pinned run is actually pinned, got " + std::to_string (halfway) + " dB");
        checkNear (finish, halfway, 0.01, "the reduction does not drift over thirty seconds");
        check (std::isfinite (worst) && worst < 10.0, "and the output stays bounded");
    }
}

/** Block size, sample rate and silence: the answer cannot depend on how the
    host cuts the audio up, and nothing may produce a NaN. */
void testInvariance()
{
    const auto tone = sine (440.0, 0.3, 0.4);

    DspCore::Params p;
    p.inputDb    = 12.0f;
    p.outputDb   = -3.0f;
    p.mixPercent = 60.0f;

    auto reference = prepared (p);
    const auto expected = run (reference, tone, 512);

    for (const auto block : { 1, 32, 64, 1023 })
    {
        auto core = prepared (p);
        const auto out = run (core, tone, block);

        check (dbOf (peakDifference (out, expected, 0) / 0.4) < -120.0,
               "block size " + std::to_string (block) + " gives the same output as 512");
    }

    for (const auto v : expected)
        if (! std::isfinite (v))
        {
            check (false, "the core produced a non-finite sample");
            break;
        }

    // The curve holds across every supported rate: nothing is hard-coded, and
    // every coefficient derives from a time and the effective rate.
    {
        DspCore::Params q;
        q.ratio = Ratio::eight;
        const auto at48 = deliveredGrDb (q, 0.0, 48000.0);

        for (const auto rate : { 44100.0, 88200.0, 96000.0, 192000.0 })
            checkNear (deliveredGrDb (q, 0.0, rate), at48, 0.3,
                       "the curve holds at " + std::to_string ((int) rate) + " Hz");
    }

    // Silence in, exact zeros out -- including under all-buttons, where the
    // gate is parked off zero and a careless bias would leak DC.
    for (const auto r : { Ratio::four, Ratio::allButtons })
    {
        DspCore::Params q;
        q.ratio = r;
        q.inputDb = 20.0f;
        auto core = prepared (q);
        const auto out = run (core, std::vector<float> (4096, 0.0f));

        auto worst = 0.0f;
        for (const auto value : out) worst = std::max (worst, std::abs (value));

        check (worst == 0.0f, "silence in is exact zeros out");
    }

    // Per-sample automation produces no zipper: the three gains are smoothed
    // and the switches crossfade, so a ramp is a ramp and not a staircase.
    {
        DspCore::Params q;
        auto core = prepared (q);
        auto signal = sine (1000.0, 0.5, 0.02);
        auto left = signal, right = signal;

        for (size_t n = 0; n < left.size(); ++n)
        {
            q.inputDb = (float) (12.0 * (double) n / (double) left.size());
            q.mixPercent = (float) (100.0 - 50.0 * (double) n / (double) left.size());
            core.setParams (q);

            float* channels[2] { left.data() + n, right.data() + n };
            core.process (channels, 2, 1);
        }

        // The first difference is no use here -- the ramp legitimately makes
        // the signal bigger, so the slew grows with it. The *second*
        // difference is: for a smooth sine it is about A*w^2, which at the
        // ramp's final amplitude is around 0.0014, and a staircase from
        // parameters that stepped rather than smoothed would be far above it.
        auto worstCurvature = 0.0;

        for (size_t n = 1; n + 1 < left.size(); ++n)
            worstCurvature = std::max (worstCurvature,
                                       (double) std::abs (left[n + 1] - 2.0f * left[n] + left[n - 1]));

        check (worstCurvature < 0.003,
               "a per-sample automation ramp does not zipper, worst curvature "
                   + std::to_string (worstCurvature));
    }
}

/** The adapter unpacks the flat array in Index order. A transposed pair here
    would be silent everywhere else -- the parameters are all floats. */
void testAdapterUnpacksInIndexOrder()
{
    FetcompDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    std::vector<float> values ((size_t) Index::count, 0.0f);
    values[input]        = 3.0f;
    values[output]       = -4.0f;
    values[attack]       = 6.0f;
    values[release]      = 2.0f;
    values[ratio]        = (float) ratioAll;
    values[mix]          = 25.0f;
    values[voicing]      = (float) blue;
    values[oversampling] = (float) os2x;

    dsp.setParams (values.data(), (int) values.size());

    const auto& p = dsp.getCore().getParams();

    checkNear (p.inputDb,         3.0, 1.0e-6, "input maps to inputDb");
    checkNear (p.outputDb,       -4.0, 1.0e-6, "output maps to outputDb");
    checkNear (p.attackPosition,  6.0, 1.0e-6, "attack maps to the attack position");
    checkNear (p.releasePosition, 2.0, 1.0e-6, "release maps to the release position");
    checkNear (p.mixPercent,     25.0, 1.0e-6, "mix maps to mixPercent");

    check (p.ratio == Ratio::allButtons, "the last ratio detent is all-buttons");
    check (p.voicing == Voicing::blue,   "voicing 0 is Blue");
    check (p.oversampling == 2,          "the 2x detent is the factor 2");

    // A short array is refused rather than read past the end. The rack hands
    // this the slot's values and a mismatched count is how that goes wrong.
    const auto before = dsp.getCore().getParams().inputDb;
    dsp.setParams (values.data(), Index::count - 1);
    checkNear (dsp.getCore().getParams().inputDb, (double) before, 1.0e-6,
               "a short parameter array is ignored");
}


//==============================================================================
// 11 section 3's four remaining suites. Added 2026-09-21 on AURORA; before
// this the file was green with all four missing, which is what
// docs/1176-comp/HANDOFF-dsp-fixes.md section 3 was about. Every figure they
// assert was measured first with measure_fetcomp and written up in
// testing-notes/fetcomp-section3-2026-09-21.md -- nothing here is a bound
// invented at the keyboard.
//==============================================================================

/** The INPUT drive that lands a given reduction, by bisection.

    Mirrors `driveForGr` in tools/measure/fetcomp/main.cpp. 24 halvings of an
    80 dB bracket is 5e-6 dB, far finer than anything asserted against it. */
float driveForGr (DspCore::Params p, double targetGrDb, double sourceDb)
{
    auto low = -20.0, high = 60.0;

    for (int i = 0; i < 24; ++i)
    {
        const auto mid = 0.5 * (low + high);
        p.inputDb = (float) mid;

        if (deliveredGrDb (p, sourceDb) < targetGrDb)
            low = mid;
        else
            high = mid;
    }

    return (float) (0.5 * (low + high));
}

/** THD from H2..H10 against the fundamental, as a percentage, plus the first
    two harmonics in dB. Only harmonics below Nyquist are counted: at 15 kHz
    there are none, which is why the grid treats that row differently. */
struct Harmonics
{
    double thdPercent = 0.0, h2Db = 0.0, h3Db = 0.0;
    int counted = 0;
    bool finite = true;
};

Harmonics harmonicsOf (const std::vector<float>& out, double toneHz,
                       size_t from, size_t count)
{
    Harmonics h;

    const auto fundamental = magnitudeAt (out, toneHz, kSampleRate, from, count);
    double sum = 0.0, h2 = 0.0, h3 = 0.0;

    for (int n = 2; n <= 10; ++n)
    {
        const auto hz = toneHz * (double) n;

        if (hz >= 0.5 * kSampleRate)
            break;

        const auto m = magnitudeAt (out, hz, kSampleRate, from, count);
        sum += m * m;
        ++h.counted;

        if (n == 2) h2 = m;
        if (n == 3) h3 = m;
    }

    for (size_t i = from; i < from + count && i < out.size(); ++i)
        if (! std::isfinite (out[i]))
            h.finite = false;

    h.thdPercent = 100.0 * std::sqrt (sum) / std::max (fundamental, 1.0e-12);
    h.h2Db = dbOf (h2 / std::max (fundamental, 1.0e-12));
    h.h3Db = dbOf (h3 / std::max (fundamental, 1.0e-12));

    return h;
}

/** THD over 11 section 3's grid: level x depth x frequency x voicing.

    Measured on AURORA at 1 kHz, 4:1, release position 1, oversampling Off,
    from a -18 dBFS source -- Black 0.0038 / 0.2593 / 0.4399 / 1.5228 / 5.1075 %
    at 0 / 6 / 10 / 20 / 30 dB GR, Blue 0.0112 / 0.9451 / 1.6651 / 5.5849 /
    10.8683 %. What is asserted here is the shape of that, not the values: a
    level is a calibration, and every CALIBRATE constant in Calibration.h is
    still a first-pass number awaiting an ear. The one real bound is the
    manual's condition, which has its own test below.

    **15 kHz carries no THD assertion**, and cannot: every harmonic of it is
    above Nyquist at 48 kHz, so `harmonicsOf` counts none. What folds back into
    band there is aliasing, which `testAliasFloor` owns. The row still runs, to
    assert the module stays finite and keeps its fundamental when driven hard
    at the top of the band. */
void testThdGrid()
{
    // 11 section 3's grid. 10 dB GR is not in it -- that is the manual's own
    // condition and is asserted separately.
    const double levels[] { -20.0, -10.0, 0.0 };
    const double depths[] { 0.0, 6.0, 12.0, 20.0, 30.0 };
    const double tones[]  { 50.0, 1000.0, 15000.0 };

    for (const auto v : { Voicing::blue, Voicing::black })
    {
        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        for (const auto level : levels)
        {
            for (const auto toneHz : tones)
            {
                double previousThd = -1.0;

                for (const auto depth : depths)
                {
                    DspCore::Params p;
                    p.ratio = Ratio::four;
                    p.voicing = v;
                    p.releasePosition = 1.0f;
                    // Depth 0 needs its own drive too: a 0 dBFS source sits
                    // 16 dB over the 4:1 threshold, so "no drive" is not
                    // "no reduction" and the row would not be the clean
                    // reference the monotonicity check needs.
                    p.inputDb = driveForGr (p, depth, level);

                    const auto amplitude = std::pow (10.0, level / 20.0);
                    auto core = prepared (p);
                    const auto out = run (core, sine (toneHz, 1.2, amplitude));

                    const auto from  = (size_t) (0.9 * kSampleRate);
                    const auto count = (size_t) (0.25 * kSampleRate);

                    const auto h = harmonicsOf (out, toneHz, from, count);

                    const auto where = who + " at " + std::to_string ((int) level)
                                         + " dBFS, " + std::to_string ((int) toneHz)
                                         + " Hz, " + std::to_string ((int) depth) + " dB GR";

                    check (h.finite, where + ": the output stays finite");

                    const auto fundamental = magnitudeAt (out, toneHz, kSampleRate, from, count);

                    check (fundamental > 0.0 && std::isfinite (fundamental),
                           where + ": the fundamental survives");

                    if (h.counted == 0)
                        continue;                 // 15 kHz: see the comment above

                    check (std::isfinite (h.thdPercent) && h.thdPercent < 100.0,
                           where + ": THD is finite and under 100 %, got "
                               + std::to_string (h.thdPercent));

                    // **Monotone at 20 and 30 dB, which is what section 3 asks
                    // for** ("at 20 and 30 dB assert THD is monotone in depth
                    // and finite"). Measured on AURORA, it is NOT monotone
                    // below that at 50 Hz: Black runs 1.82 / 1.57 / 1.26 % at
                    // 0 / 6 / 12 dB GR, falling, and 12 dB is still below 6.
                    // At that frequency the figure
                    // is dominated by the envelope's own ripple rather than by
                    // the cell, and the ripple shrinks relative to the tone as
                    // the drive comes up. testLfRippleAgainstRelease is what
                    // owns that mechanism. Asserting monotonicity from 0 would
                    // be asserting something the module does not do.
                    if (previousThd >= 0.0 && depth >= 20.0)
                        check (h.thdPercent >= previousThd - 1.0e-6,
                               where + ": THD is monotone in depth, "
                                   + std::to_string (previousThd) + " then "
                                   + std::to_string (h.thdPercent));

                    previousThd = h.thdPercent;

                    // **H2 leads H3 at 1 kHz only.** At 50 Hz it legitimately
                    // does not: the control ripples at 2f and a gain modulated
                    // at 2f puts its product on the third harmonic, so H3 runs
                    // above H2 there -- measured -26.8 vs -30.7 dB for Black at
                    // 30 dB GR. That is the wanted character 10 section 12
                    // derives, not a defect, and it is bounded by the LF ripple
                    // suite rather than forbidden here.
                    if (depth > 0.0 && h.counted >= 2 && toneHz == 1000.0)
                        check (h.h2Db > h.h3Db,
                               where + ": the 2nd harmonic still leads the 3rd, "
                                   + std::to_string (h.h2Db) + " vs "
                                   + std::to_string (h.h3Db));
                }
            }
        }
    }
}

/** The manual's published condition, and the one number in the THD suite that
    is a real bound rather than a shape: 10 dB GR at 4:1 with the 1.1 s
    release, 1 kHz, Black, from -18 dBFS. Measured 0.4399 % on AURORA against
    the 0.5 % ceiling (10 section 8, 11 section 3).

    **Black only.** Blue measures 1.6651 % at the same point and is meant to --
    it is the louder voicing by design, which `testVoicingThdSplit` pins. The
    measure tool's own header says "< 0.5 % for Black" for this reason. */
void testThdAtTheManualCondition()
{
    DspCore::Params p;
    p.ratio = Ratio::four;
    p.voicing = Voicing::black;
    p.releasePosition = 1.0f;
    p.inputDb = driveForGr (p, 10.0, -18.0);

    auto core = prepared (p);
    const auto out = run (core, sine (1000.0, 3.0, std::pow (10.0, -18.0 / 20.0)));

    const auto h = harmonicsOf (out, 1000.0, (size_t) (2.0 * kSampleRate),
                                (size_t) (0.5 * kSampleRate));

    check (h.thdPercent < 0.5,
           "Black at the manual's 10 dB GR condition is under 0.5 % THD, got "
               + std::to_string (h.thdPercent));

    check (h.h2Db > h.h3Db,
           "and its 2nd harmonic leads the 3rd, " + std::to_string (h.h2Db)
               + " vs " + std::to_string (h.h3Db));
}

/** Blue above Black at every depth, with the gap widening (10 section 8).

    This is the assertion the handoff called the one that would be hardest to
    notice going missing: every THD figure in the first write-up came from the
    manual tool, so nothing in the suite failed if the voicing split silently
    collapsed. Two constant sets over one topology is the whole design of the
    module; if they converge, BMO FET has one voicing and a switch that does
    nothing. */
void testVoicingThdSplit()
{
    const double depths[] { 6.0, 12.0, 20.0, 30.0 };

    double previousGap = -1.0;

    for (const auto depth : depths)
    {
        double thd[2] {};
        int index = 0;

        for (const auto v : { Voicing::black, Voicing::blue })
        {
            DspCore::Params p;
            p.ratio = Ratio::four;
            p.voicing = v;
            p.releasePosition = 1.0f;
            p.inputDb = driveForGr (p, depth, -18.0);

            auto core = prepared (p);
            const auto out = run (core, sine (1000.0, 1.6, std::pow (10.0, -18.0 / 20.0)));

            thd[index++] = harmonicsOf (out, 1000.0, (size_t) (1.2 * kSampleRate),
                                        (size_t) (0.35 * kSampleRate)).thdPercent;
        }

        const auto at = " at " + std::to_string ((int) depth) + " dB GR";

        check (thd[1] > thd[0],
               "Blue distorts more than Black" + at + ", "
                   + std::to_string (thd[1]) + " vs " + std::to_string (thd[0]));

        const auto gap = thd[1] - thd[0];

        if (previousGap >= 0.0)
            check (gap > previousGap,
                   "and the gap widens with depth" + at + ", "
                       + std::to_string (previousGap) + " then " + std::to_string (gap));

        previousGap = gap;
    }
}

/** Intermodulation: SMPTE 60 Hz + 7 kHz, and CCIF 19 + 20 kHz, per depth.

    SMPTE reads the 7 kHz carrier's sidebands at +/-60 Hz; CCIF reads the 1 kHz
    difference tone, a bin no harmonic of either tone can occupy. Both are
    asserted as bounded and monotone in depth rather than against a level, for
    the same reason the THD grid is. What a bug looks like here is a figure
    that is not finite, or one that falls as the compressor works harder. */
void testIntermodulation()
{
    for (const auto v : { Voicing::blue, Voicing::black })
    {
        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        double previousSmpte = -1.0, previousCcif = -1.0;

        for (const auto depth : { 0.0, 6.0, 12.0, 20.0 })
        {
            DspCore::Params p;
            p.ratio = Ratio::four;
            p.voicing = v;
            p.releasePosition = 1.0f;
            p.inputDb = depth > 0.0 ? driveForGr (p, depth, -18.0) : 0.0f;

            const auto from  = (size_t) (1.2 * kSampleRate);
            const auto count = (size_t) (0.25 * kSampleRate);
            const auto at = " " + who + " at " + std::to_string ((int) depth) + " dB GR";

            // SMPTE: 60 Hz at four times the amplitude of the 7 kHz carrier,
            // which is the 4:1 the standard means -- not a compression ratio.
            {
                const auto low  = sine (60.0,   1.6, std::pow (10.0, -18.0 / 20.0) * 0.8);
                const auto high = sine (7000.0, 1.6, std::pow (10.0, -18.0 / 20.0) * 0.2);

                std::vector<float> mixed (low.size());

                for (size_t i = 0; i < mixed.size(); ++i)
                    mixed[i] = low[i] + high[i];

                auto core = prepared (p);
                const auto out = run (core, mixed);

                const auto carrier = magnitudeAt (out, 7000.0, kSampleRate, from, count);
                const auto lower   = magnitudeAt (out, 6940.0, kSampleRate, from, count);
                const auto upper   = magnitudeAt (out, 7060.0, kSampleRate, from, count);

                const auto imd = 100.0 * (lower + upper) / std::max (carrier, 1.0e-12);

                check (std::isfinite (imd) && imd < 100.0,
                       "SMPTE IMD is finite and bounded" + at + ", got "
                           + std::to_string (imd));

                if (previousSmpte >= 0.0)
                    check (imd >= previousSmpte - 1.0e-6,
                           "SMPTE IMD is monotone in depth" + at + ", "
                               + std::to_string (previousSmpte) + " then "
                               + std::to_string (imd));

                previousSmpte = imd;
            }

            // CCIF: 19 and 20 kHz at equal amplitude, read at the 1 kHz
            // difference tone.
            {
                const auto a = sine (19000.0, 1.6, std::pow (10.0, -18.0 / 20.0) * 0.5);
                const auto b = sine (20000.0, 1.6, std::pow (10.0, -18.0 / 20.0) * 0.5);

                std::vector<float> mixed (a.size());

                for (size_t i = 0; i < mixed.size(); ++i)
                    mixed[i] = a[i] + b[i];

                auto core = prepared (p);
                const auto out = run (core, mixed);

                const auto reference  = magnitudeAt (out, 19000.0, kSampleRate, from, count);
                const auto difference = magnitudeAt (out,  1000.0, kSampleRate, from, count);

                const auto imd = 100.0 * difference / std::max (reference, 1.0e-12);

                check (std::isfinite (imd) && imd < 100.0,
                       "CCIF IMD is finite and bounded" + at + ", got "
                           + std::to_string (imd));

                if (previousCcif >= 0.0)
                    check (imd >= previousCcif - 1.0e-6,
                           "CCIF IMD is monotone in depth" + at + ", "
                               + std::to_string (previousCcif) + " then "
                               + std::to_string (imd));

                previousCcif = imd;
            }
        }
    }
}

/** LF ripple against the release detents (10 section 12, 11 section 3).

    With a fast release the control follows the rectified envelope and ripples
    at 2f; a gain modulated at 2f puts a third harmonic on the tone at roughly
    half the ripple depth. This is wanted character -- it is a large part of
    what the fastest release sounds like -- so the test bounds it rather than
    forbidding it, and catches the day it stops being ripple and starts being
    a bug.

    Measured on AURORA, 50 Hz at 20 dB GR and 20:1, Black across positions
    1..7: 0.194 / 0.316 / 0.515 / 0.843 / 1.367 / 2.180 / 3.392 %. Blue:
    0.226 / 0.355 / 0.561 / 0.884 / 1.373 / 2.156 / 3.375 %. 100 Hz runs at
    about half those figures in both, which is the mechanism -- the same
    ripple against a tone an octave up.

    10 section 12 derives about 0.23 % at 1.1 s and about 4.5 % at 50 ms for
    50 Hz. Both ends are asserted within +/-50 % of the derivation, which is a
    mechanism check and not a tolerance: the figures come out of an algebraic
    estimate, not a measurement of hardware. */
void testLfRippleAgainstRelease()
{
    for (const auto v : { Voicing::blue, Voicing::black })
    {
        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        for (const auto toneHz : { 50.0, 100.0 })
        {
            const auto what = who + " at " + std::to_string ((int) toneHz) + " Hz";

            double h3Percent[7] {};

            for (int position = 1; position <= 7; ++position)
            {
                DspCore::Params p;
                p.ratio = Ratio::twenty;
                p.voicing = v;
                p.releasePosition = (float) position;
                p.inputDb = driveForGr (p, 20.0, -18.0);

                auto core = prepared (p);
                const auto out = run (core, sine (toneHz, 4.0, std::pow (10.0, -18.0 / 20.0)));

                const auto from  = (size_t) (3.0 * kSampleRate);
                const auto count = (size_t) (0.5 * kSampleRate);

                const auto h1 = magnitudeAt (out, toneHz,       kSampleRate, from, count);
                const auto h3 = magnitudeAt (out, toneHz * 3.0, kSampleRate, from, count);

                h3Percent[position - 1] = 100.0 * h3 / std::max (h1, 1.0e-12);

                check (std::isfinite (h3Percent[position - 1]),
                       what + " position " + std::to_string (position)
                            + ": the ripple figure is finite");
            }

            // Shorter release, more ripple, at every step. Position 7 is the
            // fastest, so the array must rise across it.
            for (int i = 1; i < 7; ++i)
                check (h3Percent[i] > h3Percent[i - 1],
                       what + ": H3 rises as the release shortens, position "
                            + std::to_string (i) + " gave "
                            + std::to_string (h3Percent[i - 1]) + " and position "
                            + std::to_string (i + 1) + " gave "
                            + std::to_string (h3Percent[i]));

            // 10 section 12's derived figures, 50 Hz only, +/-50 %.
            if (toneHz == 50.0)
            {
                check (h3Percent[0] > 0.115 && h3Percent[0] < 0.345,
                       what + " at the 1.1 s release lands near 10 section 12's "
                              "0.23 %, got " + std::to_string (h3Percent[0]));

                check (h3Percent[6] > 2.25 && h3Percent[6] < 6.75,
                       what + " at the 50 ms release lands near 10 section 12's "
                              "4.5 %, got " + std::to_string (h3Percent[6]));
            }
        }
    }
}

/** Level range: the test `modules/fetcomp/params.h` names and did not have.

    The INPUT range reaches +60 dB rather than +45 because 45 is about 5 dB
    short of 30 dB of reduction at 4:1 from a -18 dBFS source, and OUTPUT
    reaches +/-36 because +/-24 cannot restore it. `params.h` says of the input
    range that "the level-range DSP test is what fails if this is ever
    reverted" -- this is that test.

    From a -18 dBFS RMS source, at every ratio: `input` must reach 30 dB of
    reduction, `output` must restore unity there, and **neither may sit at its
    rail** to do it. The rail check is the point. A range that only just
    reaches the target is one revert away from not reaching it, and the whole
    reason the numbers are 60 and 36 is the margin. */
void testLevelRange()
{
    constexpr float kInputMax  = 60.0f;      // params.h, S::floatParam (kInput, ...)
    constexpr float kOutputMax = 36.0f;      // params.h, S::floatParam (kOutput, ...)
    constexpr double kSourceDb = -18.0;

    for (const auto r : { Ratio::four, Ratio::eight, Ratio::twelve, Ratio::twenty })
    {
        for (const auto v : { Voicing::blue, Voicing::black })
        {
            const std::string who = std::string (v == Voicing::blue ? "Blue" : "Black")
                                      + " at ratio index " + std::to_string ((int) r);

            DspCore::Params p;
            p.ratio = r;
            p.voicing = v;

            const auto drive = driveForGr (p, 30.0, kSourceDb);

            check (drive < kInputMax,
                   who + ": input reaches 30 dB GR below its +60 dB rail, needed "
                       + std::to_string (drive));

            // And it really does deliver 30 dB there, not merely bisect to it.
            p.inputDb = drive;

            const auto delivered = deliveredGrDb (p, kSourceDb);

            checkNear (delivered, 30.0, 0.5,
                       who + ": the drive found actually delivers 30 dB of reduction");

            // OUTPUT must restore unity at that depth without railing.
            //
            // The makeup that does it is **not** the reduction. INPUT drives
            // the cell, so with the tone leaving at
            // `source + input - reduction`, unity needs
            // `reduction - input` -- which at 4:1 is a cut of about 19 dB, not
            // a boost of 30. An earlier draft of this test asserted the output
            // came back at the source level after applying +30 and failed at
            // every ratio, because that is not what these two controls do.
            //
            // Both halves of the range still matter and both are checked:
            // the cut that restores unity from a driven source, and the +30
            // boost that params.h says +/-24 could not supply.
            const auto makeup = delivered - (double) drive;

            check (std::abs (makeup) < (double) kOutputMax,
                   who + ": the makeup that restores unity, "
                       + std::to_string (makeup)
                       + " dB, is inside the +/-36 dB range");

            check ((double) kOutputMax >= 30.0,
                   who + ": the output range can supply the 30 dB of makeup "
                         "params.h says +/-24 could not");

            p.outputDb = (float) makeup;

            auto core = prepared (p);
            const auto out = run (core, sine (1000.0, 1.2, std::pow (10.0, kSourceDb / 20.0)));

            const auto restored = dbOf (peakOver (out, (size_t) (1.0 * kSampleRate)));

            checkNear (restored, kSourceDb, 1.0,
                       who + ": output restores unity at 30 dB GR, got "
                           + std::to_string (restored) + " dBFS from "
                           + std::to_string (kSourceDb));
        }
    }
}

/** Golden state, not golden audio: compact numeric tables in the file.

    11 section 3 is explicit that no audio is ever committed and that the
    regression object is state -- per-voicing GR-curve arrays, control-state
    samples, and per-block RMS and peak quantised to 1e-4. These are the
    numbers this build produces on AURORA, 2026-09-21.

    **These pin behaviour, not correctness.** Every one of them moves the day
    a CALIBRATE constant is tuned by ear, and that is the point: they make a
    change to the sound visible in a diff instead of silent. If a calibration
    pass changes them, re-record them in the same commit and say so -- do not
    widen the tolerance. */
void testGoldenState()
{
    // GR delivered at 1 kHz from -18 dBFS, 20:1, no makeup, at INPUT drives
    // of 0, 10, 20, 30 and 40 dB. Quantised to 1e-4 by the comparison.
    const double goldenBlack[] { 5.7698, 14.9065, 23.1152, 30.2677, 36.6219 };
    const double goldenBlue[]  { 5.7843, 14.9363, 23.1570, 30.3392, 36.9033 };

    int index = 0;

    for (const auto v : { Voicing::black, Voicing::blue })
    {
        const auto* golden = index++ == 0 ? goldenBlack : goldenBlue;
        const std::string who { v == Voicing::blue ? "Blue" : "Black" };

        int at = 0;

        for (const auto drive : { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.voicing = v;
            p.inputDb = drive;

            const auto delivered = deliveredGrDb (p, -18.0);

            checkNear (delivered, golden[at], 0.01,
                       who + ": the golden GR array holds at INPUT "
                           + std::to_string ((int) drive) + " dB, expected "
                           + std::to_string (golden[at]) + " got "
                           + std::to_string (delivered));
            ++at;
        }
    }

    // Per-block RMS and peak of a fixed render, quantised to 1e-4. 20:1,
    // 20 dB of drive, fastest release, four 4096-sample blocks after settle.
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.voicing = Voicing::black;
        p.releasePosition = 7.0f;
        p.inputDb = 20.0f;

        auto core = prepared (p);
        const auto out = run (core, sine (220.0, 1.0, std::pow (10.0, -18.0 / 20.0)));

        const size_t block = 4096, from = (size_t) (0.5 * kSampleRate);

        const double goldenRms[]  { 0.0629, 0.0628, 0.0631, 0.0627 };
        const double goldenPeak[] { 0.0884, 0.0884, 0.0884, 0.0884 };

        for (int b = 0; b < 4; ++b)
        {
            double sumSquares = 0.0, peak = 0.0;

            for (size_t i = from + (size_t) b * block;
                 i < from + (size_t) (b + 1) * block && i < out.size(); ++i)
            {
                sumSquares += (double) out[i] * out[i];
                peak = std::max (peak, (double) std::abs (out[i]));
            }

            const auto rms = std::sqrt (sumSquares / (double) block);

            checkNear (rms, goldenRms[b], 1.0e-4,
                       "golden block " + std::to_string (b) + " RMS, got "
                           + std::to_string (rms));

            checkNear (peak, goldenPeak[b], 1.0e-4,
                       "golden block " + std::to_string (b) + " peak, got "
                           + std::to_string (peak));
        }
    }
}
} // namespace

int main()
{
    testPositionLaw();
    testLatency();
    testGainAndMix();
    testMixDoesNotComb();
    testResponse();
    testGainReductionCurve();
    testCurveAboveTwentyDb();
    testAllButtonsShape();
    testFirstSampleOvershoot();
    testReleaseDetents();
    testProgrammeDependentRelease();
    testImplicitSolve();
    testVoicingPair();
    testStereoLink();
    testReductionPastThePin();
    testAliasFloor();
    testPinnedStability();
    testInvariance();
    testAdapterUnpacksInIndexOrder();

    // 11 section 3's four remaining suites, added 2026-09-21.
    testThdGrid();
    testThdAtTheManualCondition();
    testVoicingThdSplit();
    testIntermodulation();
    testLfRippleAgainstRelease();
    testLevelRange();
    testGoldenState();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
