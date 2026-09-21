/*
    Offline measurement harness for BMO Defang.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/deq and tools/measure/vcomp.

    **It is registered before it is useful, and that is deliberate.** The core
    it drives is still the placeholder in modules/deesser/dsp/DspCore.h, so
    there is no detector, no curve and no timing to print yet. What it can
    answer honestly today are the two things that are true of the placeholder
    and will still be true of the finished module:

        measure_deesser latency     the reported delay at every setting, and
                                    the delay actually measured by running an
                                    impulse through the core -- which is zero
                                    everywhere, permanently
        measure_deesser constants   the internal constants v1 ships, including
                                    the fixed reference blend that is NOT a
                                    parameter, so the value being argued about
                                    is the value in the build

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
    std::printf ("usage: measure_deesser <latency|constants>\n\n"
                 "  latency     reported vs measured delay at every setting\n"
                 "  constants   the internal constants this build ships\n\n"
                 "  The detector modes -- detect, depth, timing, kappa, gates,\n"
                 "  pump, zipper, alias, bench, render, gen -- arrive with the\n"
                 "  DSP. See docs/deesser/11-integration-and-test-plan.md 5.\n");
}

} // namespace

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

    if (mode == "detect")
    {
        const auto arg = [argc, argv] (int i, double fallback)
        { return argc > i ? std::atof (argv[i]) : fallback; };

        printDetect (arg (2, 110.0), arg (3, 260.0), arg (4, 3.0));
        return 0;
    }

    printUsage();
    return mode.empty() ? 0 : 2;
}
