/*
    Tests for BMO Defang's DSP core. No JUCE, no host.

    **The core is still the placeholder** -- see modules/deesser/dsp/DspCore.h
    -- so this file asserts the frame rather than the de-esser: the adapter's
    unpacking of the flat parameter array, the latency contract, the listen
    hook's lifecycle, and that the placeholder really is inert where it claims
    to be.

    Everything here should still pass once the detector and the band land. What
    the real suite adds -- detection over a 24 dB level sweep, the two absolute
    gates and the `S` clamp, level independence at three values of the internal
    blend, the static curve and its depth, the fixed timings, the slow branch
    and the hold, HF pumping, transparency, shelf mode, modulation, aliasing,
    the listen null, stereo link, rate and block invariance, robustness, and
    the meter's agreement with the applied offset -- is the table in
    docs/deesser/11-integration-and-test-plan.md 5, and the stimuli it needs
    are generated in-test from a fixed-seed LCG, never committed.
*/

#include "modules/deesser/dsp/DeesserDsp.h"
#include "modules/deesser/dsp/DspCore.h"
#include "modules/deesser/dsp/Detector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bmo::deesser;

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

/** Runs one buffer through a prepared DSP, in blocks, and hands it back. */
std::vector<float> run (bmo::ModuleDsp& dsp, const std::vector<float>& source, int block = 512)
{
    auto left = source, right = source;

    for (size_t n = 0; n < source.size(); n += (size_t) block)
    {
        const auto count = (int) std::min ((size_t) block, source.size() - n);
        float* channels[2] { left.data() + n, right.data() + n };
        dsp.process (channels, 2, count);
    }

    return left;
}

/** The default parameter array, in Index order and in real units. */
std::vector<float> defaults()
{
    std::vector<float> v;

    for (const auto& s : specs())
        v.push_back (s.def);

    return v;
}

//==============================================================================
void testPlaceholderIsAWire()
{
    // The placeholder passes audio untouched, and that is deliberate rather
    // than unfinished: a placeholder that did something would have to be
    // undone, and a listening round run against it would be a round against
    // nothing in particular. **This test is expected to be rewritten, not
    // deleted, when the DSP lands.**
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    auto v = defaults();
    dsp.setParams (v.data(), (int) v.size());

    const auto source = sine (7000.0, 0.25, 0.5);
    const auto out = run (dsp, source);

    check (out.size() == source.size(), "the placeholder returns as many samples as it was given");

    auto worst = 0.0;

    for (size_t i = 0; i < out.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - source[i]));

    checkNear (worst, 0.0, 0.0, "the placeholder is bit-identical to its input");
}

void testLatencyIsZeroEverywhere()
{
    // **The one figure here that is shipped rather than placeheld.** There is
    // no lookahead and no oversampling, by decision
    // (docs/deesser/10-dsp-spec.md 1), so latency is zero at *every* setting
    // and not merely at the default -- and latency is permanent once shipped,
    // which makes this the expensive thing to be wrong about. 11 section 5's
    // interface row asks for exactly this.
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    auto v = defaults();

    const float freqs[]   { 2000.0f, 6500.0f, 10000.0f };
    const float qs[]      { 0.7f, 2.5f, 6.0f };
    const float threshs[] { -24.0f, 0.0f, 24.0f };
    const float ranges[]  { 1.0f, 8.0f, 18.0f };
    const float shapes[]  { (float) bell, (float) highShelf };

    for (const auto f : freqs)
        for (const auto qv : qs)
            for (const auto t : threshs)
                for (const auto r : ranges)
                    for (const auto sh : shapes)
                    {
                        v[freq] = f; v[q] = qv; v[thresh] = t; v[range] = r; v[shape] = sh;
                        dsp.setParams (v.data(), (int) v.size());

                        check (dsp.latencyForParams (v.data(), (int) v.size()) == 0,
                               "latency is zero at every parameter combination");
                    }

    // And across a prepare and a reset, which is where a module that buffers
    // would give itself away.
    dsp.prepare (44100.0, 64, 2);
    check (dsp.latencyForParams (v.data(), (int) v.size()) == 0, "latency is zero after a re-prepare");
    dsp.reset();
    check (dsp.latencyForParams (v.data(), (int) v.size()) == 0, "latency is zero after a reset");

    check (DspCore::latencySamples() == 0, "the core reports zero latency");
}

void testAdapterUnpacksInIndexOrder()
{
    // The adapter is the one place a host value becomes a DSP unit, so this is
    // the test that fails if the array is ever read out of order -- which is
    // silent everywhere else, because every one of these is a float.
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    auto v = defaults();
    v[freq]   = 4321.0f;
    v[q]      = 3.75f;
    v[thresh] = -7.5f;
    v[range]  = 13.25f;
    v[shape]  = (float) highShelf;

    dsp.setParams (v.data(), (int) v.size());

    const auto& p = dsp.getCore().getParams();

    checkNear (p.freqHz,   4321.0, 1.0e-4, "freq reaches the core");
    checkNear (p.q,        3.75,   1.0e-4, "q reaches the core");
    checkNear (p.threshDb, -7.5,   1.0e-4, "thresh reaches the core");
    checkNear (p.rangeDb,  13.25,  1.0e-4, "range reaches the core");
    check (p.shape == Shape::highShelf, "shape reaches the core as High Shelf");

    v[shape] = (float) bell;
    dsp.setParams (v.data(), (int) v.size());
    check (dsp.getCore().getParams().shape == Shape::bell, "shape 0 is Bell");

    // A short array is ignored rather than read past the end. A rack slot can
    // hand over fewer values than a module has while a chain is being built.
    const auto before = dsp.getCore().getParams().freqHz;
    dsp.setParams (v.data(), Index::count - 1);
    checkNear (dsp.getCore().getParams().freqHz, (double) before, 0.0,
               "a short parameter array is refused rather than read past");
}

void testListenIsMomentary()
{
    // The listen path has no parameter behind it -- it is the `setSolo` hook,
    // set while the button is held and cleared with -1 on release. What is
    // asserted here is the lifecycle; what the path must *output* (the band's
    // contribution, `H(x) - x`, nulling to -100 dB, and -1 restoring a
    // bit-identical signal) waits on the DSP, and is 11 section 5's listen row.
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    auto v = defaults();
    dsp.setParams (v.data(), (int) v.size());

    check (! dsp.getCore().isListening(), "a fresh core is not listening");

    dsp.setSolo (0);
    check (dsp.getCore().isListening(), "soloing band 0 engages the listen path");

    dsp.setSolo (-1);
    check (! dsp.getCore().isListening(), "-1 clears the listen path");

    // Any index at or above zero, because this module has one band and a panel
    // that sent 1 would mean the same thing.
    dsp.setSolo (3);
    check (dsp.getCore().isListening(), "any non-negative index engages the listen path");
    dsp.setSolo (-1);

    // **Placeholder: audio is untouched in either state.** The real core
    // outputs the band's contribution while soloed; until it does, the only
    // honest assertion is that engaging listen does not corrupt the signal.
    const auto source = sine (7000.0, 0.1, 0.5);

    dsp.setSolo (0);
    const auto soloed = run (dsp, source);
    dsp.setSolo (-1);

    auto worst = 0.0;

    for (size_t i = 0; i < soloed.size(); ++i)
        worst = std::max (worst, (double) std::abs (soloed[i] - source[i]));

    checkNear (worst, 0.0, 0.0, "the placeholder passes audio through while listening too");
}

void testGainReductionIsZeroForNow()
{
    // Signed, positive = gain taken away, and the real core reports the **peak
    // band reduction** rather than a wideband-equivalent figure -- 10 section 8
    // has the argument, and 11 section 5's interface row asserts it against
    // the applied offset once there is one. A flat zero is all the placeholder
    // can honestly say.
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    auto v = defaults();
    v[range] = 18.0f;
    dsp.setParams (v.data(), (int) v.size());

    run (dsp, sine (7000.0, 0.1, 0.9));

    checkNear (dsp.currentGainReductionDb(), 0.0, 0.0,
               "the placeholder reports no gain reduction");
}

void testTheScheduleIsFiveParameters()
{
    // The schema is frozen at first ship and append-only after it, and the
    // absences are decisions: attack, release, mix, lookahead, oversampling, a
    // stereo-link switch and ADAPT are all out of v1 and all of them can only
    // ever be appended after `shape`. Asserted on the JUCE-free side as well
    // as in tests/plugin, because this is the file the DSP pass will have open.
    check (specs().size() == (size_t) Index::count, "the Index enum matches specs()");
    check (specs().size() == 5, "v1 ships five parameters");

    check (bmo::indexOfParam (specs(), "adapt") < 0, "there is no ADAPT parameter");
    check (bmo::indexOfParam (specs(), "mix") < 0, "there is no MIX parameter");
    check (bmo::indexOfParam (specs(), "attack") < 0, "there is no ATTACK parameter");
    check (bmo::indexOfParam (specs(), "release") < 0, "there is no RELEASE parameter");
    check (bmo::indexOfParam (specs(), "lookahead") < 0, "there is no LOOKAHEAD parameter");
    check (bmo::indexOfParam (specs(), "oversampling") < 0, "there is no oversampling parameter");
    check (bmo::indexOfParam (specs(), "link") < 0, "there is no stereo-link parameter");
    check (bmo::indexOfParam (specs(), "listen") < 0, "listen is not a parameter");

    // `shape` is last, so that anything appended later lands after it rather
    // than between two controls a session already refers to by position.
    check (Index::shape == Index::count - 1, "shape is the last parameter, so ADAPT can only append");
}

} // namespace

//==============================================================================
// The detector, tested as arithmetic rather than through the audio path.
//
// modules/deesser/dsp/Detector.h is JUCE-free and holds no state between runs,
// so these feed it levels directly instead of rendering audio and inferring
// what it must have seen. What is checked here is the part that cannot be
// heard: that prominence is level-independent, that the gates are gates, and
// that the static curve is the one 10 section 4 prints.

/** Feeds a steady band level and reference level until the envelopes settle,
    and returns the prominence. */
double settledProminence (Prominence::Config config, double bandLevel,
                          double referenceLevel, double seconds = 3.0)
{
    Prominence p;
    p.prepare (config, kSampleRate);

    double out = Prominence::kSilent;
    const auto n = (size_t) (seconds * kSampleRate);

    for (size_t i = 0; i < n; ++i)
        out = p.process (bandLevel, referenceLevel);

    return out;
}

/** The whole reason this detector shape exists: every term is the log of a
    level, so an input gain adds the same constant to B, W and S and cancels
    exactly -- at every kappa, not only at 1. A threshold that had to be
    re-ridden when the take got louder would be the ordinary kind of de-esser
    this module is deliberately not.

    24 dB of gain, which is the sweep 11 section 5 asks for. */
void testProminenceIsLevelIndependent()
{
    for (const auto kappa : { 0.0, 0.6, 1.0 })
    {
        Prominence::Config config;
        config.kappa = kappa;

        const auto quiet = settledProminence (config, 0.02, 0.2);
        const auto loud  = settledProminence (config, 0.02 * 15.85, 0.2 * 15.85);

        checkNear (loud, quiet, 1.0e-9,
                   "prominence is unchanged by 24 dB of input gain at kappa "
                       + std::to_string (kappa));
    }
}

/** Both gates hard-zero rather than clamping, and the result is
    distinguishable from a genuine prominence of 0 dB -- which is a real
    reading meaning "typical vocal balance", not "nothing here". */
void testTheGatesAreGates()
{
    Prominence::Config config;

    // A reference at about -70 dBFS is below the -55 dB gate. Without the gate
    // this is the case that reads as enormous prominence: room tone with a
    // little hiss in it, band over reference, and the module de-essing silence.
    check (settledProminence (config, 1.0e-4, 3.2e-4) <= Prominence::kSilent,
           "a reference under the gate reports silence, not a huge prominence");

    // And the band gate, which is what catches breaths.
    check (settledProminence (config, 1.0e-4, 0.2) <= Prominence::kSilent,
           "a band under the gate reports silence");

    // Neither gate engaged: a real reading, and one the caller can act on.
    check (settledProminence (config, 0.02, 0.2) > Prominence::kSilent,
           "an ordinary balance reports a real prominence");
}

/** The static curve, 10 section 4, read at the four points that define its
    shape. Absolutes, not comparisons with each other -- the house rule from
    tests/dsp/OptoDspTests.cpp, which passed for a release while both modes
    were broken because it only compared them to one another. */
void testTheStaticCurve()
{
    GainComputer g;
    g.thresholdDb = 0.0;
    g.kneeDb      = 6.0;
    g.slope       = 4.0;
    g.rangeDb     = 18.0;

    checkNear (g.reductionDb (-3.0), 0.0, 1.0e-12,
               "flat below the knee: the curve starts at T - W/2");

    // At the threshold the parabola has covered (0 + 3)^2 / 12 = 0.75 dB of
    // over, times the 0.75 slope.
    checkNear (g.reductionDb (0.0), 0.5625, 1.0e-12,
               "the knee's midpoint, about half a dB rather than a corner");

    checkNear (g.reductionDb (12.0), 9.0, 1.0e-12,
               "0.75 dB of cut per dB of prominence in the linear region");

    checkNear (g.reductionDb (100.0), 18.0, 1.0e-12,
               "the curve flattens hard at RANGE");

    checkNear (g.reductionDb (Prominence::kSilent), 0.0, 1.0e-12,
               "a gated detector asks for no reduction");
}

/** RANGE is a ceiling on the applied figure, at every setting it can take. */
void testRangeIsACeiling()
{
    GainComputer g;
    g.thresholdDb = 0.0;

    for (const auto range : { 1.0, 8.0, 18.0 })
    {
        g.rangeDb = range;
        checkNear (g.reductionDb (60.0), range, 1.0e-12,
                   "a loud ess reaches exactly RANGE at " + std::to_string (range));
    }
}

/** Attack is one pole at 0.8 ms, and that number is what lets this module
    refuse lookahead: about 90 % applied 2 ms into an onset. Measured the way
    the tau convention says, from the sample before the step. */
void testAttackReachesTheEventInTime()
{
    Reduction::Config config;
    Reduction r;
    r.prepare (config, kSampleRate);

    const auto target   = 8.0;
    const auto atTau    = (int) std::lround (0.8e-3 * kSampleRate);
    const auto atTwoMs  = (int) std::lround (2.0e-3 * kSampleRate);

    double oneTau = 0.0;

    for (int i = 0; i < atTwoMs; ++i)
    {
        const auto applied = r.process (target);

        if (i == atTau - 1)
            oneTau = applied;
    }

    checkNear (oneTau / target, 0.632, 0.02,
               "attack covers 63.2 % of the step in one tau");
    check (r.appliedDb() / target > 0.9,
           "about 90 % of the reduction is applied 2 ms into an onset");
}

/** The hold is what keeps an /s/-/t/ cluster one event. Without it the
    follower starts releasing into the stop and has to attack again on the
    burst, which is audible as a flutter on one syllable. */
void testHoldKeepsAClusterTogether()
{
    Reduction::Config config;
    Reduction r;
    r.prepare (config, kSampleRate);

    for (int i = 0; i < (int) (0.02 * kSampleRate); ++i)
        r.process (8.0);

    const auto engaged = r.appliedDb();

    // A 3 ms gap, shorter than the 5 ms hold.
    for (int i = 0; i < (int) (0.003 * kSampleRate); ++i)
        r.process (0.0);

    checkNear (r.appliedDb(), engaged, 1.0e-9,
               "a gap shorter than the hold does not release at all");

    for (int i = 0; i < (int) (0.03 * kSampleRate); ++i)
        r.process (0.0);

    check (r.appliedDb() < engaged * 0.5,
           "past the hold the reduction does release");
}

/** The slow branch is for a sustained bright passage rather than a phoneme,
    so an ordinary ess must not reach it however loud it is. */
void testTheSlowBranchNeedsMoreThanAPhoneme()
{
    const auto releaseAfter = [] (double heldSeconds)
    {
        Reduction::Config config;
        Reduction r;
        r.prepare (config, kSampleRate);

        for (int i = 0; i < (int) (heldSeconds * kSampleRate); ++i)
            r.process (8.0);

        const auto engaged = r.appliedDb();

        // 60 ms of release, two fast time constants.
        for (int i = 0; i < (int) (0.06 * kSampleRate); ++i)
            r.process (0.0);

        return r.appliedDb() / engaged;
    };

    const auto phoneme = releaseAfter (0.12);   // a long ess
    const auto passage = releaseAfter (0.4);    // a bright passage

    check (phoneme < 0.25, "an ess releases on the fast branch");
    check (passage > phoneme * 1.5,
           "a sustained passage releases more slowly than a phoneme");
}

/** Hysteresis is claimed only while the module is actually engaged. */
void testHysteresisFollowsEngagement()
{
    Reduction::Config config;
    Reduction r;
    r.prepare (config, kSampleRate);

    checkNear (r.thresholdOffsetDb(), 0.0, 1.0e-12,
               "an idle module claims no hysteresis");

    for (int i = 0; i < (int) (0.02 * kSampleRate); ++i)
        r.process (8.0);

    checkNear (r.thresholdOffsetDb(), config.hysteresisDb, 1.0e-12,
               "an engaged module drops its threshold by the stated amount");
}

int main()
{
    testPlaceholderIsAWire();
    testLatencyIsZeroEverywhere();
    testAdapterUnpacksInIndexOrder();
    testListenIsMomentary();
    testGainReductionIsZeroForNow();
    testTheScheduleIsFiveParameters();

    testProminenceIsLevelIndependent();
    testTheGatesAreGates();
    testTheStaticCurve();
    testRangeIsACeiling();
    testAttackReachesTheEventInTime();
    testHoldKeepsAClusterTogether();
    testTheSlowBranchNeedsMoreThanAPhoneme();
    testHysteresisFollowsEngagement();

    std::printf ("%s  BMO Defang DSP: %d checks, %d failures\n",
                 failures == 0 ? "PASS" : "FAIL", checks, failures);

    return failures == 0 ? 0 : 1;
}
