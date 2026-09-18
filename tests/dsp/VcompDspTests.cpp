/*
    Tests for BMO Vcomp's DSP core. No JUCE, no host: DspCore takes plain
    buffers, so the claims the module is built on are checked in a couple of
    seconds on a bare container.

    What is pinned here is the argument in modules/vcomp/AGENTS.md, claim by
    claim: AMOUNT 0 is inert, AMOUNT buys density rather than level, the curve
    delivers the ratio it says, ARC's slow branch really is programme-dependent
    (the naive two-branch implementation is not, and would pass a weaker test),
    standard mode really does ignore the six complex parameters, and turning
    COMPLEX on with untouched knobs really is silent.
*/

#include "modules/vcomp/dsp/DspCore.h"
#include "modules/vcomp/dsp/VcompDsp.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace bmo::vcomp;

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

double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
double linToDb (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

std::vector<float> render (const std::vector<float>& input, DspCore::Params params)
{
    // setParams before prepare, which is the order ModuleDsp documents and
    // the order a host uses -- prepare() snaps the parameter smoothers to
    // whatever the parameters are, so the other way round every render spends
    // its first 15 ms ramping AMOUNT up from the default.
    DspCore core;
    core.setParams (params);
    core.prepare (kSampleRate, 512, 1);

    auto out = input;

    for (size_t at = 0; at < out.size(); at += 512)
    {
        auto* p = out.data() + at;
        core.process (&p, 1, (int) std::min<size_t> (512, out.size() - at));
    }

    return out;
}

std::pair<std::vector<float>, std::vector<float>> renderStereo (
    const std::vector<float>& l, const std::vector<float>& r, DspCore::Params params)
{
    DspCore core;
    core.setParams (params);
    core.prepare (kSampleRate, 512, 2);

    auto outL = l, outR = r;

    for (size_t at = 0; at < outL.size(); at += 512)
    {
        const auto n = (int) std::min<size_t> (512, outL.size() - at);
        float* channels[2] { outL.data() + at, outR.data() + at };
        core.process (channels, 2, n);
    }

    return { outL, outR };
}

/** Peak over a window given in seconds, so a test can look at the settled
    part of a tone and ignore the detector coming up to speed. */
double peakBetween (const std::vector<float>& v, double fromSec, double toSec)
{
    const auto from = (size_t) (fromSec * kSampleRate);
    const auto to   = std::min (v.size(), (size_t) (toSec * kSampleRate));
    auto p = 0.0;

    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (v[i]));

    return p;
}

/** RMS over a window. Phase-insensitive, which matters wherever a test looks
    at a signal that has been through the band split: the crossover is allpass
    in magnitude but not in phase, so a *peak* comparison at 8 kHz -- six
    samples a cycle -- reads up to 1.25 dB of pure sampling artefact as if it
    were a frequency response. */
double rmsBetween (const std::vector<float>& v, double fromSec, double toSec)
{
    const auto from = (size_t) (fromSec * kSampleRate);
    const auto to   = std::min (v.size(), (size_t) (toSec * kSampleRate));
    auto sum = 0.0;

    for (size_t i = from; i < to; ++i)
        sum += (double) v[i] * v[i];

    return std::sqrt (sum / (double) (to - from));
}

/** Amplitude of one frequency over a window, by direct correlation against it
    rather than a whole FFT -- one bin is all these tests need. Lets a test
    watch what the compressor is doing to a quiet tone while a loud one at
    another frequency is triggering it. */
double magnitudeAt (const std::vector<float>& v, double hz, double fromSec, double toSec)
{
    const auto from = (size_t) (fromSec * kSampleRate);
    const auto to   = std::min (v.size(), (size_t) (toSec * kSampleRate));
    auto re = 0.0, im = 0.0;

    for (size_t i = from; i < to; ++i)
    {
        const auto t = 2.0 * kPi * hz * (double) i / kSampleRate;
        re += (double) v[i] * std::cos (t);
        im += (double) v[i] * std::sin (t);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) (to - from);
}

DspCore::Params standard (float amountPercent, float outputDb = 0.0f)
{
    DspCore::Params p;
    p.amountPercent = amountPercent;
    p.outputDb = outputDb;
    return p;
}

//==============================================================================
/** AMOUNT 0 is a wire, and not by a special case: the ratio sweep starts at
    1:1, which is a slope of zero, so the curve reduces nothing at any level.
    This is the house rule from modules/opto/params.h -- a freshly inserted
    instance is heard doing nothing until the ear asks for it -- and it has to
    hold for the *default* parameters, which is what a user actually inserts.

    Checked at two levels well apart, because the earlier curve was inert only
    below 0 dBFS and a single quiet tone would not have told them apart. */
void testAmountZeroIsInert()
{
    const auto in  = sine (440.0, 0.5, dbToLin (-6.0));
    const auto out = render (in, standard (0.0f));

    auto worst = 0.0;

    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - in[i]));

    checkNear (worst, 0.0, 1.0e-6, "AMOUNT 0 passes a -6 dBFS tone through untouched");

    // The other end: a hot -1 dBFS tone, which the earlier curve did reach.
    const auto hot = sine (440.0, 0.5, dbToLin (-1.0));
    const auto hotOut = render (hot, standard (0.0f));

    worst = 0.0;

    for (size_t i = 0; i < hot.size(); ++i)
        worst = std::max (worst, (double) std::abs (hotOut[i] - hot[i]));

    checkNear (worst, 0.0, 1.0e-6, "AMOUNT 0 passes a -1 dBFS tone through untouched");
}

/** AMOUNT grabs harder as it goes up. Measured as gain reduction rather than
    as output level, because the makeup deliberately hides the level change --
    which is the next test. */
void testAmountGrabsHarder()
{
    const auto in = sine (440.0, 1.0, dbToLin (-10.0));

    auto reductionAt = [&in] (float amountPercent)
    {
        DspCore core;
        core.setParams (standard (amountPercent));
        core.prepare (kSampleRate, 512, 1);

        auto buf = in;

        for (size_t at = 0; at < buf.size(); at += 512)
        {
            auto* p = buf.data() + at;
            core.process (&p, 1, (int) std::min<size_t> (512, buf.size() - at));
        }

        return (double) core.currentGainReductionDb();
    };

    const auto low = reductionAt (20.0f), mid = reductionAt (50.0f), high = reductionAt (80.0f);

    checkNear (reductionAt (0.0f), 0.0, 1.0e-4, "AMOUNT 0 reduces nothing");
    check (low > 0.5, "AMOUNT 20 is doing something");
    check (mid > low + 2.0, "AMOUNT 50 reduces meaningfully more than 20");
    check (high > mid + 2.0, "AMOUNT 80 reduces meaningfully more than 50");
}

/** The headline claim: AMOUNT buys density, not level. A voice at the
    reference level comes out at the reference level however far the knob is
    turned, because the auto makeup adds back exactly what the static curve
    took at that level.

    One dB of tolerance, and it is the detector's ripple that spends it: a peak
    detector on a sine dips a little between peaks, so the mean reduction is a
    shade under the static figure and the tone comes out a shade hot. */
void testAmountBuysDensityNotLevel()
{
    const auto in = sine (440.0, 2.0, dbToLin (kReferenceDb));

    for (const auto amountPercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto out = render (in, standard (amountPercent));
        const auto db  = linToDb (peakBetween (out, 1.0, 2.0));

        checkNear (db, kReferenceDb, 1.0,
                   "a " + std::to_string ((int) kReferenceDb) + " dBFS tone stays there at AMOUNT "
                       + std::to_string ((int) amountPercent));
    }
}

/** OUTPUT adds exactly what it says, on top of the automatic makeup. */
void testOutputIsExact()
{
    const auto in = sine (440.0, 2.0, dbToLin (kReferenceDb));

    const auto flat = linToDb (peakBetween (render (in, standard (50.0f, 0.0f)), 1.0, 2.0));
    const auto up   = linToDb (peakBetween (render (in, standard (50.0f, 6.0f)), 1.0, 2.0));
    const auto down = linToDb (peakBetween (render (in, standard (50.0f, -6.0f)), 1.0, 2.0));

    checkNear (up - flat, 6.0, 0.05, "OUTPUT +6 adds 6 dB");
    checkNear (down - flat, -6.0, 0.05, "OUTPUT -6 takes 6 dB");
}

/** The curve delivers the ratio it advertises. Two steady tones well above
    threshold, 10 dB apart at the input: the output difference should be
    10/ratio. Measured with the makeup and OUTPUT identical for both, so they
    cancel out of the difference entirely. */
void testDeliveredRatio()
{
    const auto amountPercent = 100.0f;   // ratio kRatioMax, threshold -40 dBFS
    const auto expectedRatio = kRatioMax;

    const auto loud  = render (sine (440.0, 2.0, dbToLin (-10.0)), standard (amountPercent));
    const auto quiet = render (sine (440.0, 2.0, dbToLin (-20.0)), standard (amountPercent));

    const auto delivered = 10.0 / (linToDb (peakBetween (loud, 1.0, 2.0))
                                       - linToDb (peakBetween (quiet, 1.0, 2.0)));

    checkNear (delivered, (double) expectedRatio, 0.6,
               "10 dB in comes out as 10/ratio dB at full AMOUNT");
}

//==============================================================================
/** ARC is genuinely programme-dependent, and this is the test the naive
    implementation fails.

    Two signals, same level, same peak reduction: one a short burst, one a long
    sustained tone. Recovery after the long one has to be *slower*, because the
    slow branch only charges on sustained material. Two release branches
    combined with max() and no slow attack would recover identically after both
    -- it would be the slow branch either way -- so a test that only checked
    "release is slow" would pass a broken ARC. See ReleaseStage in Detector.h.

    Measured as how far the gain has come back a fixed time after the tone
    stops, by looking at what a quiet probe tone that follows it is scaled by. */
void testArcIsProgrammeDependent()
{
    // hit for `hitSec`, then a quiet probe at the reference level.
    auto recoveryDb = [] (double hitSec)
    {
        const auto silenceSec = 0.15;
        const auto probeSec   = 0.05;

        auto buf = sine (440.0, hitSec, dbToLin (-4.0));
        const auto gap = sine (440.0, silenceSec, 0.0);
        const auto probeIn = sine (440.0, probeSec, dbToLin (-40.0));

        buf.insert (buf.end(), gap.begin(), gap.end());
        const auto probeStart = buf.size();
        buf.insert (buf.end(), probeIn.begin(), probeIn.end());

        // OUTPUT trimmed well down so the limiter never engages. Every test that
        // compares two renders has to stay off the ceiling: the limiter is last
        // and pins whatever reaches it to the same level, which would make two
        // different settings measure identical and the check vacuous.
        auto p = standard (70.0f, -24.0f);
        p.complex = true;
        p.arc = true;
        const auto out = render (buf, p);

        const auto from = (double) probeStart / kSampleRate;
        // How much the probe was pulled down: less reduction left = more
        // recovered.
        return linToDb (peakBetween (out, from, from + probeSec))
                   - linToDb (peakBetween (probeIn, 0.0, probeSec));
    };

    const auto afterShort = recoveryDb (0.03);   // one consonant
    const auto afterLong  = recoveryDb (3.0);    // a sustained phrase

    // Both figures include the same static makeup, so comparing them is
    // comparing leftover reduction directly.
    check (afterLong < afterShort - 1.0,
           "ARC: the gain is still further down after a long hit than after a short one");

    // And the control: with ARC off, the release is one time constant and the
    // length of the hit makes no difference to where it has got to.
    auto recoveryWithoutArc = [] (double hitSec)
    {
        const auto silenceSec = 0.15;
        const auto probeSec   = 0.05;

        auto buf = sine (440.0, hitSec, dbToLin (-4.0));
        const auto gap = sine (440.0, silenceSec, 0.0);
        const auto probeIn = sine (440.0, probeSec, dbToLin (-40.0));

        buf.insert (buf.end(), gap.begin(), gap.end());
        const auto probeStart = buf.size();
        buf.insert (buf.end(), probeIn.begin(), probeIn.end());

        // OUTPUT trimmed well down so the limiter never engages. Every test that
        // compares two renders has to stay off the ceiling: the limiter is last
        // and pins whatever reaches it to the same level, which would make two
        // different settings measure identical and the check vacuous.
        auto p = standard (70.0f, -24.0f);
        p.complex = true;
        p.arc = false;
        const auto out = render (buf, p);

        const auto from = (double) probeStart / kSampleRate;
        return linToDb (peakBetween (out, from, from + probeSec))
                   - linToDb (peakBetween (probeIn, 0.0, probeSec));
    };

    checkNear (recoveryWithoutArc (3.0), recoveryWithoutArc (0.03), 0.5,
               "ARC off: the length of the hit does not change the recovery");
}

/** RELEASE still means something with ARC on -- it scales the branches rather
    than being ignored, so a longer setting recovers less by a fixed time. */
void testReleaseScalesArc()
{
    auto leftoverDb = [] (float releaseMs)
    {
        const auto probeSec = 0.05;

        auto buf = sine (440.0, 2.0, dbToLin (-4.0));
        const auto gap = sine (440.0, 0.2, 0.0);
        const auto probeIn = sine (440.0, probeSec, dbToLin (-40.0));

        buf.insert (buf.end(), gap.begin(), gap.end());
        const auto probeStart = buf.size();
        buf.insert (buf.end(), probeIn.begin(), probeIn.end());

        // OUTPUT trimmed well down so the limiter never engages. Every test that
        // compares two renders has to stay off the ceiling: the limiter is last
        // and pins whatever reaches it to the same level, which would make two
        // different settings measure identical and the check vacuous.
        auto p = standard (70.0f, -24.0f);
        p.complex = true;
        p.arc = true;
        p.releaseMs = releaseMs;

        const auto out = render (buf, p);
        const auto from = (double) probeStart / kSampleRate;

        return linToDb (peakBetween (out, from, from + probeSec))
                   - linToDb (peakBetween (probeIn, 0.0, probeSec));
    };

    check (leftoverDb (1000.0f) < leftoverDb (50.0f) - 1.0,
           "RELEASE 1000 has recovered less than RELEASE 50, with ARC on");
}

/** A shorter ATTACK reaches its reduction sooner. Measured on the very front
    of a tone that starts hot from silence. */
void testAttackIsFasterWhenShorter()
{
    const auto in = sine (440.0, 0.2, dbToLin (-4.0));

    auto peakInFirst = [&in] (float attackMs, double windowSec)
    {
        // OUTPUT trimmed well down so the limiter never engages. Every test that
        // compares two renders has to stay off the ceiling: the limiter is last
        // and pins whatever reaches it to the same level, which would make two
        // different settings measure identical and the check vacuous.
        auto p = standard (70.0f, -24.0f);
        p.complex = true;
        p.attackMs = attackMs;
        return peakBetween (render (in, p), 0.0, windowSec);
    };

    // A fast attack has already pulled the first few ms down; a slow one has
    // not, so it lets more through.
    check (peakInFirst (0.5f, 0.005) < peakInFirst (100.0f, 0.005),
           "ATTACK 0.5 ms lets less of the first 5 ms through than ATTACK 100 ms");
}

/** The sidechain high-pass takes the detector's ear off the bottom end. The
    same loud tone, below and above the cutoff: the low one must produce less
    reduction. */
void testSidechainHighpassDeafensTheDetector()
{
    auto reductionFor = [] (double hz, float cutoffHz)
    {
        DspCore core;

        // OUTPUT trimmed well down so the limiter never engages. Every test that
        // compares two renders has to stay off the ceiling: the limiter is last
        // and pins whatever reaches it to the same level, which would make two
        // different settings measure identical and the check vacuous.
        auto p = standard (70.0f, -24.0f);
        p.complex = true;
        p.sidechainHz = cutoffHz;
        core.setParams (p);
        core.prepare (kSampleRate, 512, 1);

        auto buf = sine (hz, 1.0, dbToLin (-6.0));

        for (size_t at = 0; at < buf.size(); at += 512)
        {
            auto* q = buf.data() + at;
            core.process (&q, 1, (int) std::min<size_t> (512, buf.size() - at));
        }

        return (double) core.currentGainReductionDb();
    };

    const auto low  = reductionFor (50.0, 300.0f);
    const auto high = reductionFor (1000.0, 300.0f);

    check (low < high - 6.0, "a 50 Hz tone under a 300 Hz sidechain cut reduces far less than 1 kHz");

    // At the bottom of the range the filter is effectively flat, which is what
    // stands in for an Off position -- see params.h.
    checkNear (reductionFor (50.0, 20.0f), reductionFor (1000.0, 20.0f), 1.0,
               "at SIDECHAIN 20 Hz the detector hears 50 Hz and 1 kHz alike");
}

//==============================================================================
/** Standard mode ignores ATTACK, RELEASE, ARC and SIDECHAIN entirely. Not
    "the panel hides them": the DSP does not read them. Set all four to
    something wild with COMPLEX off and the output must be sample-identical. */
void testStandardModeIgnoresTheComplexControls()
{
    const auto in = sine (440.0, 1.0, dbToLin (-8.0));

    auto p = standard (60.0f);
    const auto plain = render (in, p);

    p.attackMs = 100.0f;
    p.releaseMs = 1000.0f;
    p.arc = false;
    p.sidechainHz = 500.0f;
    p.lowThruHz = 400.0f;
    p.highThruHz = 3000.0f;
    const auto wild = render (in, p);

    auto worst = 0.0;

    for (size_t i = 0; i < plain.size(); ++i)
        worst = std::max (worst, (double) std::abs (plain[i] - wild[i]));

    checkNear (worst, 0.0, 0.0, "with COMPLEX off the six detector parameters change nothing");
}

/** Turning COMPLEX on with untouched knobs is silent. The standard-mode
    figures and the six defaults are the same numbers, and this is what keeps
    them the same numbers: an escape hatch a user falls through is a trapdoor. */
void testComplexIsSilentAtTheDefaults()
{
    const auto in = sine (440.0, 1.0, dbToLin (-8.0));

    auto p = standard (60.0f);
    const auto off = render (in, p);

    p.complex = true;      // the other six are already at their defaults
    const auto on = render (in, p);

    auto worst = 0.0;

    for (size_t i = 0; i < off.size(); ++i)
        worst = std::max (worst, (double) std::abs (off[i] - on[i]));

    checkNear (worst, 0.0, 0.0, "COMPLEX on at the defaults sounds exactly like COMPLEX off");
}

/** Stereo is always linked: one detector, both channels, no switch. A loud
    left channel must pull the right one down with it, by the same amount. */
void testStereoIsLinked()
{
    const auto loud  = sine (440.0, 1.0, dbToLin (-4.0));
    const auto quiet = sine (440.0, 1.0, dbToLin (-30.0));

    const auto [outL, outR] = renderStereo (loud, quiet, standard (70.0f));

    const auto gainL = linToDb (peakBetween (outL, 0.5, 1.0)) - linToDb (peakBetween (loud, 0.5, 1.0));
    const auto gainR = linToDb (peakBetween (outR, 0.5, 1.0)) - linToDb (peakBetween (quiet, 0.5, 1.0));

    checkNear (gainR, gainL, 0.1, "both channels get the gain the louder one decided");
    check (gainL < -1.0, "and that gain is a reduction, so the test is not vacuous");
}

//==============================================================================
/** The gate is exactly inert at its rail, not nearly inert. "Off" is a promise
    the module makes -- AMOUNT 0 with GATE at its floor has to be a wire -- and
    a soft knee that reached a fraction of a dB below the rail would break it
    without anything sounding wrong. */
void testGateIsInertAtItsRail()
{
    // Quiet enough that a gate anywhere near its rail would grab it.
    const auto in = sine (440.0, 0.5, dbToLin (-55.0));
    const auto out = render (in, standard (0.0f));

    auto worst = 0.0;

    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - in[i]));

    checkNear (worst, 0.0, 1.0e-9, "GATE at its rail passes a -55 dBFS tone through untouched");
}

/** What the gate is actually for: the auto makeup lifts the room tone by as
    much as it lifts the voice, and the gate is what stops that. A loud phrase
    followed by a quiet tail, compressed hard -- with the gate set between the
    two, the tail comes out far quieter than it would without it, and the
    phrase itself is unchanged. */
void testGateShutsWhatTheMakeupWouldLift()
{
    const auto phraseSec = 0.8, tailSec = 0.8;

    auto buf = sine (440.0, phraseSec, dbToLin (-8.0));
    const auto tail = sine (440.0, tailSec, dbToLin (-48.0));
    buf.insert (buf.end(), tail.begin(), tail.end());

    auto p = standard (75.0f);              // enough makeup to make the point
    const auto ungated = render (buf, p);

    p.gateDb = -30.0f;                      // between the phrase and the tail
    const auto gated = render (buf, p);

    // Late in the tail, well past the hold and several close time constants.
    const auto from = phraseSec + 0.5, to = phraseSec + tailSec;
    const auto tailUngated = linToDb (peakBetween (ungated, from, to));
    const auto tailGated   = linToDb (peakBetween (gated,   from, to));

    check (tailGated < tailUngated - 20.0,
           "the gate takes at least 20 dB off the tail the makeup was lifting");

    // And the phrase itself is untouched: a gate that costs you the voice to
    // clean up the silence is not a trade worth making.
    const auto phraseUngated = linToDb (peakBetween (ungated, 0.3, phraseSec));
    const auto phraseGated   = linToDb (peakBetween (gated,   0.3, phraseSec));

    checkNear (phraseGated, phraseUngated, 0.1, "the gate leaves the phrase alone");
}

/** The gate opens fast enough not to bite the first syllable off. A tone
    starting from silence with the gate armed: within 5 ms it is essentially
    all the way open. This is the direction that costs you a word if it is
    wrong, which is why it is ~0.5 ms and the closing side is 150. */
void testGateOpensFastEnoughForTheFirstSyllable()
{
    std::vector<float> buf ((size_t) (0.2 * kSampleRate), 0.0f);
    const auto tone = sine (440.0, 0.3, dbToLin (-8.0));
    const auto startsAt = buf.size();
    buf.insert (buf.end(), tone.begin(), tone.end());

    auto p = standard (0.0f);   // no compression: the gate is the only thing acting
    p.gateDb = -30.0f;
    const auto out = render (buf, p);

    const auto from = (double) startsAt / kSampleRate;
    const auto early = linToDb (peakBetween (out, from + 0.005, from + 0.02));
    const auto settled = linToDb (peakBetween (out, from + 0.2, from + 0.3));

    checkNear (early, settled, 1.0, "the gate is open within 5 ms of the tone starting");
}

//==============================================================================
/** The three bands reconstruct. With the split in circuit but nothing being
    compressed, the module has to be flat: LOW THRU and HIGH THRU change what
    the compressor acts on, and are not allowed to be an EQ in their own right.

    This is the test that fails if the low band is not run through the second
    crossover's allpass -- see Crossover.h. Without that alignment the error is
    a dip around the upper crossover, so the tones either side of HIGH THRU are
    where it shows. */
void testBandsReconstruct()
{
    auto p = standard (0.0f);   // AMOUNT 0: compressor gain and makeup are both 1
    p.complex = true;
    p.lowThruHz = 200.0f;
    p.highThruHz = 4000.0f;

    for (const auto hz : { 60.0, 150.0, 200.0, 500.0, 1000.0, 3000.0, 4000.0, 8000.0 })
    {
        const auto in = sine (hz, 0.5, dbToLin (-12.0));
        const auto out = render (in, p);

        // RMS, not peak, and settled part only: the crossover needs a few
        // cycles to charge, and its phase shift makes a peak comparison at the
        // top of the range read sampling artefact as frequency response.
        const auto inDb  = linToDb (rmsBetween (in,  0.3, 0.5));
        const auto outDb = linToDb (rmsBetween (out, 0.3, 0.5));

        checkNear (outDb, inDb, 0.5,
                   "the split reconstructs flat at " + std::to_string ((int) hz) + " Hz");
    }
}

/** A quiet tone at one frequency, and a loud burst at another that drives the
    compressor. Measures what happens to the quiet tone while the burst is
    triggering reduction, against what happens to it once the burst has gone.
    Returns (during - after) in dB: how much the compressor is *modulating* the
    tone. */
double duckingOf (double toneHz, DspCore::Params p)
{
    constexpr double burstFrom = 0.6, burstTo = 1.2, total = 2.0;

    auto buf = sine (toneHz, total, dbToLin (-20.0));
    const auto burst = sine (2000.0, total, dbToLin (-4.0));

    for (size_t i = (size_t) (burstFrom * kSampleRate); i < (size_t) (burstTo * kSampleRate); ++i)
        buf[i] += burst[i];

    const auto out = render (buf, p);

    // Late in the burst, so the detector has settled; and late after it, so
    // the release has let go.
    return linToDb (magnitudeAt (out, toneHz, 1.0, 1.2))
               - linToDb (magnitudeAt (out, toneHz, 1.7, 1.9));
}

/** LOW THRU means the low end is genuinely not acted on, which is a different
    claim from the sidechain high-pass making the detector deaf to it.

    **Measured as modulation, not as level.** The obvious test -- "the low tone
    comes out louder with LOW THRU up" -- is wrong, and wrong in a way that
    looks right until you do the arithmetic: the automatic makeup gives back
    almost exactly what the curve took at the reference level, so a steady tone
    near that level comes out at the same place whether it was compressed or
    not. The first version of this test asserted a 6 dB difference and found
    0.9 dB, which is the makeup working, not the split failing.

    What LOW THRU actually buys is that the low end stops being *pumped* by
    whatever else triggers the compressor. So: a steady 80 Hz tone under a loud
    2 kHz burst. Without the split the burst ducks the low tone with everything
    else; with it, the low tone sits still. */
void testLowThruStopsTheLowEndBeingPumped()
{
    // OUTPUT trimmed so the limiter stays out of it: at AMOUNT 80 the makeup
    // alone is ~22 dB, the output would sit on the ceiling, and the limiter
    // would be modulating everything the ducking figure is trying to measure.
    auto p = standard (80.0f, -24.0f);
    p.complex = true;

    const auto pumped = duckingOf (80.0, p);

    p.lowThruHz = 300.0f;
    const auto held = duckingOf (80.0, p);

    check (pumped < -6.0, "without LOW THRU a 2 kHz burst ducks an 80 Hz tone by over 6 dB");
    checkNear (held, 0.0, 1.0, "with LOW THRU 300 the same burst leaves the 80 Hz tone alone");
}

/** The same at the top. Air and sibilance stop being pumped by the body of the
    voice.

    The tone sits nearly two octaves above the crossover on purpose. At 24
    dB/octave a tone less than an octave clear is genuinely in *both* bands --
    9 kHz against a 5 kHz split leaves about -20 dB of itself in the compressed
    band, and that residue is ducked with everything else, which showed up here
    as 1.2 dB of leftover pumping. That is the crossover's slope doing exactly
    what a crossover does, not the split failing, so the test is written where
    the claim is actually being made rather than loosened until the smeared
    case passes. */
void testHighThruStopsTheTopBeingPumped()
{
    // OUTPUT trimmed so the limiter stays out of it: at AMOUNT 80 the makeup
    // alone is ~22 dB, the output would sit on the ceiling, and the limiter
    // would be modulating everything the ducking figure is trying to measure.
    auto p = standard (80.0f, -24.0f);
    p.complex = true;

    const auto pumped = duckingOf (12000.0, p);

    p.highThruHz = 4000.0f;
    const auto held = duckingOf (12000.0, p);

    check (pumped < -6.0, "without HIGH THRU a 2 kHz burst ducks a 12 kHz tone by over 6 dB");
    checkNear (held, 0.0, 1.0, "with HIGH THRU 4000 the same burst leaves the 12 kHz tone alone");
}

/** Engaging one side must not quietly disengage the other, which is exactly
    what it used to do.

    With LOW THRU off its rail the crossover is in circuit, and the *upper*
    split was then run as well -- at HIGH THRU's 20 kHz rail, which a filter
    cannot be placed at, so it was clamped. The clamp landed at 10.8 kHz at 48
    kHz, and everything above that was handed to the thru band and passed
    through uncompressed while the panel said HIGH THRU was off.

    Nothing sounded broken; the module just stopped compressing the top end.
    tools/measure/vcomp's bands report is what caught it -- 4.7 dB of pumping
    where the un-split case had 11.5 -- and this is the test that was missing.
    A 12 kHz tone is used because that is above the old clamp and below the
    new one. */
void testOneSideEngagedLeavesTheOtherAlone()
{
    // OUTPUT trimmed so the limiter stays out of it: at AMOUNT 80 the makeup
    // alone is ~22 dB, the output would sit on the ceiling, and the limiter
    // would be modulating everything the ducking figure is trying to measure.
    auto p = standard (80.0f, -24.0f);
    p.complex = true;

    const auto bothRails = duckingOf (12000.0, p);

    p.lowThruHz = 300.0f;       // engages the crossover; HIGH THRU stays at its rail
    const auto lowOnly = duckingOf (12000.0, p);

    check (bothRails < -6.0, "the baseline pumps, so the test is not vacuous");
    checkNear (lowOnly, bothRails, 1.0,
               "LOW THRU alone leaves the top end as compressed as it was");
}

/** The thru bands cannot run away with the makeup.

    A band that is passed through uncompressed and then handed the whole makeup
    can only get louder, by the whole makeup figure. That is not a voicing
    choice anybody could tune around; it is what "both bands get the makeup"
    means arithmetically, and it made LOW THRU a control that worked at the
    bottom of AMOUNT and defeated itself at the top. With the cap taken out of
    DspCore.h this test prints the mechanism exactly: the three lifts below come
    out 12.44, 18.92 and 25.51 dB, which is the makeup column of
    `measure_vcomp curve` to the second decimal.

    What is checked is the design claim rather than the formula -- that the
    lift is bounded and stops growing, while the makeup over the same span of
    the knob goes on growing by 13 dB. Asserting the figure the same expression
    produces would only prove the expression was copied correctly twice.

    OUTPUT is trimmed 24 dB so the limiter is nowhere near the result -- the
    same reason the two pumping tests trim it -- and the trim is added back
    before the comparison. 80 Hz against a 300 Hz split is nearly two octaves
    clear, which at 24 dB/octave leaves the compressed band's copy of the tone
    far enough down to ignore. */
void testThruMakeupCannotRunAway()
{
    const auto tone = sine (80.0, 2.0, dbToLin (-20.0));
    const auto dry  = linToDb (magnitudeAt (tone, 80.0, 0.5, 1.9));

    const auto liftAt = [&] (float amountPercent)
    {
        auto p = standard (amountPercent, -24.0f);
        p.complex   = true;
        p.lowThruHz = 300.0f;

        return linToDb (magnitudeAt (render (tone, p), 80.0, 0.5, 1.9)) - dry + 24.0;
    };

    const auto at10 = liftAt (10.0f);
    const auto at50 = liftAt (50.0f);
    const auto at90 = liftAt (90.0f);

    check (at50 > 0.0 && at50 < 7.0, "the thru band is lifted, and by well under its makeup, at AMOUNT 50");
    check (at90 > 0.0 && at90 < 7.0, "and still by well under its makeup at AMOUNT 90");

    // 12.44 dB of makeup at AMOUNT 50 against 25.55 at 90. The thru band's
    // share of that has to stop climbing or the feature eats itself.
    check (at90 - at50 < 1.5,
           "the thru band's lift stops growing while the makeup it comes from grows 13 dB");

    // A cap that clamped everything to one figure would pass the three checks
    // above and make LOW THRU do the same thing at every AMOUNT. It is derived
    // from the curve, so at the bottom of the knob there is barely any of it.
    check (at10 > 0.0 && at10 < at50,
           "at a low AMOUNT the thru band takes a correspondingly small lift");
}

/** At both rails the crossover is bypassed outright, not run with empty outer
    bands. The difference is invisible in a magnitude plot and real all the
    same: a crossover left in circuit still costs its allpass phase shift, so a
    user who is not using the feature would be paying for it. Sample-identical
    to the un-split path is the only version of "off" worth having. */
void testRailsAreExactlyOff()
{
    const auto in = sine (440.0, 1.0, dbToLin (-8.0));

    auto p = standard (60.0f);
    const auto unsplit = render (in, p);

    p.complex = true;
    p.lowThruHz = kLowThruOffHz;
    p.highThruHz = kHighThruOffHz;
    const auto railed = render (in, p);

    auto worst = 0.0;

    for (size_t i = 0; i < unsplit.size(); ++i)
        worst = std::max (worst, (double) std::abs (unsplit[i] - railed[i]));

    checkNear (worst, 0.0, 0.0, "LOW and HIGH THRU at their rails bypass the crossover entirely");
}

//==============================================================================
/** The ceiling is a ceiling, not a target it usually hits.

    Zero latency means the limiter cannot see a transient coming, so it holds
    the ceiling by computing each sample's gain from that same sample. The
    claim that buys is absolute rather than statistical: **no sample, anywhere,
    at any setting, comes out above the ceiling.** Driven here with the module
    at its most aggressive and OUTPUT pushed hard on top, which is the worst
    case a user can build. */
void testLimiterHoldsTheCeiling()
{
    const auto ceiling = dbToLin ((double) kLimiterCeilingDb);

    // A source with real transients, not a steady tone: a limiter that only
    // ever saw sine waves would pass this by being slow.
    std::vector<float> in ((size_t) (2.0 * kSampleRate));

    for (size_t i = 0; i < in.size(); ++i)
    {
        const auto t = (double) i / kSampleRate;
        const auto beat = std::fmod (t, 0.31);
        const auto envelope = beat < 0.002 ? beat / 0.002 : std::exp (-(beat - 0.002) * 9.0);
        in[i] = (float) (dbToLin (-3.0) * envelope * std::sin (2.0 * kPi * 180.0 * t));
    }

    for (const auto amountPercent : { 0.0f, 50.0f, 100.0f })
    {
        for (const auto outputDb : { 0.0f, 12.0f, 24.0f })
        {
            const auto out = render (in, standard (amountPercent, outputDb));
            auto worst = 0.0;

            for (const auto v : out)
                worst = std::max (worst, (double) std::abs (v));

            check (worst <= ceiling * 1.0001,
                   "nothing exceeds the ceiling at AMOUNT " + std::to_string ((int) amountPercent)
                       + " OUTPUT " + std::to_string ((int) outputDb)
                       + " (peak " + std::to_string (linToDb (worst)) + " dBFS)");
        }
    }
}

/** Below the knee the limiter is not there at all.

    A safety net that quietly shaved everything near full scale would break the
    module's own wire claim, and it did: the knee was 3 dB wide and centred on
    the ceiling, so it reached 1.5 dB below it and touched a -1 dBFS tone at
    AMOUNT 0. The knee is 1 dB now and this is the check that keeps it honest
    -- a tone just under the knee's lower edge has to come out bit-identical. */
void testLimiterIsInertBelowItsKnee()
{
    const auto justUnder = (double) kLimiterCeilingDb - (double) kLimiterKneeDb * 0.5 - 0.2;
    const auto in = sine (440.0, 0.5, dbToLin (justUnder));
    const auto out = render (in, standard (0.0f));

    auto worst = 0.0;

    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (out[i] - in[i]));

    checkNear (worst, 0.0, 1.0e-6,
               "a tone just below the limiter's knee passes through untouched");
}

/** Nothing blows up, goes NaN or sticks, over a level sweep that crosses the
    whole curve at the most aggressive setting. */
void testStability()
{
    std::vector<float> in ((size_t) (4.0 * kSampleRate));

    for (size_t i = 0; i < in.size(); ++i)
    {
        const auto t = (double) i / kSampleRate;
        const auto envelope = dbToLin (-60.0 + 60.0 * std::abs (std::sin (2.0 * kPi * 0.25 * t)));
        in[i] = (float) (envelope * std::sin (2.0 * kPi * 220.0 * t));
    }

    auto p = standard (100.0f, 12.0f);
    p.complex = true;
    p.attackMs = 0.1f;
    p.releaseMs = 20.0f;

    const auto out = render (in, p);

    auto finite = true;

    for (const auto v : out)
        finite = finite && std::isfinite (v);

    check (finite, "a 4 s level sweep at the most aggressive settings stays finite");
}

/** Zero latency, at every setting -- no lookahead and no oversampling. */
void testLatencyIsAlwaysZero()
{
    VcompDsp dsp;
    std::vector<float> values (Index::count, 0.0f);

    values[amount] = 100.0f;
    values[complex] = 1.0f;
    values[attack] = 0.1f;

    check (dsp.latencyForParams (values.data(), (int) values.size()) == 0,
           "latency is zero whatever the parameters say");
}

} // namespace

//==============================================================================
int main()
{
    testAmountZeroIsInert();
    testAmountGrabsHarder();
    testAmountBuysDensityNotLevel();
    testOutputIsExact();
    testDeliveredRatio();
    testArcIsProgrammeDependent();
    testReleaseScalesArc();
    testAttackIsFasterWhenShorter();
    testSidechainHighpassDeafensTheDetector();
    testStandardModeIgnoresTheComplexControls();
    testComplexIsSilentAtTheDefaults();
    testStereoIsLinked();
    testGateIsInertAtItsRail();
    testGateShutsWhatTheMakeupWouldLift();
    testGateOpensFastEnoughForTheFirstSyllable();
    testBandsReconstruct();
    testLowThruStopsTheLowEndBeingPumped();
    testHighThruStopsTheTopBeingPumped();
    testOneSideEngagedLeavesTheOtherAlone();
    testThruMakeupCannotRunAway();
    testRailsAreExactlyOff();
    testLimiterHoldsTheCeiling();
    testLimiterIsInertBelowItsKnee();
    testStability();
    testLatencyIsAlwaysZero();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
