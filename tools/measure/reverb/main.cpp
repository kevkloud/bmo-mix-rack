/*
    Offline measurement harness for BMO Linger.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/deq and tools/measure/vcomp.

    **It was registered before it was useful, and that was deliberate.** The
    core it drives has early reflections since M2 and no tail until M3, so
    there is still no decay and no echo density to print. What it answers
    today:

        measure_reverb latency      the reported delay at every setting, and
                                    the delay actually measured by running an
                                    impulse through the core -- zero
                                    everywhere, permanently
        measure_reverb tail         the figure `tailSecondsForParams` will
                                    report once ModuleDsp has the accessor,
                                    across the schema's corners and against
                                    the 30 s ceiling
        measure_reverb taps         the ER tap table at a given SIZE, which is
                                    the one piece of the ER generator that
                                    exists -- and the numbers the panel's
                                    display is drawn from
        measure_reverb constants    the internal constants v1 ships, so the
                                    value being argued about is the value in
                                    the build
        measure_reverb bench        10 section 6's CPU budget, measured: the
                                    worst case first, then the transients.
                                    Release only; see printBench

    docs/reverb/11-integration-and-test-plan.md section 6 lists the modes this
    grows when the engine lands -- `ir t60 er density mono sweep bench` -- with
    WAVs going to the gitignored packages/reverb-listening/. **No audio is ever
    written into the tree**; twice a tool in this repository has done that.

    Two of those modes decide things nothing else can. `bench` is the only way
    the 10 section 6 budget gets a number: 60 s of noise at 48 kHz/128,
    Release, median of five, against 1.5 % of one core -- and 10 section 8's
    worst case (DENSITY at 48 taps, three diffuser stages, 192 kHz) is the
    first thing to measure, not the last. `density` prints the normalised echo
    density curve, which is how the Abel-Huang crossings at 0.3 and 0.7 get
    placed at recognisable knob positions.

    **Every recorded result names the machine it ran on (AURORA / ICE QUEEN) in
    testing-notes/.**
*/

#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace bmo::reverb;

namespace
{

constexpr double kSampleRate = 48000.0;

/** Every parameter at its schema default, in `Index` order. Built from
    `specs()` so it cannot drift from the schema. */
std::vector<float> defaults()
{
    std::vector<float> v;

    for (const auto& s : specs())
        v.push_back (s.def);

    return v;
}

/** Where an impulse comes back out, in samples: the index of the largest
    magnitude in the core's output. The reported latency has to equal this, or
    the module is lying to the host about its delay. */
int measuredDelay (const std::vector<float>& values)
{
    ReverbDsp dsp;
    dsp.prepare (kSampleRate, 1024, 2);
    dsp.setParams (values.data(), (int) values.size());

    constexpr int n = 1024;
    std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    float* channels[] { left.data(), right.data() };
    dsp.process (channels, 2, n);

    int best = 0;

    for (int i = 1; i < n; ++i)
        if (std::fabs (left[(size_t) i]) > std::fabs (left[(size_t) best]))
            best = i;

    return best;
}

void printLatency()
{
    std::printf ("latency, %g Hz\n", kSampleRate);
    std::printf ("  %-28s %8s %8s\n", "setting", "reported", "measured");

    const auto row = [] (const char* what, const std::vector<float>& v)
    {
        ReverbDsp dsp;
        dsp.prepare (kSampleRate, 1024, 2);

        std::printf ("  %-28s %8d %8d\n", what,
                     dsp.latencyForParams (v.data(), (int) v.size()),
                     measuredDelay (v));
    };

    row ("defaults", defaults());

    {
        auto v = defaults();
        v[Index::predelay] = 250.0f;
        row ("pre-delay at maximum", v);
    }

    {
        auto v = defaults();
        v[Index::decay] = 20.0f;
        v[Index::damphi] = 2.0f;
        row ("40 s of effective decay", v);
    }

    for (int t = 0; t < numTypes; ++t)
    {
        auto v = defaults();
        v[Index::type] = (float) t;
        row (kTypeNames[t], v);
    }

    std::printf ("\n  Zero everywhere, and permanently: there is no lookahead,\n"
                 "  no oversampling and no negative pre-delay (10 sections 1, 2).\n");
}

void printTail()
{
    std::printf ("tail report, seconds -- the figure 11 section 2a will wire up at M5\n");
    std::printf ("  %-34s %10s\n", "setting", "seconds");

    const auto row = [] (const char* what, const DspCore::Params& p)
    {
        std::printf ("  %-34s %10.3f\n", what, (double) DspCore::tailSecondsFor (p));
    };

    row ("defaults", DspCore::Params {});

    {
        DspCore::Params p;
        p.decaySeconds = 0.1f;
        p.sizeM = 0.5f;
        row ("shortest decay, smallest room", p);
    }

    {
        DspCore::Params p;
        p.decaySeconds = 20.0f;
        p.dampLo = 2.0f;
        p.dampHi = 2.0f;
        p.preDelayMs = 250.0f;
        p.sizeM = 80.0f;
        row ("everything at maximum (clamped)", p);
    }

    {
        DspCore::Params p;
        p.decaySeconds = 6.0f;
        p.dampLo = 0.1f;
        p.dampHi = 0.1f;
        row ("6 s, both multipliers at 0.10", p);
    }

    std::printf ("\n  preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05 s,\n"
                 "  clamped to %.0f s. The rack SUMS this over occupied slots\n"
                 "  rather than taking the maximum: slots are in series.\n",
                 (double) DspCore::kMaxTailSeconds);
}

void printTaps (float sizeM)
{
    std::printf ("ER taps at SIZE %.1f m (reference %.1f m)\n",
                 (double) sizeM, (double) kReferenceSizeM);
    std::printf ("  %3s %10s %10s %10s %8s\n", "k", "t (ms)", "gain", "dB", "pan");

    for (int i = 0; i < kNumReferenceTaps; ++i)
    {
        const auto& t = kReferenceTaps[i];
        const auto gain = tapGainAt (t, sizeM);

        std::printf ("  %3d %10.3f %10.4f %10.2f %8.2f\n", i,
                     (double) tapTimeMsAt (t, sizeM), (double) gain,
                     (double) (20.0f * std::log10 (gain)), (double) t.pan);
    }

    std::printf ("\n  span %.2f ms\n", (double) erSpanMsAt (sizeM));
    std::printf ("\n  **PLACEHOLDER GEOMETRY.** These are not the shipped tables:\n"
                 "  the real ones come offline from the image-source method and\n"
                 "  none of 11 section 6's comb, spacing or flamming rules is\n"
                 "  claimed of these. What is real is the Size law and the fact\n"
                 "  that the panel's display reads this same table.\n");
}

void printConstants()
{
    std::printf ("internal constants -- fixed at first ship, not parameters\n\n");

    std::printf ("  FDN lines                 %d\n", DspCore::kNumLines);
    std::printf ("  speed of sound            %.1f m/s\n", (double) DspCore::kSpeedOfSound);
    std::printf ("  density ramp width        %.3f      CALIBRATE\n", (double) DspCore::kRampWidth);
    std::printf ("  crossfade                 %.1f ms\n", (double) DspCore::kCrossfadeMs);
    std::printf ("  coefficient smoothing     %.1f ms\n", (double) DspCore::kSmoothingMs);
    std::printf ("  wet fade in reset()       %.1f ms\n", (double) DspCore::kBypassFadeMs);
    std::printf ("  reported tail ceiling     %.1f s\n", (double) DspCore::kMaxTailSeconds);
    std::printf ("  base ER tap count         %d\n", kNumReferenceTaps);
    std::printf ("  reference room size       %.1f m\n", (double) kReferenceSizeM);

    std::printf ("\n  **Eight lines is the live risk.** The mode-density rule scales\n"
                 "  with decay -- Sum(m_i) >= 0.15 * T60 * fs -- so eight cover Hall\n"
                 "  to about 2.9 s and Plate to barely 1 s (10 section 4). Sixteen\n"
                 "  lines is one of three ways out and takes the CPU budget with it.\n");

    std::printf ("\n  Not listed here because they do not exist yet: beta per type,\n"
                 "  the per-tap cutoff law, the eight delay times per type, the\n"
                 "  three reserved era fields. 10 section 1 names the categories a\n"
                 "  type's constant block holds and gives no numbers for any of them.\n");
}

void printSchema()
{
    std::printf ("schema -- %d parameters, %d host lanes spare in a rack slot\n\n",
                 (int) specs().size(), 32 - (int) specs().size());

    std::printf ("  %3s %-14s %-16s %10s %10s %10s %6s\n",
                 "i", "id", "name", "min", "max", "default", "log");

    for (size_t i = 0; i < specs().size(); ++i)
    {
        const auto& s = specs()[i];

        std::printf ("  %3d %-14s %-16s %10.3f %10.3f %10.3f %6s\n",
                     (int) i, s.id, s.name, (double) s.min, (double) s.max, (double) s.def,
                     s.logarithmic ? "yes" : "");
    }
}

/** 10 section 6's budget, measured: 60 s of noise through the whole module at
    block size 128, the median of five runs, printed as processing time over
    audio time. **Only a Release build's figure means anything**; a Debug one
    is printed with a warning rather than refused, because the ratio between
    rows is still worth reading.

    The first row is 10 section 8's worst case, which is what the budget is
    held against: DENSITY at 100 % so all 48 taps per channel play and all
    three diffuser stages are in, stereo, Taps mode, a Variation with two
    per-channel sets. The rows under it are the costlier *transient* states
    -- a crossfade running on every block, and DENSITY moving so the weights
    are recomputed every sample -- which a session passes through rather than
    sits in, and which are printed so nobody has to guess what they cost. */
void printBench()
{
#ifndef NDEBUG
    std::printf ("  ** DEBUG BUILD: these figures are not the budget's. Build Release. **\n\n");
#endif

    constexpr int kBlock = 128;
    constexpr double kSeconds = 60.0;
    constexpr int kRuns = 5;

    struct Row
    {
        const char* name;
        int variation;
        int mode;
        bool moveSize;
        bool moveDensity;
    };

    const Row rows[] {
        { "worst case: 48 taps, 3 stages",  2, (int) ErMode::taps,   false, false },
        { "  the same, Variation 6 (comb)", 6, (int) ErMode::taps,   false, false },
        { "  the same, Energy mode",        2, (int) ErMode::energy, false, false },
        { "  crossfading on every block",   2, (int) ErMode::taps,   true,  false },
        { "  DENSITY moving every block",   2, (int) ErMode::taps,   false, true  },
    };

    std::printf ("bench -- whole module, stereo, block %d, %.0f s of noise, median of %d\n",
                 kBlock, kSeconds, kRuns);
    std::printf ("  budget (10 section 6): <= 1.5 %% of a core at 48 kHz, <= 5 %% at 192 kHz\n\n");
    std::printf ("  %-34s %10s %10s %12s\n", "setting", "48 kHz", "192 kHz", "memory");

    for (const auto& row : rows)
    {
        double percent[2] {};
        size_t memory = 0;

        for (int r = 0; r < 2; ++r)
        {
            const auto rate = r == 0 ? 48000.0 : 192000.0;
            const auto total = (long long) (rate * kSeconds);

            // One second of fixed-seed noise, cycled. Generating it inside the
            // timed loop would charge the generator to the reverb.
            std::vector<float> noise ((size_t) rate);
            unsigned int seed = 12345u;

            for (auto& s : noise)
            {
                seed = seed * 1664525u + 1013904223u;
                s = 0.5f * ((float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f);
            }

            std::vector<double> times;

            for (int run = 0; run < kRuns; ++run)
            {
                ReverbDsp dsp;
                dsp.prepare (rate, kBlock, 2);

                auto v = defaults();
                v[Index::erdensity]   = 100.0f;
                v[Index::ervariation] = (float) row.variation;
                v[Index::ermode]      = (float) row.mode;
                v[Index::mix]         = 50.0f;
                dsp.setParams (v.data(), (int) v.size());

                memory = dsp.getCore().erEngine().memoryBytes();

                std::vector<float> left ((size_t) kBlock), right ((size_t) kBlock);
                float* channels[] { left.data(), right.data() };

                double elapsed = 0.0;
                size_t cursor = 0;
                long long block = 0;

                for (long long done = 0; done < total; done += kBlock, ++block)
                {
                    for (int i = 0; i < kBlock; ++i)
                    {
                        left[(size_t) i]  = noise[cursor];
                        right[(size_t) i] = noise[(cursor + 7919) % noise.size()];
                        cursor = (cursor + 1) % noise.size();
                    }

                    if (row.moveSize)
                    {
                        v[Index::size] = (block & 1) != 0 ? 12.0f : 12.6f;
                        dsp.setParams (v.data(), (int) v.size());
                    }

                    if (row.moveDensity)
                    {
                        v[Index::erdensity] = (block & 1) != 0 ? 100.0f : 90.0f;
                        dsp.setParams (v.data(), (int) v.size());
                    }

                    const auto t0 = std::chrono::steady_clock::now();
                    dsp.process (channels, 2, kBlock);
                    const auto t1 = std::chrono::steady_clock::now();

                    elapsed += std::chrono::duration<double> (t1 - t0).count();
                }

                times.push_back (elapsed);
            }

            std::sort (times.begin(), times.end());
            percent[r] = 100.0 * times[(size_t) kRuns / 2] / kSeconds;
        }

        std::printf ("  %-34s %9.3f%% %9.3f%% %9.1f kB\n", row.name, percent[0], percent[1],
                     (double) memory / 1024.0);
    }

    std::printf ("\n  Memory is the ER engine's at 192 kHz: the delay line and the\n"
                 "  diffuser's lines. Record every figure in testing-notes/ naming\n"
                 "  the machine.\n");
}

void usage()
{
    std::printf ("usage: measure_reverb <latency|tail|taps [size]|constants|schema|bench>\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
        return 2;
    }

    const std::string mode { argv[1] };

    if (mode == "latency")   { printLatency();   return 0; }
    if (mode == "tail")      { printTail();      return 0; }
    if (mode == "constants") { printConstants(); return 0; }
    if (mode == "schema")    { printSchema();    return 0; }
    if (mode == "bench")     { printBench();     return 0; }

    if (mode == "taps")
    {
        const auto sizeM = argc > 2 ? (float) std::atof (argv[2]) : kReferenceSizeM;
        printTaps (sizeM);
        return 0;
    }

    std::printf ("unknown mode: %s\n", mode.c_str());
    usage();
    return 2;
}
