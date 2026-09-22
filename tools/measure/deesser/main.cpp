/*
    Offline measurement harness for BMO Defang.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/deq and tools/measure/vcomp.

    The core it drives is real since 2026-09-21. What it answers today:

        measure_deesser latency     the reported delay at every setting, and
                                    the delay actually measured by running an
                                    impulse through the core -- zero
                                    everywhere, permanently
        measure_deesser constants   the internal constants v1 ships, including
                                    the fixed reference blend that is NOT a
                                    parameter, so the value being argued about
                                    is the value in the build
        measure_deesser detect      a take's prominence distribution and the
                                    kProminenceRefDb it suggests -- the
                                    measurement 10 section 10.1 asks for, and
                                    the one thing that can settle that constant
                                    without three rounds of guessing
        measure_deesser gen         the synthetic take, written to a WAV, so
                                    the harness can be exercised and two
                                    machines can compare on the same file

    docs/deesser/11-integration-and-test-plan.md 5 lists the modes this grows
    when the DSP lands -- detect, depth, timing, kappa, gates, pump, zipper,
    alias, bench, render, gen -- with WAVs going to
    packages/deesser-listening/ (gitignored).

    Two of those modes decide things nothing else can. `detect` prints a take's
    **prominence distribution**, which is how `P_ref` gets fitted (10 section
    10.1) before any listening round happens. `kappa` sweeps the internal blend
    so the shipped value can be chosen by ear and argued about afterwards
    (10 section 11) -- it is the mode that answers whether ADAPT ever earns a
    control. `bench` runs beside measure_deq on the same box, because the two
    share an SVF and a detector shape: budget is 1.0x DEQ at defaults and 1.5x
    at the heaviest.

    **Every recorded result names the machine it ran on (AURORA / ICE QUEEN) in
    testing-notes/.**
*/

#include "modules/deesser/dsp/DeesserDsp.h"
#include "tools/measure/Wav.h"
#include "modules/deesser/dsp/DspCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

using namespace bmo::deesser;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

/** Where an impulse comes back out, in samples: the index of the largest
    magnitude in the core's output. The reported latency has to equal this, or
    the module is lying to the host about its delay. */
int measuredDelay (const DspCore::Params& p)
{
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    float v[Index::count];
    v[freq]   = p.freqHz;
    v[q]      = p.q;
    v[thresh] = p.threshDb;
    v[(size_t) range]  = p.rangeDb;
    v[shape]  = (float) (p.shape == Shape::highShelf ? highShelf : bell);

    dsp.setParams (v, Index::count);
    dsp.reset();

    constexpr int kLength = 512;
    std::vector<float> left ((size_t) kLength, 0.0f), right ((size_t) kLength, 0.0f);
    left[0] = right[0] = 1.0f;

    float* channels[2] { left.data(), right.data() };
    dsp.process (channels, 2, kLength);

    int best = 0;
    float peak = 0.0f;

    for (int i = 0; i < kLength; ++i)
        if (std::abs (left[i]) > peak)
        {
            peak = std::abs (left[i]);
            best = i;
        }

    return best;
}

void printLatency()
{
    std::printf ("BMO Defang -- latency, %.0f Hz\n\n", kSampleRate);
    std::printf ("  %-12s %-8s %-8s %10s %10s\n", "shape", "freq", "range", "reported", "measured");

    const float freqs[]  { 2000.0f, 6500.0f, 10000.0f };
    const float ranges[] { 1.0f, 8.0f, 18.0f };

    for (const auto sh : { Shape::bell, Shape::highShelf })
        for (const auto f : freqs)
            for (const auto r : ranges)
            {
                DspCore::Params p;
                p.freqHz  = f;
                p.rangeDb = r;
                p.shape   = sh;

                DeesserDsp dsp;
                float v[Index::count] { p.freqHz, p.q, p.threshDb, p.rangeDb,
                                        (float) (sh == Shape::highShelf ? highShelf : bell) };

                std::printf ("  %-12s %-8.0f %-8.1f %10d %10d\n",
                             sh == Shape::bell ? "Bell" : "High Shelf",
                             (double) f, (double) r,
                             dsp.latencyForParams (v, Index::count),
                             measuredDelay (p));
            }

    std::printf ("\n  Zero everywhere is the shipped figure, not a placeholder one:\n"
                 "  no lookahead and no oversampling, and latency is permanent\n"
                 "  once shipped (docs/deesser/10-dsp-spec.md 1).\n");
}

void printConstants()
{
    std::printf ("BMO Defang -- internal constants, v1\n\n");
    std::printf ("  These are fixed at first ship, invisible to the host, and\n"
                 "  free to be retuned right up until it. Every one is marked\n"
                 "  CALIBRATE in docs/deesser/10-dsp-spec.md 9.\n\n");

    std::printf ("  reference blend kappa   %6.2f   NOT a parameter in v1 (10 section 11)\n",
                 (double) DspCore::kKappa);
    std::printf ("  attack tau              %6.2f ms\n", (double) DspCore::kAttackMs);
    std::printf ("  release tau, fast       %6.1f ms\n", (double) DspCore::kReleaseFastMs);
    std::printf ("  release tau, slow       %6.1f ms  after %.0f ms over threshold\n",
                 (double) DspCore::kReleaseSlowMs, (double) DspCore::kSlowEngageMs);
    std::printf ("  slow reference tau      %6.1f ms\n", (double) DspCore::kSlowRefMs);
    std::printf ("  knee / slope            %6.1f dB / %.0f:1\n",
                 (double) DspCore::kKneeDb, (double) DspCore::kSlope);
    std::printf ("  hold / hysteresis       %6.1f ms / %.1f dB\n",
                 (double) DspCore::kHoldMs, (double) DspCore::kHysteresisDb);
    std::printf ("  gates, reference / band %6.1f / %.1f dBFS\n",
                 (double) DspCore::kRefGateDb, (double) DspCore::kBandGateDb);
    std::printf ("  reference high-pass     %6.0f Hz, 1st order\n",
                 (double) DspCore::kRefHighPassHz);
    std::printf ("  engine clamps           Q %.1f..%.0f, depth <= %.0f dB, f0 <= %.2f * Fs\n",
                 (double) DspCore::kMinQ, (double) DspCore::kMaxQ,
                 (double) DspCore::kMaxDepthDb, (double) DspCore::kMaxFreqFraction);
    std::printf ("  control interval        %6d samples, converted from ms, never ticks\n",
                 DspCore::kControlInterval);

    std::printf ("\n  kappa is the one worth arguing about: 1 compares the band with\n"
                 "  the whole signal now, 0 with the band's own last half second.\n"
                 "  Whether one fixed value serves a solo vocal, a vocal over a\n"
                 "  bright bed, cymbal bleed and a full mix alike is the listening\n"
                 "  pass's question, and the `kappa` mode is how it gets answered.\n");
}

void printUsage()
{
    std::printf ("usage: measure_deesser <mode> [args]\n\n");
    std::printf ("  latency                     reported vs measured delay, every setting\n");
    std::printf ("  constants                   the internal constants this build ships\n");
    std::printf ("  detect <file.wav> [hz] [q]  a take's prominence distribution, and the\n");
    std::printf ("                              kProminenceRefDb it suggests\n");
    std::printf ("  detect [burst] [gap] [gain] the same detector on a synthetic ess,\n");
    std::printf ("                              printed as reduction over time\n");
    std::printf ("  gen <out.wav> [seconds]     write that synthetic take to a file\n\n");
    std::printf ("  The remaining modes -- depth, timing, kappa, gates, pump, zipper,\n");
    std::printf ("  alias, bench, render -- are unwritten. See\n");
    std::printf ("  docs/deesser/11-integration-and-test-plan.md 5.\n");
}

} // namespace

/** `gen <out.wav> [seconds]`: writes the synthetic take the other modes use,
    so `detect` can be exercised without a real vocal to hand.

    It is **not** a substitute for one. A synthesised ess is band noise with an
    envelope on it; a real one is a turbulent consonant produced by a specific
    mouth, and the constants this tool exists to fit are about that. What this
    is for is checking the harness end to end, and for producing a file two
    machines can compare against exactly.

    Vowel plus sparse bursts, from a fixed seed, matching what tools/snapshot
    renders with `stimulus=ess`. */
void printGen (const std::string& path, double seconds)
{
    const auto n = (size_t) (seconds * kSampleRate);
    std::vector<std::vector<float>> channels (2, std::vector<float> (n, 0.0f));

    const auto burst = (int64_t) (0.110 * kSampleRate);
    const auto period = (int64_t) (1.300 * kSampleRate);

    uint32_t seed = 0x5EEDu;
    double xz1 = 0.0, xz2 = 0.0, yz1 = 0.0, yz2 = 0.0, phase = 0.0;

    const auto w = 2.0 * kPi * 6500.0 / kSampleRate;
    const auto alpha = std::sin (w) / 6.0;
    const auto a0 = 1.0 + alpha;
    const auto b0 = alpha / a0, b2 = -alpha / a0;
    const auto a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - alpha) / a0;

    const auto amplitude = std::pow (10.0, -18.0 / 20.0);

    for (size_t i = 0; i < n; ++i)
    {
        const auto vowel = 0.62 * std::sin (phase) + 0.30 * std::sin (2.0 * phase)
                         + 0.15 * std::sin (4.0 * phase);
        phase += 2.0 * kPi * 200.0 / kSampleRate;

        seed = seed * 1664525u + 1013904223u;
        const auto white = (double) (int32_t) (seed >> 8) / 8388608.0 - 1.0;
        const auto band = b0 * white + b2 * xz2 - a1 * yz1 - a2 * yz2;
        xz2 = xz1; xz1 = white; yz2 = yz1; yz1 = band;

        const auto at = (int64_t) i % period;
        auto env = 0.0;

        if (at < burst)
        {
            const auto through = (double) at / (double) burst;
            const auto edge = 0.12;
            env = through < edge       ? 0.5 - 0.5 * std::cos (kPi * through / edge)
                : through > 1.0 - edge ? 0.5 - 0.5 * std::cos (kPi * (1.0 - through) / edge)
                                       : 1.0;
        }

        const auto v = (float) (amplitude * (vowel + 3.0 * env * band));
        channels[0][i] = channels[1][i] = v;
    }

    if (bmo::measure::writeWav (path, channels, kSampleRate))
        std::printf ("wrote %s, %.1f s, %.0f Hz, one ess every 1.3 s\n",
                     path.c_str(), seconds, kSampleRate);
    else
        std::printf ("could not write %s\n", path.c_str());
}

/** `detect <file.wav>`: the prominence distribution of a real take, which is
    the measurement `kProminenceRefDb` is supposed to be fitted from.

    10 section 10.1 says P_ref "places 0 dB prominence at typical vocal
    balance", and typical is not something anybody can guess. This mode runs
    the detector over a take and prints where its prominence actually sits, so
    the constant is chosen from a number rather than from three rounds of
    turning THRESH and listening.

    **It reproduces the engine's detection front-end rather than sharing it**,
    and that is a real liability worth stating. Three things here must match
    `DspCore::process` or this tool measures something the module does not: the
    band filter is `detectorDesign` at the same shape, frequency and Q; the
    reference is a second-order high-pass at `kRefHighPassHz`; and both levels
    are power-summed across channels rather than mono-summed. If that wiring
    changes over there, change it here. The cross-check at the end exists to
    make a drift visible: it runs the actual engine over the same file, and
    what it reports should agree with what this predicts.

    The WAV reader is `tools/measure/Wav.h`, shared -- PCM 16/24/32 and float
    32, any channel count. */
void printDetectFile (const std::string& path, double freqHz, double qValue)
{
    std::vector<std::vector<float>> channels;
    double rate = 0.0;

    if (! bmo::measure::readWav (path, channels, rate) || channels.empty())
    {
        std::printf ("could not read %s\n", path.c_str());
        return;
    }

    const auto frames = channels.front().size();
    const auto chans = (int) std::min<size_t> (channels.size(), 2);

    std::printf ("BMO Defang -- prominence of %s\n", path.c_str());
    std::printf ("  %.0f Hz, %d of %d channels, %.2f s\n\n",
                 rate, chans, (int) channels.size(), (double) frames / rate);

    // The detector, configured exactly as DspCore configures it -- including
    // kProminenceRefDb, so the figures below are in the units THRESH is in
    // *today*. A suggestion to change that constant is therefore relative to
    // whatever it is now, which is what makes it applyable.
    Prominence::Config pc;
    pc.attackMs      = DspCore::kAttackMs;
    pc.releaseFastMs = DspCore::kReleaseFastMs;
    pc.slowRefMs     = DspCore::kSlowRefMs;
    pc.kappa         = DspCore::kKappa;
    pc.refGateDb     = DspCore::kRefGateDb;
    pc.bandGateDb    = DspCore::kBandGateDb;
    pc.referenceDb   = DspCore::kProminenceRefDb;
    pc.slowFloorDb   = DspCore::kSlowFloorDb;

    Prominence detector;
    detector.prepare (pc, rate);

    const auto band = bmo::dsp::SvfCoeffs::fromBiquad (
        detectorDesign (Shape::bell, freqHz, qValue, rate));
    const auto bandTaps = bmo::dsp::SvfTaps::of (band.g, band.k);

    const auto ref = bmo::dsp::SvfCoeffs::fromBiquad (
        detectorDesign (Shape::highShelf, DspCore::kRefHighPassHz, 0.707, rate));
    const auto refTaps = bmo::dsp::SvfTaps::of (ref.g, ref.k);

    std::vector<bmo::dsp::SvfState> bandState ((size_t) chans), refState ((size_t) chans);
    std::vector<float> values;
    values.reserve (frames);

    for (size_t i = 0; i < frames; ++i)
    {
        double bandPower = 0.0, refPower = 0.0;

        for (int c = 0; c < chans; ++c)
        {
            const auto x = (double) channels[(size_t) c][i];
            const auto b = bandState[(size_t) c].process (bandTaps, band, x);
            const auto r = refState[(size_t) c].process (refTaps, ref, x);

            bandPower += b * b;
            refPower  += r * r;
        }

        const auto inv = 1.0 / (double) chans;
        const auto p = detector.process (std::sqrt (bandPower * inv),
                                         std::sqrt (refPower * inv));

        // Gated samples are not quiet sibilance, they are "no signal worth
        // judging" -- silence, room tone, breaths. Averaging them in would
        // drag every figure below toward whatever the noise floor does.
        if (p > Prominence::kSilent)
            values.push_back ((float) p);
    }

    if (values.empty())
    {
        std::printf ("  every sample was gated -- nothing above the floors to measure\n");
        return;
    }

    std::sort (values.begin(), values.end());

    const auto at = [&values] (double fraction)
    {
        const auto i = (size_t) (fraction * (double) (values.size() - 1));
        return (double) values[i];
    };

    std::printf ("  band %.0f Hz at Q %.1f, %.1f %% of the take above the gates\n\n",
                 freqHz, q, 100.0 * (double) values.size() / (double) frames);

    std::printf ("  %10s %10s\n", "percentile", "prominence");

    for (const auto pc_ : { 5.0, 25.0, 50.0, 75.0, 90.0, 95.0, 99.0 })
        std::printf ("  %9.0f%% %9.1f dB\n", pc_, at (pc_ / 100.0));

    // **The suggestion, and it is not the median.**
    //
    // The spec says P_ref puts "typical vocal balance" at zero, and the first
    // version of this read that as the median. It is not: a vocal's prominence
    // distribution is **bimodal**, and the two humps are the point. Most of a
    // take is vowels, where the band sits far *below* the reference -- on the
    // synthetic take this was written against, everything from the 5th to the
    // 75th percentile lands within a dB of -33. The esses are the top few per
    // cent, and they are 30 dB up from that.
    //
    // Centring on the median would therefore put THRESH 0 in the middle of the
    // vowels, and the line below said so: "would act on 50 % of the take".
    // Sibilance is a few per cent of speech, so the statistic wanted is the
    // top of the distribution, not its middle.
    const auto offset = at (0.95);

    std::printf ("\n  kProminenceRefDb is %.1f; this take suggests %.1f\n",
                 (double) DspCore::kProminenceRefDb,
                 (double) DspCore::kProminenceRefDb + offset);

    // What that would mean in use, and the line is kept because it is what
    // disproved the median: if THRESH 0 would act on a third of a take, the
    // suggestion is wrong or the band is in the wrong place. Near five per
    // cent is what a fitted P_ref looks like.
    auto over = 0.0;

    for (const auto v : values)
        if ((double) v > offset)
            over += 1.0;

    std::printf ("  at THRESH 0 it would then act on %.1f %% of what is above the gates\n",
                 100.0 * over / (double) values.size());

    //== The cross-check ======================================================
    //
    // The engine, over the same file, at the same band. If the front-end here
    // has drifted from the one in DspCore, this is where it shows: a take
    // whose prominence sits far above threshold should produce reduction, and
    // one that never rises should produce none.
    DeesserDsp dsp;
    dsp.prepare (rate, 512, chans);

    std::vector<float> v;
    for (const auto& s : specs())
        v.push_back (s.def);

    // Index::, not the bare enumerators: `q` is this function's own parameter,
    // and `v[(size_t) q]` quietly indexed slot 2 with 2.5 -- writing the Q into
    // THRESH and leaving Q at its default. The cross-check printed "THRESH 2"
    // and that is the only reason it was noticed.
    v[(size_t) Index::freq] = (float) freqHz;
    v[(size_t) Index::q]    = (float) qValue;
    dsp.setParams (v.data(), (int) v.size());

    auto left = channels.front();
    auto right = channels.size() > 1 ? channels[1] : channels.front();

    auto peak = 0.0f;
    auto sum = 0.0;
    auto blocks = 0;

    for (size_t i = 0; i < frames; i += 512)
    {
        const auto count = (int) std::min<size_t> (512, frames - i);
        float* ch[2] { left.data() + i, right.data() + i };
        dsp.process (ch, chans, count);

        const auto gr = dsp.currentGainReductionDb();
        peak = std::max (peak, gr);
        sum += gr;
        ++blocks;
    }

    std::printf ("\n  the engine on this file at THRESH %.0f, RANGE %.0f:"
                 " peak %.2f dB, mean %.2f dB\n",
                 (double) v[(size_t) Index::thresh], (double) v[(size_t) Index::range],
                 (double) peak, blocks > 0 ? sum / blocks : 0.0);
}

/** `detect`: what the detector actually sees, on the same burst stimulus the
    renderer uses.

    This mode exists because a render could not be debugged from the outside.
    BMO Defang's first live render came back with the meter at rest, and there
    was no way to tell from the picture whether the detector was wrong, the
    meter was unwired, or the stimulus was simply not sibilant enough to act
    on. It prints the middle of that chain.

    It is also the mode `P_ref` gets fitted with (10 section 10.1): the
    threshold reads 0 at "typical vocal balance", and typical is a measurement
    nobody has taken yet.
*/
void printDetect (double burstMs, double periodMs, double essGain)
{
    DeesserDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);

    std::vector<float> v;
    for (const auto& s : specs())
        v.push_back (s.def);

    dsp.setParams (v.data(), (int) v.size());

    const auto seconds = 4.0;
    const auto n = (size_t) (seconds * kSampleRate);

    const auto burst  = (int64_t) (burstMs  * 1.0e-3 * kSampleRate);
    const auto period = (int64_t) (periodMs * 1.0e-3 * kSampleRate);

    // The renderer's stimulus, reproduced: same vowel, same bandpass, same
    // LCG. If these two ever drift apart, what the tool measures stops being
    // what the render shows.
    uint32_t seed = 0x5EEDu;
    double xz1 = 0.0, xz2 = 0.0, yz1 = 0.0, yz2 = 0.0, phase = 0.0;

    const auto w = 2.0 * 3.14159265358979323846 * 6500.0 / kSampleRate;
    const auto alpha = std::sin (w) / 6.0;
    const auto a0 = 1.0 + alpha;
    const auto b0 = alpha / a0, b2 = -alpha / a0;
    const auto a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - alpha) / a0;

    const auto amplitude = std::pow (10.0, -18.0 / 20.0);

    std::vector<float> left (n), right (n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto vowel = 0.62 * std::sin (phase) + 0.30 * std::sin (2.0 * phase)
                         + 0.15 * std::sin (4.0 * phase);
        phase += 2.0 * 3.14159265358979323846 * 200.0 / kSampleRate;

        seed = seed * 1664525u + 1013904223u;
        const auto white = (double) (int32_t) (seed >> 8) / 8388608.0 - 1.0;
        const auto band = b0 * white + b2 * xz2 - a1 * yz1 - a2 * yz2;
        xz2 = xz1; xz1 = white; yz2 = yz1; yz1 = band;

        const auto at = (int64_t) i % period;
        auto env = 0.0;

        if (at < burst)
        {
            const auto through = (double) at / (double) burst;
            const auto edge = 0.12;
            env = through < edge       ? 0.5 - 0.5 * std::cos (3.14159265358979323846 * through / edge)
                : through > 1.0 - edge ? 0.5 - 0.5 * std::cos (3.14159265358979323846 * (1.0 - through) / edge)
                                       : 1.0;
        }

        left[i] = right[i] = (float) (amplitude * (vowel + essGain * env * band));
    }

    // Block by block, keeping the peak reduction and where it happened.
    auto peak = 0.0f;
    size_t peakAt = 0;

    std::printf ("burst %.0f ms, period %.0f ms, ess gain %.1f, signal -18 dBFS\n",
                 burstMs, periodMs, essGain);
    std::printf ("  %8s  %10s\n", "time ms", "GR dB");

    for (size_t i = 0; i < n; i += 64)
    {
        const auto count = (int) std::min ((size_t) 64, n - i);
        float* ch[2] { left.data() + i, right.data() + i };
        dsp.process (ch, 2, count);

        const auto gr = dsp.currentGainReductionDb();

        if (gr > peak) { peak = gr; peakAt = i; }

        // One line every 20 ms over the first second, which is four bursts.
        if (i < (size_t) kSampleRate && (i % (size_t) (0.02 * kSampleRate)) < 64)
            std::printf ("  %8.0f  %10.3f\n", 1000.0 * (double) i / kSampleRate, gr);
    }

    std::printf ("\n  peak %.3f dB at %.0f ms\n", peak, 1000.0 * (double) peakAt / kSampleRate);
    std::printf ("  RANGE is %.1f dB, so that is %.0f %% of the bar\n",
                 v[(size_t) range], 100.0 * peak / v[(size_t) range]);
}

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";

    if (mode == "latency")   { printLatency();   return 0; }
    if (mode == "constants") { printConstants(); return 0; }

    if (mode == "gen")
    {
        if (argc < 3)
        {
            std::printf ("gen needs an output path\n");
            return 2;
        }

        printGen (argv[2], argc > 3 ? std::atof (argv[3]) : 6.0);
        return 0;
    }

    if (mode == "detect")
    {
        const auto arg = [argc, argv] (int i, double fallback)
        { return argc > i ? std::atof (argv[i]) : fallback; };

        // A path where a number was expected means a real take. Decided by
        // what the argument *is* rather than by a second mode name, because
        // the question -- what does the detector see -- is the same one; only
        // the material differs.
        const std::string first = argc > 2 ? argv[2] : "";
        const auto looksLikeNumber = ! first.empty()
            && first.find_first_not_of ("0123456789.+-") == std::string::npos;

        if (! first.empty() && ! looksLikeNumber)
            printDetectFile (first, arg (3, 6500.0), arg (4, 2.5));
        else
            printDetect (arg (2, 110.0), arg (3, 260.0), arg (4, 3.0));

        return 0;
    }

    printUsage();
    return mode.empty() ? 0 : 2;
}
