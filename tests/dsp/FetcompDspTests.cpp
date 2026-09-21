/*
    Tests for BMO FET's DSP core. No JUCE, no host.

    **The core is still the placeholder** -- see modules/fetcomp/dsp/DspCore.h
    -- so this file asserts the frame rather than the compressor: the
    position-to-time law, which is the permanent definition of what ATTACK and
    RELEASE mean; the latency table and the fact that the reported figure is
    the delay actually measured; the MIX blend and its null; and that the
    placeholder really is inert where it claims to be.

    Everything here should still pass once the cell lands. What the real suite
    adds -- the GR curve to 30 dB, the implicit solve, timing and first-sample
    overshoot, programme-dependent release, THD/IMD, LF ripple, the alias
    floor, pinned-GR stability, the voicing pair, stereo link, the dry path's
    comb check at every factor -- is set out in
    docs/1176-comp/11-integration-and-test-plan.md 3, in both voicings.
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

std::vector<float> sine (double hz, double seconds, double amplitude)
{
    const auto n = (size_t) (seconds * kSampleRate);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / kSampleRate));

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

DspCore prepared (const DspCore::Params& p)
{
    DspCore core;
    core.setParams (p);
    core.prepare (kSampleRate, 512, 2);
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
    }

    // Off the detents as well: the law is continuous, because the parameter is.
    check (attackMicrosecondsFor (4.5f) < attackMicrosecondsFor (4.0f),
           "the attack law is continuous between detents");

    // And clamped at both rails, so a value from a corrupt session cannot
    // produce a coefficient from nowhere.
    checkNear (attackMicrosecondsFor (-3.0f), 800.0, 0.1, "attack clamps at the slow rail");
    checkNear (attackMicrosecondsFor (99.0f),  20.0, 0.1, "attack clamps at the fast rail");
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

        for (const auto pair : { std::pair<float, int> { 0.0f, 0 },
                                 std::pair<float, int> { 1.0f, 40 },
                                 std::pair<float, int> { 2.0f, 60 } })
        {
            values[oversampling] = pair.first;
            check (dsp.latencyForParams (values.data(), (int) values.size()) == pair.second,
                   "reported latency at detent " + std::to_string ((int) pair.first));
        }
    }

    // The delay is real, not merely declared: an impulse comes out where the
    // reported figure says it will. This is what testDryPathIsDelayMatched
    // grows into once the wet path carries the oversampler's own round trip.
    for (const auto factor : { 1, 2, 4 })
    {
        DspCore::Params p;
        p.oversampling = factor;
        auto core = prepared (p);

        std::vector<float> impulse (512, 0.0f);
        impulse[0] = 1.0f;

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

        const auto tone = sine (1000.0, 0.2, 0.5);
        run (core, tone);

        p.oversampling = 4;
        core.setParams (p);

        const auto out = run (core, tone);

        check (std::isfinite (out.back()), "the core stays finite across a factor change");
    }
}

/** The placeholder's whole job: INPUT gain, OUTPUT gain, the MIX blend, and
    nothing else. These are the properties that have to survive the real cell
    arriving, so they are worth pinning now. */
void testGainAndMix()
{
    const auto tone = sine (1000.0, 0.5, 0.25);

    // A defaults instance is a wire: unity in, unity out, zero delay.
    {
        DspCore::Params p;
        auto core = prepared (p);
        const auto out = run (core, tone);

        check (peakDifference (out, tone, 0) < 1.0e-6,
               "a defaults instance passes audio through untouched");
    }

    // OUTPUT is exact. Measured after the smoother has settled, which is what
    // the offset into the buffer is for.
    {
        DspCore::Params p;
        p.outputDb = -6.0f;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto expected = std::pow (10.0, -6.0 / 20.0);
        const auto settled = (size_t) (kSampleRate * 0.2);

        auto peakIn = 0.0, peakOut = 0.0;

        for (size_t i = settled; i < tone.size(); ++i)
        {
            peakIn  = std::max (peakIn,  (double) std::abs (tone[i]));
            peakOut = std::max (peakOut, (double) std::abs (out[i]));
        }

        checkNear (peakOut / peakIn, expected, 0.001, "OUTPUT delivers exactly what it says");
    }

    // MIX at 0 nulls against the input, which is also how bypass is proven.
    // At Off there is no delay to compensate, so this is a straight null.
    {
        DspCore::Params p;
        p.inputDb   = 18.0f;
        p.mixPercent = 0.0f;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        check (peakDifference (out, tone, settled) < 1.0e-6, "MIX at 0 nulls against the input");
    }

    // And it is a blend, not a switch: half way sits between the two.
    {
        DspCore::Params p;
        p.inputDb    = 6.0f;
        p.mixPercent = 50.0f;
        auto core = prepared (p);
        const auto out = run (core, tone);

        const auto settled = (size_t) (kSampleRate * 0.2);
        const auto wet = std::pow (10.0, 6.0 / 20.0);
        const auto expected = 0.5 * wet + 0.5;

        auto peakIn = 0.0, peakOut = 0.0;

        for (size_t i = settled; i < tone.size(); ++i)
        {
            peakIn  = std::max (peakIn,  (double) std::abs (tone[i]));
            peakOut = std::max (peakOut, (double) std::abs (out[i]));
        }

        checkNear (peakOut / peakIn, expected, 0.005, "MIX at 50 is halfway between dry and wet");
    }
}

/** Block size cannot change the answer, and nothing may produce a NaN.

    Cheap now and load-bearing later: 11 section 3's invariance suite asks for
    identical output to -120 dB across block sizes 1 / 32 / 64 / 512 / 1023,
    and a core that only works at 512 is a core nobody notices is broken until
    a host picks another number. */
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

        check (peakDifference (out, expected, 0) < 1.0e-6,
               "block size " + std::to_string (block) + " gives the same output as 512");
    }

    for (const auto v : expected)
        if (! std::isfinite (v))
        {
            check (false, "the core produced a non-finite sample");
            break;
        }

    // Silence in, exact zeros out.
    {
        auto core = prepared (p);
        const std::vector<float> silence (4096, 0.0f);
        const auto out = run (core, silence);

        auto worst = 0.0f;
        for (const auto v : out) worst = std::max (worst, std::abs (v));

        check (worst == 0.0f, "silence in is exact zeros out");
    }
}

/** The placeholder reports no reduction, and says so here rather than leaving
    a zero to be discovered. When the cell lands this becomes 11 section 3's
    "GR metering past the pin": drive to 25, 30 and 40 dB, assert the needle
    fraction clamps at 1.0 while this keeps reporting the true figure. */
void testGainReductionIsNotImplementedYet()
{
    DspCore::Params p;
    p.inputDb = 40.0f;
    auto core = prepared (p);

    run (core, sine (1000.0, 0.3, 0.5));

    checkNear (core.currentGainReductionDb(), 0.0, 1.0e-9,
               "the placeholder core reports no gain reduction");
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

} // namespace

int main()
{
    testPositionLaw();
    testLatency();
    testGainAndMix();
    testInvariance();
    testGainReductionIsNotImplementedYet();
    testAdapterUnpacksInIndexOrder();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
