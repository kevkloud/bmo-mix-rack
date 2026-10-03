/*
    Tests for BMO Opto's DSP core. No JUCE, no host: DspCore takes plain
    buffers, so the claims the module is built on are checked in a couple of
    seconds on a bare container: it leaves a quiet signal alone, CRUSH makes
    it grab harder, LEVEL adds exactly what it says, the release gets slower
    after a heavier/longer hit (and does so differently per mode), Stressed
    mode's ratio genuinely differs from Tele's, stereo Link actually shares
    gain reduction, and Color's Tele-mode lock actually holds.
*/

#include "modules/opto/dsp/DspCore.h"
#include "modules/opto/dsp/OptoDsp.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bmo::opto;

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

std::vector<float> render (const std::vector<float>& input, DspCore::Params params)
{
    DspCore core;
    core.prepare (kSampleRate, 512, 1);
    core.setParams (params);

    auto out = input;

    for (size_t at = 0; at < out.size(); at += 512)
    {
        auto* p = out.data() + at;
        core.process (&p, 1, (int) std::min<size_t> (512, out.size() - at));
    }

    return out;
}

/** Runs an L/R pair through a fresh core and returns { outL, outR }. */
std::pair<std::vector<float>, std::vector<float>> renderStereo (
    const std::vector<float>& l, const std::vector<float>& r, DspCore::Params params)
{
    DspCore core;
    core.prepare (kSampleRate, 512, 2);
    core.setParams (params);

    auto outL = l, outR = r;

    for (size_t at = 0; at < outL.size(); at += 512)
    {
        const auto n = (int) std::min<size_t> (512, outL.size() - at);
        float* channels[2] { outL.data() + at, outR.data() + at };
        core.process (channels, 2, n);
    }

    return { outL, outR };
}

double rms (const std::vector<float>& v, size_t from = 0)
{
    double sum = 0.0;
    for (size_t i = from; i < v.size(); ++i) sum += (double) v[i] * v[i];
    return std::sqrt (sum / (double) (v.size() - from));
}

/** Amplitude of the `harmonic`-th harmonic of `hz`, by direct correlation
    against that one frequency rather than a whole FFT -- one bin is all
    these tests need. Callers must hand it a whole number of cycles from
    `from` to the end, or the answer leaks across bins. */
double harmonicMagnitude (const std::vector<float>& v, double hz, int harmonic, size_t from)
{
    double re = 0.0, im = 0.0;
    const auto n = v.size() - from;

    for (size_t i = 0; i < n; ++i)
    {
        const auto phase = 2.0 * kPi * hz * (double) harmonic * (double) i / kSampleRate;
        re += (double) v[from + i] * std::cos (phase);
        im += (double) v[from + i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

/** The ratio a mode actually delivers, measured rather than assumed: two
    settled tones 10 dB apart, both well clear of the knee, read the output
    delta. Crush 50 puts the threshold at -23 dB, so -12 and -2 dBFS are
    both above the knee in either mode and neither hits the 40 dB reduction
    clamp. */
double deliveredRatio (Mode mode, float crushPercent)
{
    DspCore::Params p;
    p.crushPercent = crushPercent;
    p.mode = mode;
    p.color = false;

    const auto quiet = render (sine (200.0, 4.0, 0.2512), p);   // -12 dBFS
    const auto loud  = render (sine (200.0, 4.0, 0.7943), p);   //  -2 dBFS

    const auto from = (size_t) (kSampleRate * 3.0);
    const auto deltaDb = 20.0 * std::log10 (rms (loud, from) / rms (quiet, from));

    return 10.0 / deltaDb;
}

//==============================================================================
/** Well under Tele's Crush 0 threshold (-8 dB, 16 dB knee -- nothing engages
    below -16 dB), a quiet tone should come back essentially as it went in. */
void testQuietSignalIsLeftAlone()
{
    DspCore::Params p;
    p.crushPercent = 0.0f;
    p.levelDb = 0.0f;

    const auto dry = sine (1000.0, 1.0, 0.05);   // ~ -26 dBFS
    const auto wet = render (dry, p);

    double worst = 0.0;
    for (size_t i = 4000; i < dry.size(); ++i)
        worst = std::max (worst, (double) std::abs (dry[i] - wet[i]));

    check (worst < 0.01, "a quiet tone under threshold passes essentially unchanged at Crush 0");
}

/** CRUSH only moves threshold now (ratio/knee are fixed per mode), so a loud
    tone should still be reduced more, or at least no less, as CRUSH rises. */
void testReductionRisesWithCrush()
{
    const auto dry = sine (1000.0, 1.0, 0.5);   // -6 dBFS: loud enough to engage even at Crush 0
    double previous = -1.0;

    for (float crush : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        DspCore::Params p;
        p.crushPercent = crush;

        DspCore core;
        core.prepare (kSampleRate, 512, 1);
        core.setParams (p);

        auto out = dry;
        for (size_t at = 0; at < out.size(); at += 512)
        {
            auto* pp = out.data() + at;
            core.process (&pp, 1, (int) std::min<size_t> (512, out.size() - at));
        }

        const auto reduction = core.currentGainReductionDb();
        check (reduction >= previous - 1.0e-6, "reduction at Crush " + std::to_string ((int) crush) + " is not less than the setting below it");
        previous = reduction;
    }

    check (previous > 3.0, "a loud tone is meaningfully reduced at Crush 100");
}

/** With nothing engaging (Crush 0, a quiet source, Color off), LEVEL is the
    only thing touching the signal, so it must add exactly what it says. */
void testMakeupGainIsExact()
{
    const auto dry = sine (1000.0, 1.0, 0.05);

    DspCore::Params p;
    p.crushPercent = 0.0f;
    p.levelDb = 6.0f;

    const auto wet = render (dry, p);

    double drySum = 0.0, wetSum = 0.0;
    for (size_t i = 4000; i < dry.size(); ++i)
    {
        drySum += (double) dry[i] * dry[i];
        wetSum += (double) wet[i] * wet[i];
    }

    const auto ratioDb = 10.0 * std::log10 (wetSum / drySum);
    checkNear (ratioDb, 6.0, 0.3, "Level +6 dB adds ~6 dB when nothing is being reduced");
}

/** How much reduction (dB) is still showing `silenceSeconds` after a hit of
    `loudSeconds` ends. Measuring "how much is left at a fixed checkpoint"
    rather than racing to a full-recovery threshold, because the release
    tail is long enough (into the tens of seconds at the ceiling) that a
    race could need an impractically long silence buffer to ever finish --
    a fixed checkpoint mid-decay is exactly as good a test of "did the long
    hit release slower" and doesn't depend on getting that buffer length
    right. */
float reductionAfter (double loudSeconds, double silenceSeconds, float crushPercent, Mode mode)
{
    DspCore core;
    DspCore::Params p;
    p.crushPercent = crushPercent;
    p.mode = mode;
    core.prepare (kSampleRate, 512, 1);
    core.setParams (p);

    auto signal = sine (200.0, loudSeconds, 0.9);
    const std::vector<float> silence ((size_t) (kSampleRate * silenceSeconds), 0.0f);
    signal.insert (signal.end(), silence.begin(), silence.end());

    auto reduction = 0.0f;

    for (size_t at = 0; at < signal.size(); at += 512)
    {
        auto* pp = signal.data() + at;
        const auto n = (int) std::min<size_t> (512, signal.size() - at);
        core.process (&pp, 1, n);
        reduction = core.currentGainReductionDb();
    }

    return reduction;
}

/** Reduction right when a hit ends, and again `silenceSeconds` later, from
    the same run -- so a residual can be read as a *fraction* of where it
    started, rather than an absolute dB figure. */
std::pair<float, float> reductionAtEndAndAfter (double loudSeconds, double silenceSeconds, float crushPercent, Mode mode,
                                                float amplitude = 0.178f)
{
    DspCore core;
    DspCore::Params p;
    p.crushPercent = crushPercent;
    p.mode = mode;
    core.prepare (kSampleRate, 512, 1);
    core.setParams (p);

    // 0.178 is a -18 dBFS RMS sine: the level a track actually arrives at,
    // per the house convention. The default used to be 0.9, which is a mix
    // bus slammed, and every release figure read off it was a figure about
    // behaviour at 14 dB hotter than anything real.
    auto signal = sine (200.0, loudSeconds, amplitude);
    const std::vector<float> silence ((size_t) (kSampleRate * silenceSeconds), 0.0f);
    signal.insert (signal.end(), silence.begin(), silence.end());

    const auto loudSamples = (size_t) (loudSeconds * kSampleRate);
    auto atEnd = 0.0f, atCheckpoint = 0.0f;

    for (size_t at = 0; at < signal.size(); at += 512)
    {
        auto* pp = signal.data() + at;
        const auto n = (int) std::min<size_t> (512, signal.size() - at);
        core.process (&pp, 1, n);
        const auto reduction = core.currentGainReductionDb();

        if (at < loudSamples && at + (size_t) n >= loudSamples)
            atEnd = reduction;

        atCheckpoint = reduction;
    }

    return { atEnd, atCheckpoint };
}

/** The whole point of the dosage-dependent release: a long, heavy hit still
    shows more reduction a fixed few seconds later than a short one does, in
    both modes -- though not by the same margin, since the two modes don't
    use the same depth of model on purpose (see Detector.h). Tele's release
    tau is itself a continuous, growing function of accumulated drive, so a
    2.5s hit and a 0.2s hit land on genuinely different taus (a wide spread
    -- roughly 1.9s vs 8.9s by hand -- so the gap is easily seconds of
    residual reduction). Stressed's simpler single charge-blend only shifts
    *how much* of one fixed floor-to-ceiling range it reaches, a smaller
    effect (tenths of a dB at this checkpoint by hand) -- correctly smaller
    given it's the intentionally simpler of the two models, not a bug. */
void testReleaseIsProgramDependent()
{
    const auto afterShortTele = reductionAfter (0.2, 3.0, 80.0f, Mode::La2a);
    const auto afterLongTele  = reductionAfter (2.5, 3.0, 80.0f, Mode::La2a);

    check (afterLongTele > afterShortTele + 0.5f,
           "Tele: 3s after the hit ends, a long one (" + std::to_string (afterLongTele)
             + " dB left) still shows more reduction than a short one (" + std::to_string (afterShortTele) + " dB left)");

    const auto afterShortStressed = reductionAfter (0.2, 3.0, 80.0f, Mode::Distressor);
    const auto afterLongStressed  = reductionAfter (2.5, 3.0, 80.0f, Mode::Distressor);

    check (afterLongStressed > afterShortStressed + 0.05f,
           "Stressed: 3s after the hit ends, a long one (" + std::to_string (afterLongStressed)
             + " dB left) still shows more reduction than a short one (" + std::to_string (afterShortStressed) + " dB left)");
}

/** A cell must give the gain back. Both bounds are absolute dB at the level a
    track actually arrives at, because the relative version of this test --
    "Stressed retains a larger fraction than Tele" -- passed for the whole of
    0.2.0 while both modes were failing to release at all.

    What it was hiding, measured against a real Distressor and a competitor
    LA-2A on the same gain-matched vocal: both references recovered
    *completely* in every phrase gap of 0.3-1.0 s. Ours recovered 63% (Tele)
    and 23% (ELD), and got worse across the take -- the first gaps recovered
    112-122%, the last five 32-47%. Ten seconds into pure digital silence,
    Stressed was still holding 26.7 dB of reduction, and the fraction test
    called that a pass because Tele was holding 9.2.

    Upper bound: three seconds of silence after a long hit must leave
    essentially nothing. Old constants left 7.41 dB (Stressed) and 1.62
    (Tele); both now land on 0.00.

    Lower bound, in the same test so neither can be satisfied alone: one
    second in, there must still be real reduction standing. Without it the
    fix for the above is just "make it a fast compressor", which would cost
    the mode the dosage memory that is the whole point of the model. */
void testReleaseGivesTheGainBack()
{
    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");

        const auto afterThree = reductionAtEndAndAfter (10.0, 3.0, 60.0f, mode).second;
        check (afterThree < 1.0f,
               name + " has released after 3s of silence (" + std::to_string (afterThree) + " dB left)");

        const auto afterOne = reductionAtEndAndAfter (10.0, 1.0, 60.0f, mode).second;
        check (afterOne > 0.4f,
               name + " is still holding reduction 1s in, so the release is slow rather than fast ("
                 + std::to_string (afterOne) + " dB left)");
    }
}

/** Stressed's fixed 10:1 ratio should catch harder than Tele's fixed 3:1 at
    the same Crush setting and input level -- the one thing CRUSH does not
    equalize between modes, on purpose (see curveForLa2a/curveForDistressor). */
void testDistressorRatioExceedsLa2a()
{
    const auto dry = sine (1000.0, 1.0, 0.5);

    const auto reductionFor = [&] (Mode mode) -> float
    {
        DspCore core;
        DspCore::Params p;
        p.crushPercent = 50.0f;
        p.mode = mode;
        core.prepare (kSampleRate, 512, 1);
        core.setParams (p);

        auto out = dry;
        for (size_t at = 0; at < out.size(); at += 512)
        {
            auto* pp = out.data() + at;
            core.process (&pp, 1, (int) std::min<size_t> (512, out.size() - at));
        }

        return core.currentGainReductionDb();
    };

    const auto tele = reductionFor (Mode::La2a);
    const auto stressed = reductionFor (Mode::Distressor);

    check (stressed > tele, "Stressed's fixed 10:1 ratio reduces more than Tele's fixed 3:1 at the same Crush");
}

/** Unlinked, two channels at genuinely different levels should compress
    independently, so a fixed level ratio between them does not survive.
    Linked, one shared cell decides the gain for both, so it does -- exactly,
    since it is literally the same multiply applied to both channels. */
void testStereoLink()
{
    const auto l = sine (1000.0, 1.5, 0.8);
    std::vector<float> r (l.size());
    for (size_t i = 0; i < l.size(); ++i) r[i] = l[i] * 0.25f;   // quieter, but proportional

    for (bool linked : { false, true })
    {
        DspCore::Params p;
        p.crushPercent = 70.0f;
        p.link = linked;

        const auto [outL, outR] = renderStereo (l, r, p);
        const auto ratio = rms (outR, l.size() / 2) / rms (outL, l.size() / 2);

        if (linked)
            checkNear (ratio, 0.25, 0.01, "linked: R stays proportional to L (same shared gain on both channels)");
        else
            check (std::abs (ratio - 0.25) > 0.02, "unlinked: R/L ratio moves away from the input's 0.25 once each channel compresses on its own");
    }
}

/** In Stressed mode, Color is a real toggle: on adds harmonic content, off
    leaves the signal alone. (Tele has no off state for Color to compare
    against -- see testTeleColorIsLocked instead.) Checked as "the waveform
    differs from a pure linear copy," not a specific harmonic number, since
    that's what the toggle is actually claiming. */
void testColorTogglesHarmonics()
{
    const auto dry = sine (300.0, 0.5, 0.6);

    DspCore::Params off;
    off.crushPercent = 0.0f;   // minimal cell action; identical in both runs either way
    off.mode = Mode::Distressor;
    off.color = false;

    DspCore::Params on = off;
    on.color = true;

    const auto wetOff = render (dry, off);
    const auto wetOn  = render (dry, on);

    double diff = 0.0;
    for (size_t i = 4000; i < dry.size(); ++i)
        diff = std::max (diff, (double) std::abs (wetOff[i] - wetOn[i]));

    check (diff > 0.01, "Stressed: Color on measurably differs from Color off");
}

/** Tele always runs its drive stage, regardless of what the Color parameter
    says -- the panel is expected to hide/disable the switch there, but the
    DSP has to hold the lock even if something else sets the parameter. */
void testTeleColorIsLocked()
{
    const auto dry = sine (300.0, 0.5, 0.6);

    DspCore::Params colorOff;
    colorOff.crushPercent = 0.0f;   // minimal cell action; identical in every run below either way
    colorOff.mode = Mode::La2a;
    colorOff.color = false;

    DspCore::Params colorOn = colorOff;
    colorOn.color = true;

    const auto wetColorOff = render (dry, colorOff);
    const auto wetColorOn  = render (dry, colorOn);

    check (wetColorOff == wetColorOn, "Tele: output is identical whether Color's parameter is off or on -- the lock ignores it");

    // And the lock isn't hiding a no-op: La2aDrive is a genuine nonlinearity
    // (checked directly, not by comparing against a different mode's
    // output, which would also differ for cell/topology reasons that have
    // nothing to do with Color) -- a fixed multiply would give the same
    // input/output gain at any level; a saturator's gain changes with it.
    La2aDrive lowShaper, highShaper;
    const auto gainLow  = lowShaper.process (0.1f) / 0.1f;
    const auto gainHigh = highShaper.process (0.9f) / 0.9f;

    check (std::abs (gainHigh - gainLow) > 0.02f,
           "La2aDrive's gain at 0.9 (" + std::to_string (gainHigh) + ") differs from its gain at 0.1 ("
             + std::to_string (gainLow) + ") -- it's a nonlinearity, not a fixed multiply");
}

/** Nothing here may produce a NaN, an infinity, or a runaway, in either
    mode, linked or not, Color on or off. */
void testStability()
{
    std::vector<float> nasty;
    for (int i = 0; i < 48000; ++i)
        nasty.push_back ((float) ((i / 64) % 2 == 0 ? 3.0 : -3.0));   // past full scale

    for (auto mode : { Mode::La2a, Mode::Distressor })
    {
        for (bool link : { false, true })
        {
            DspCore::Params p;
            p.crushPercent = 100.0f;
            p.levelDb = 24.0f;
            p.mode = mode;
            p.link = link;
            p.color = true;

            DspCore core;
            core.prepare (kSampleRate, 512, 2);
            core.setParams (p);

            auto l = nasty, r = nasty;

            for (size_t at = 0; at < l.size(); at += 512)
            {
                const auto n = (int) std::min<size_t> (512, l.size() - at);
                float* channels[2] { l.data() + at, r.data() + at };
                core.process (channels, 2, n);
            }

            for (auto v : l) check (std::isfinite (v) && std::abs (v) < 60.0f, "the output stays finite and bounded under an extreme input");
        }
    }
}

/** No lookahead, no oversampling: this module must never cost the host a
    sample of plugin-delay-compensation. */
void testLatencyIsAlwaysZero()
{
    OptoDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    const float values[] { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    check (dsp.latencyForParams (values, 5) == 0, "Crush 0 reports zero latency");

    const float loud[] { 100.0f, 24.0f, 1.0f, 1.0f, 1.0f };
    check (dsp.latencyForParams (loud, 5) == 0, "Crush 100, Stressed, Link, Color also reports zero latency");
}

/** The same programme gives the same reduction at every sample rate.

    Every constant in the cells is a time, turned into a coefficient from the
    rate in use, and until this test nothing checked that at any rate but
    48 kHz. A constant written as a per-sample figure would pass everything
    here and run twice as fast at 96 kHz; that is what this is for. The DC
    blocker in the drive stage did exactly that until 0.2.0.

    Three readings per rate, both modes, deep: the reduction the programme
    settles to, the reduction at the end of a 100 ms passage 18 dB hotter,
    and what is left a second later. Each within 0.1 dB of 48 kHz. */
void testReductionIsTheSameAtEverySampleRate()
{
    const auto readings = [] (double rate, Mode mode)
    {
        const auto block = (int) std::lround (rate / 1000.0);   // 1 ms
        const auto n = (size_t) (7.0 * rate);
        std::vector<float> signal (n);

        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / rate;
            const auto amplitude = t >= 4.0 && t < 4.1 ? 0.178 * 7.943282 : 0.178;
            signal[i] = (float) (amplitude * std::sin (2.0 * kPi * 220.0 * t));
        }

        DspCore core;
        DspCore::Params p;
        p.crushPercent = 100.0f;
        p.mode = mode;
        core.prepare (rate, block, 1);
        core.setParams (p);

        std::vector<float> trace;

        for (size_t at = 0; at < n; at += (size_t) block)
        {
            auto* pp = signal.data() + at;
            core.process (&pp, 1, (int) std::min<size_t> ((size_t) block, n - at));
            trace.push_back (core.currentGainReductionDb());
        }

        const auto mean = [&trace] (size_t fromMs, size_t count)
        {
            double sum = 0.0;
            for (size_t i = fromMs; i < fromMs + count; ++i) sum += trace[i];
            return sum / (double) count;
        };

        return std::array<double, 3> { mean (3500, 500), mean (4090, 10), mean (5100, 10) };
    };

    const char* const what[] { "settled reduction", "reduction at the end of a loud passage", "reduction a second after it" };

    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");
        const auto reference = readings (48000.0, mode);

        check (reference[0] > 10.0 && reference[1] > reference[0] + 3.0,
               name + " at 48 kHz is reducing, and reducing more in the loud passage");

        for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        {
            const auto here = readings (rate, mode);

            for (size_t k = 0; k < 3; ++k)
                checkNear (here[k], reference[k], 0.1,
                           name + " at " + std::to_string ((int) rate) + " Hz, " + what[k] + ", against 48 kHz");
        }
    }
}

//==============================================================================
/** Each mode delivers the ratio it claims -- measured, not assumed.

    Added in 0.2.0 because testDistressorRatioExceedsLa2a only ever checked
    that Stressed reduces *more* than Tele, which is true for any pair of
    numbers in the right order. It stayed true, and silent, while Tele's
    feedback loop was quietly delivering 1.67:1 against a stated 3:1: the
    curve was being handed the feedforward slope figure. See
    feedbackSlope() in Detector.h. */
void testDeliveredRatioMatchesTheSpec()
{
    checkNear (deliveredRatio (Mode::La2a, 50.0f), 3.0, 0.4,
               "Tele delivers ~3:1 through its feedback loop");
    checkNear (deliveredRatio (Mode::Distressor, 50.0f), 10.0, 1.0,
               "Stressed delivers ~10:1 feedforward");
}

/** Tele's drive stage is asymmetric, so it must generate a real 2nd
    harmonic -- the even-order warmth of a single-ended tube stage, which is
    the whole reason that term exists.

    Added in 0.2.0 because testColorTogglesHarmonics only checks that Color
    on differs from Color off, and an odd-harmonic stage differs from no
    stage just as well as an even-harmonic one does. That let a sgn() factor
    sit in La2aDrive making the whole function odd -- so it produced no even
    harmonics whatsoever while its comment claimed the opposite. */
void testTeleDriveProducesEvenHarmonics()
{
    La2aDrive drive;
    drive.prepare (kSampleRate);

    const auto dry = sine (200.0, 1.0, 0.5);
    std::vector<float> wet (dry.size());

    for (size_t i = 0; i < dry.size(); ++i)
        wet[i] = drive.process (dry[i]);

    // From 0.25 s: past the DC blocker's own settling, and exactly 150
    // whole cycles of 200 Hz remain, so the correlation doesn't leak.
    const auto from = (size_t) (kSampleRate * 0.25);
    const auto fundamental = harmonicMagnitude (wet, 200.0, 1, from);
    const auto second      = harmonicMagnitude (wet, 200.0, 2, from);
    const auto third       = harmonicMagnitude (wet, 200.0, 3, from);

    check (second > 0.02 * fundamental,
           "Tele's drive generates a real 2nd harmonic, not just odd-order clip");
    check (second > third,
           "Tele's drive is even-harmonic dominant -- the asymmetry is the point");
}

/** What Crush 0 does at both ends of the gain-staging range.

    An unprocessed track reaching a compressor should sit around -18 dBFS
    RMS and peak around -12 dBFS -- the same 0 VU = -18 dBFS the meters are
    calibrated to. At that level Crush 0 is effectively transparent: the
    knee opens at -16 dBFS, so only peaks reach into it at all, and the
    feedback loop holds what they earn under a dB. That is what "should load
    doing nothing" was actually asking for, and it already held.

    It is still not a bypass. Hand it a mastering-bus level and the knee is
    genuinely open. Both halves are pinned because the distinction between
    them is the whole answer: a review pass in 0.2.0 measured only the hot
    case, called -6 dBFS a realistic track level, and concluded the knob
    needed its threshold sweep moved. It did not -- -6 dBFS is roughly where
    a whole mix sits going into mastering, not where one source sits going
    into a compressor. */
void testCrushZeroAtProperGainStaging()
{
    DspCore::Params p;
    p.crushPercent = 0.0f;

    const auto reductionFor = [&p] (double amplitude)
    {
        const auto dry = sine (200.0, 2.0, amplitude);
        const auto wet = render (dry, p);
        const auto from = (size_t) (kSampleRate * 1.5);
        return -20.0 * std::log10 (rms (wet, from) / rms (dry, from));
    };

    const auto atTrackLevel = reductionFor (0.2512);   // peaks at -12 dBFS
    const auto atBusLevel   = reductionFor (0.5012);   // peaks at  -6 dBFS

    check (atTrackLevel < 1.0,
           "Crush 0 is transparent on a correctly staged track, peaks at -12 dBFS (measured "
             + std::to_string (atTrackLevel) + " dB of reduction)");
    check (atBusLevel > 1.0,
           "Crush 0 is still not a bypass -- a hot source does reach the knee (measured "
             + std::to_string (atBusLevel) + " dB)");
}

//==============================================================================
// A spike on top of programme. Everything above reads the release across a
// gap into silence; these read it where the programme carries on underneath,
// which is the case the gap tests cannot see: with 15 to 20 dB already
// standing, the charge sits at its ceiling and anything a spike adds used to
// inherit the slowest release the cell has.

constexpr double kProgrammeHz  = 220.0;
constexpr double kProgrammeAmp = 0.17825;   // -18 dBFS RMS, the level a track arrives at
constexpr double kPrerollSec   = 15.0;      // long enough for the charge and the dosage to settle

/** The programme tone, `burstDb` hotter for `burstSec` from each onset. The
    phase runs straight through, so a burst is a change of level and nothing
    else. */
std::vector<float> programmeWithBursts (double seconds, double burstDb, double burstSec,
                                        const std::vector<double>& onsets)
{
    const auto n = (size_t) (seconds * kSampleRate);
    const auto hot = std::pow (10.0, burstDb / 20.0);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / kSampleRate;
        auto amplitude = kProgrammeAmp;

        for (const auto onset : onsets)
            if (t >= onset && t < onset + burstSec)
                amplitude *= hot;

        out[i] = (float) (amplitude * std::sin (2.0 * kPi * kProgrammeHz * t));
    }

    return out;
}

/** Reduction against time, one figure per millisecond: the core run in 1 ms
    blocks and its meter read after each. */
std::vector<float> reductionTrace (const std::vector<float>& input, Mode mode, float crushPercent)
{
    constexpr int block = 48;

    DspCore core;
    DspCore::Params p;
    p.crushPercent = crushPercent;
    p.mode = mode;
    core.prepare (kSampleRate, block, 1);
    core.setParams (p);

    auto signal = input;
    std::vector<float> trace;
    trace.reserve (signal.size() / block + 1);

    for (size_t at = 0; at < signal.size(); at += block)
    {
        auto* pp = signal.data() + at;
        core.process (&pp, 1, (int) std::min<size_t> (block, signal.size() - at));
        trace.push_back (core.currentGainReductionDb());
    }

    return trace;
}

/** Mean of the trace over `lengthSec` starting at `atSec`. */
double meanReduction (const std::vector<float>& trace, double atSec, double lengthSec)
{
    const auto from = (size_t) std::llround (atSec * 1000.0);
    const auto to   = std::min (trace.size(), from + (size_t) std::llround (lengthSec * 1000.0));

    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += trace[i];
    return to > from ? sum / (double) (to - from) : 0.0;
}

/** A 20 ms spike must not leave the programme turned down behind it.

    Both bounds are absolute dB above the reduction the programme was holding
    before the spike, at the deepest setting, where it measured worst. Before
    this was fixed a single 18 dB spike left 10.3 dB (Tele) and 12.4 dB
    (Stressed) of extra reduction a quarter of a second later, and 7.5 and
    10.8 dB a full second later; the level took 3.5 and 7.2 s to come back. */
void testASpikeDoesNotLeaveADip()
{
    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");
        constexpr double burstSec = 0.020;

        const auto trace = reductionTrace (programmeWithBursts (kPrerollSec + 3.0, 18.0, burstSec, { kPrerollSec }),
                                           mode, 100.0f);

        const auto before  = meanReduction (trace, kPrerollSec - 0.5, 0.5);
        const auto end     = kPrerollSec + burstSec;
        const auto quarter = meanReduction (trace, end + 0.25, 0.01) - before;
        const auto second  = meanReduction (trace, end + 1.0, 0.01) - before;

        check (before > 10.0, name + " is holding heavy reduction before the spike (" + std::to_string (before) + " dB)");
        check (quarter < 1.5,
               name + ": 250 ms after an 18 dB, 20 ms spike the programme is back within 1.5 dB ("
                 + std::to_string (quarter) + " dB of extra reduction)");
        check (second < 0.25,
               name + ": 1 s after the spike nothing of it is left (" + std::to_string (second) + " dB of extra reduction)");
    }
}

/** Spikes a second apart must not walk the programme down.

    Ten of them, the programme running between. Read just before each next
    spike, the level used to sink 7.7 dB (Tele) and 12.2 dB (Stressed) below
    where it started, because each gap gave back a third of what the spike
    before it had added, or less. */
void testRepeatedSpikesDoNotRatchet()
{
    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");

        std::vector<double> onsets;
        for (int k = 0; k < 10; ++k) onsets.push_back (kPrerollSec + k);

        const auto trace = reductionTrace (programmeWithBursts (kPrerollSec + 10.0, 18.0, 0.020, onsets), mode, 100.0f);

        const auto before = meanReduction (trace, kPrerollSec - 0.5, 0.5);
        const auto sunk   = meanReduction (trace, kPrerollSec + 9.0 - 0.01, 0.01) - before;

        check (sunk < 0.5,
               name + ": before the tenth spike the programme sits within 0.5 dB of where it started ("
                 + std::to_string (sunk) + " dB lower)");
    }
}

/** What the fix above must not cost.

    First, a level that is *held* is programme, not a spike, and its reduction
    has to arrive as quickly as it always did: the time to come within 1 dB of
    the settled figure when the programme steps up 18 dB and stays there. A
    release that speeds up on every reduction the charge has not caught up
    with, held or not, makes the envelope sag between the crests of the very
    signal that is holding it up, and that pushed Stressed's figure from 56 ms
    to 520 ms when it was tried.

    Second, a hit that lasted counts. A full second at the louder level is
    exposure, and the cell must still be holding some of it a second after the
    level drops back, or this is a fast compressor with a slow one's name. */
void testAHeldLevelIsStillProgramme()
{
    for (const auto mode : { Mode::La2a, Mode::Distressor })
    {
        const auto name = std::string (mode == Mode::La2a ? "Tele" : "Stressed");

        const auto held    = reductionTrace (programmeWithBursts (kPrerollSec + 5.0, 18.0, 5.0, { kPrerollSec }), mode, 100.0f);
        const auto settled = meanReduction (held, kPrerollSec + 4.9, 0.1);

        auto arrivedMs = -1.0;
        for (auto i = (size_t) std::llround (kPrerollSec * 1000.0); i < held.size(); ++i)
            if (held[i] >= settled - 1.0) { arrivedMs = (double) i - kPrerollSec * 1000.0; break; }

        const auto limitMs = mode == Mode::La2a ? 20.0 : 65.0;
        check (arrivedMs >= 0.0 && arrivedMs <= limitMs,
               name + ": a held 18 dB step is within 1 dB of its settled reduction in "
                 + std::to_string (arrivedMs) + " ms (limit " + std::to_string (limitMs) + ")");

        const auto hit    = reductionTrace (programmeWithBursts (kPrerollSec + 4.0, 18.0, 1.0, { kPrerollSec }), mode, 100.0f);
        const auto before = meanReduction (hit, kPrerollSec - 0.5, 0.5);
        const auto after  = meanReduction (hit, kPrerollSec + 2.0, 0.01) - before;

        check (after > 1.0,
               name + ": a second after a 1 s louder passage the cell is still holding some of it ("
                 + std::to_string (after) + " dB)");
    }
}

//==============================================================================
// The attack. Nothing pinned it before: the suite passed unchanged with the
// attack rewritten in both cells, which is how that was found out.

/** Milliseconds from a step in the programme's level to the reduction having
    covered `fraction` of the way to where it settles. */
double attackMs (Mode mode, float crushPercent, double stepDb, double fraction)
{
    const auto trace   = reductionTrace (programmeWithBursts (kPrerollSec + 3.0, stepDb, 3.0, { kPrerollSec }), mode, crushPercent);
    const auto before  = meanReduction (trace, kPrerollSec - 0.5, 0.5);
    const auto settled = meanReduction (trace, kPrerollSec + 2.9, 0.1);
    const auto target  = before + fraction * (settled - before);

    for (auto i = (size_t) std::llround (kPrerollSec * 1000.0); i < trace.size(); ++i)
        if (trace[i] >= target)
            return (double) i - kPrerollSec * 1000.0;

    return -1.0;
}

/** A small move is met at the pace it always was.

    The programme steps up 3 dB and stays. That asks each cell for well under
    6 dB more than it is giving, so the quick stage of the attack has no part
    in it, and the figures are the 10 ms attack's own as it measures on a
    220 Hz tone: 12 ms for Tele, whose loop shortens it, and 30 ms for
    Stressed, which also has to wait for its charge before the envelope stops
    sagging between crests. They are absolute, and they are the same before
    and after the quick stage existed. */
void testASmallStepKeepsTheTenMillisecondAttack()
{
    const auto tele     = attackMs (Mode::La2a, 60.0f, 3.0, 0.63);
    const auto stressed = attackMs (Mode::Distressor, 60.0f, 3.0, 0.63);

    check (tele >= 9.0 && tele <= 15.0,
           "Tele covers 63% of a 3 dB step in " + std::to_string (tele) + " ms (9 to 15)");
    check (stressed >= 24.0 && stressed <= 36.0,
           "Stressed covers 63% of a 3 dB step in " + std::to_string (stressed) + " ms (24 to 36)");
}

/** How far the loudest sample of an 18 dB, 20 ms spike comes out above the
    peak the output settles to when that level is held. */
double letThroughDb (Mode mode)
{
    DspCore::Params p;
    p.crushPercent = 100.0f;
    p.mode = mode;

    const auto peakBetween = [] (const std::vector<float>& v, double fromSec, double toSec)
    {
        auto peak = 0.0;
        for (auto i = (size_t) (fromSec * kSampleRate); i < std::min (v.size(), (size_t) (toSec * kSampleRate)); ++i)
            peak = std::max (peak, (double) std::abs (v[i]));
        return peak;
    };

    const auto held  = render (programmeWithBursts (kPrerollSec + 5.0, 18.0, 5.0, { kPrerollSec }), p);
    const auto spike = render (programmeWithBursts (kPrerollSec + 1.0, 18.0, 0.020, { kPrerollSec }), p);

    return 20.0 * std::log10 (peakBetween (spike, kPrerollSec, kPrerollSec + 0.020)
                                / peakBetween (held, kPrerollSec + 4.95, kPrerollSec + 5.0));
}

/** A loud spike on top of heavy reduction must not come through whole.

    With no lookahead the first crest always gets some of the way out; what
    can be held is how much. On the 10 ms attack alone the spike's peak came
    out 7.8 dB (Tele) and 13.6 dB (Stressed) above where a held level
    settles. */
void testASpikeIsCaught()
{
    const auto tele     = letThroughDb (Mode::La2a);
    const auto stressed = letThroughDb (Mode::Distressor);

    check (tele < 3.5,     "Tele lets an 18 dB spike through by " + std::to_string (tele) + " dB (under 3.5)");
    check (stressed < 9.5, "Stressed lets an 18 dB spike through by " + std::to_string (stressed) + " dB (under 9.5)");
}

} // namespace

//==============================================================================
int main()
{
    testQuietSignalIsLeftAlone();
    testReductionRisesWithCrush();
    testMakeupGainIsExact();
    testReleaseIsProgramDependent();
    testReleaseGivesTheGainBack();
    testDistressorRatioExceedsLa2a();
    testDeliveredRatioMatchesTheSpec();
    testTeleDriveProducesEvenHarmonics();
    testCrushZeroAtProperGainStaging();
    testStereoLink();
    testColorTogglesHarmonics();
    testTeleColorIsLocked();
    testStability();
    testReductionIsTheSameAtEverySampleRate();
    testLatencyIsAlwaysZero();
    testASpikeDoesNotLeaveADip();
    testRepeatedSpikesDoNotRatchet();
    testAHeldLevelIsStillProgramme();
    testASmallStepKeepsTheTenMillisecondAttack();
    testASpikeIsCaught();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
