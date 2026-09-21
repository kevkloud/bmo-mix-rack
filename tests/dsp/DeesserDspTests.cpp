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

int main()
{
    testPlaceholderIsAWire();
    testLatencyIsZeroEverywhere();
    testAdapterUnpacksInIndexOrder();
    testListenIsMomentary();
    testGainReductionIsZeroForNow();
    testTheScheduleIsFiveParameters();

    std::printf ("%s  BMO Defang DSP: %d checks, %d failures\n",
                 failures == 0 ? "PASS" : "FAIL", checks, failures);

    return failures == 0 ? 0 : 1;
}
