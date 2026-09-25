/*
    Offline measurement harness for BMO Linger.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/deq and tools/measure/vcomp.

    The core it drives plays the early reflections (M2) and no tail yet, so
    the modes are the ER's and the frame's:

        measure_reverb latency      the reported delay at every setting, and
                                    the delay actually measured by running an
                                    impulse through the core with dry audible
                                    -- zero everywhere, permanently
        measure_reverb tail         the figure `tailSecondsForParams` reports,
                                    across the schema's corners and against
                                    the 30 s ceiling
        measure_reverb taps         Room's ER tap table at a given SIZE, the
                                    numbers the panel's display is drawn from
        measure_reverb tables       the six tables re-derived from their
                                    geometry, re-seeded until they pass the
                                    audit, printed as rows for TapTables.h
        measure_reverb ir           the ER-only impulse response's taps at a
                                    type, SIZE, DENSITY and VARIATION
        measure_reverb samples      raw samples over a range, for debugging
        measure_reverb bench        10 section 6's budget: 60 s of noise,
                                    Release, median of five; the worst case
                                    (DENSITY 100 %, VARIATION 5) by default
        measure_reverb constants    the internal constants v1 ships, so the
                                    value being argued about is the value in
                                    the build

    docs/reverb/11-integration-and-test-plan.md section 6 lists the modes this
    still grows when the tail lands -- `t60 er density mono sweep` -- with
    WAVs going to the gitignored packages/reverb-listening/. **No audio is ever
    written into the tree**; twice a tool in this repository has done that.

    **Every recorded result names the machine it ran on (AURORA / ICE QUEEN) in
    testing-notes/.**
*/

#include "modules/reverb/dsp/ImageSource.h"
#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"
#include "tools/measure/Wav.h"

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
int measuredDelay (std::vector<float> values)
{
    // The dry path is what a host's delay compensation is about, so the
    // impulse is measured with dry audible: MIX at 50 %, where the
    // MIX law (`DspCore::dryGainFor`) has dry at unity. At a MIX of
    // 100 % there is no dry at all and the largest
    // sample would be an early reflection, which is the effect and not a
    // delay through the module.
    values[Index::mix] = 50.0f;

    ReverbDsp dsp;
    dsp.prepare (kSampleRate, 1024, 2);
    dsp.setParams (values.data(), (int) values.size());

    // 100 ms of silence first, so the 20 ms gain smoothing has settled and
    // the impulse meets the dry gain the setting asks for.
    constexpr int pre = 4800, n = 1024;
    std::vector<float> left ((size_t) (pre + n), 0.0f), right ((size_t) (pre + n), 0.0f);
    left[(size_t) pre]  = 1.0f;
    right[(size_t) pre] = 1.0f;

    for (int at = 0; at < pre + n; at += 1024)
    {
        float* channels[] { left.data() + at, right.data() + at };
        dsp.process (channels, 2, std::min (1024, pre + n - at));
    }

    int best = pre;

    for (int i = pre + 1; i < pre + n; ++i)
        if (std::fabs (left[(size_t) i]) > std::fabs (left[(size_t) best]))
            best = i;

    return best - pre;
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

/** The ER-only condition every 11 section 6 ER rule is written in, with the
    caller's edits on top, rendered from an impulse after 300 ms of pre-roll
    so every smoothed gain and crossfade has settled. */
struct Ir { std::vector<float> l, r; double rate; };

Ir renderIr (std::vector<float> v, double rate, float seconds, int channels = 2, int block = 512)
{
    ReverbDsp dsp;
    dsp.prepare (rate, block, channels);
    dsp.setParams (v.data(), (int) v.size());

    const auto preroll = (int) (0.3 * rate);
    const auto n       = (int) (seconds * rate);

    std::vector<float> l ((size_t) (preroll + n), 0.0f), r ((size_t) (preroll + n), 0.0f);
    l[(size_t) preroll] = 1.0f;
    r[(size_t) preroll] = 1.0f;

    for (int at = 0; at < preroll + n; at += block)
    {
        float* chans[] { l.data() + at, r.data() + at };
        dsp.setParams (v.data(), (int) v.size());
        dsp.process (chans, channels, std::min (block, preroll + n - at));
    }

    Ir ir;
    ir.rate = rate;
    ir.l.assign (l.begin() + preroll, l.end());
    ir.r.assign (r.begin() + preroll, r.end());
    return ir;
}

std::vector<float> erOnly()
{
    auto v = defaults();
    v[Index::verblevel]   = -40.0f;
    v[Index::erlevel]     = 0.0f;
    v[Index::mix]         = 100.0f;
    v[Index::output]      = 0.0f;
    v[Index::erhicut]     = 20000.0f;
    v[Index::erdensity]   = 0.0f;
    v[Index::ervariation] = 0.0f;
    return v;
}

void printIr (int type, float sizeM, float density, int variation)
{
    auto v = erOnly();
    v[Index::type]        = (float) type;
    v[Index::size]        = sizeM;
    v[Index::erdensity]   = density;
    v[Index::ervariation] = (float) variation;

    const auto ir = renderIr (v, kSampleRate, 0.8f);

    std::printf ("ER-only impulse response: %s, SIZE %.1f m, DENSITY %.0f %%, VARIATION %d, %g Hz\n",
                 kTypeNames[type], (double) sizeM, (double) density, variation, kSampleRate);
    std::printf ("  %8s %9s %10s %10s\n", "sample", "ms", "L", "R");

    int printed = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < ir.l.size(); ++i)
        peak = std::max (peak, std::max (std::abs (ir.l[i]), std::abs (ir.r[i])));

    // Local maxima of |L| + |R| above -50 dB re the peak: the taps.
    for (size_t i = 1; i + 1 < ir.l.size() && printed < 80; ++i)
    {
        const auto a = std::abs (ir.l[i]) + std::abs (ir.r[i]);
        if (a > std::abs (ir.l[i - 1]) + std::abs (ir.r[i - 1]) && a >= std::abs (ir.l[i + 1]) + std::abs (ir.r[i + 1])
             && a > peak * 0.00316f)
        {
            std::printf ("  %8d %9.3f %10.5f %10.5f\n", (int) i, (double) i / kSampleRate * 1000.0,
                         (double) ir.l[i], (double) ir.r[i]);
            ++printed;
        }
    }

    double e = 0.0;
    for (size_t i = 0; i < ir.l.size(); ++i)
        e += ir.l[i] * ir.l[i] + ir.r[i] * ir.r[i];

    std::printf ("\n  peak %.5f, energy %.6f (%.2f dB re the direct sound's two channels)\n",
                 (double) peak, e, 10.0 * std::log10 (std::max (e / 2.0, 1.0e-30)));
}

/** 10 section 6's budget, measured: 60 s of noise through the module at the
    given rate and block, wall-clock per block against the block's duration,
    **median of five runs**. `worst` is 10 section 8's worst case, DENSITY at
    48 taps and three diffuser stages, which is the first thing to measure. */
void printBench (double rate, int block, bool worst)
{
    auto v = defaults();
    v[Index::erdensity] = worst ? 100.0f : v[Index::erdensity];
    v[Index::ervariation] = worst ? 5.0f : v[Index::ervariation];

    const auto seconds = 60.0;
    const auto blocks  = (int) (seconds * rate / block);

    std::vector<float> l ((size_t) block), r ((size_t) block);
    std::uint32_t noise = 12345u;
    const auto next = [&noise] { noise = noise * 1664525u + 1013904223u; return (float) (noise >> 8) / 8388608.0f - 1.0f; };

    std::vector<double> percent;

    for (int run = 0; run < 5; ++run)
    {
        ReverbDsp dsp;
        dsp.prepare (rate, block, 2);
        dsp.setParams (v.data(), (int) v.size());

        const auto start = std::chrono::steady_clock::now();

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.25f * next();

            float* chans[] { l.data(), r.data() };
            dsp.setParams (v.data(), (int) v.size());
            dsp.process (chans, 2, block);
        }

        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        percent.push_back (100.0 * elapsed / seconds);
    }

    std::sort (percent.begin(), percent.end());

    std::printf ("bench: %s, %g Hz / %d, 60 s of noise, Release, five runs\n",
                 worst ? "worst case (DENSITY 100 %, VARIATION 5)" : "schema defaults", rate, block);
    std::printf ("  runs   %.3f  %.3f  %.3f  %.3f  %.3f  %% of one core\n",
                 percent[0], percent[1], percent[2], percent[3], percent[4]);
    std::printf ("  median %.3f %% of one core   (budget: <= 1.5 %% at 48 kHz, <= 5 %% at 192 kHz, 10 section 6)\n", percent[2]);
    std::printf ("  Wall clock on whatever machine this runs on; name it in the note.\n");
}

//== Reference measurements ====================================================
//
// Two modes for putting the installed reverbs and this one on the same
// table. `stimulus` writes the impulse file that bmo-tune-hostrender renders
// through a third-party plugin; `analyse` reads any rendered file back -- a
// plugin's, or `irwav`'s of this engine -- and prints the ER figures 11
// section 6 defines. WAVs go only where the caller points them, which is the
// gitignored packages/reverb-listening/; nothing here writes into the tree.

/** A unit impulse 100 ms in, 3 s of silence after it, stereo, so the plugin
    has run for a moment before the click and has room to ring. */
void writeStimulus (const std::string& path, double rate)
{
    const auto n = (size_t) (3.1 * rate);
    std::vector<std::vector<float>> ch (2, std::vector<float> (n, 0.0f));
    ch[0][(size_t) (0.1 * rate)] = 1.0f;
    ch[1][(size_t) (0.1 * rate)] = 1.0f;

    std::printf ("%s: %s, impulse at 100 ms, %.1f s, %g Hz\n",
                 bmo::measure::writeWav (path, ch, rate) ? "wrote" : "COULD NOT WRITE", path.c_str(), (double) n / rate, rate);
}

/** This engine's own response to the same stimulus, ER-only, to a file the
    analyser can read alongside a plugin's. */
void writeIrWav (const std::string& path, int type, float sizeM, float density, int variation)
{
    auto v = erOnly();
    v[Index::type]        = (float) type;
    v[Index::size]        = sizeM;
    v[Index::erdensity]   = density;
    v[Index::ervariation] = (float) variation;

    ReverbDsp dsp;
    dsp.prepare (kSampleRate, 512, 2);
    dsp.setParams (v.data(), (int) v.size());

    const auto n = (size_t) (3.1 * kSampleRate);
    std::vector<std::vector<float>> ch (2, std::vector<float> (n, 0.0f));
    ch[0][(size_t) (0.1 * kSampleRate)] = 1.0f;
    ch[1][(size_t) (0.1 * kSampleRate)] = 1.0f;

    for (size_t at = 0; at < n; at += 512)
    {
        float* chans[] { ch[0].data() + at, ch[1].data() + at };
        dsp.setParams (v.data(), (int) v.size());
        dsp.process (chans, 2, (int) std::min<size_t> (512, n - at));
    }

    std::printf ("%s: %s (%s, SIZE %.1f m, DENSITY %.0f %%, VARIATION %d, ER-only at 0 dB, MIX 100 %%)\n",
                 bmo::measure::writeWav (path, ch, kSampleRate) ? "wrote" : "COULD NOT WRITE", path.c_str(),
                 kTypeNames[type], (double) sizeM, (double) density, variation);
}

/** A WAV through the whole module -- faders, MIX, OUTPUT -- at the schema
    defaults plus any `id=value` overrides, in real units as the schema shows
    them, so a listening set is rendered by the code that ships and nothing
    else. Mono input is duplicated into both channels, which is what the rack
    does ahead of slot 1. 200 ms of pre-roll lets the gains settle; the file's
    own rate is used. */
void renderWav (const std::string& inPath, const std::string& outPath, const std::vector<std::string>& overrides)
{
    std::vector<std::vector<float>> in;
    double rate = 0.0;

    if (! bmo::measure::readWav (inPath, in, rate) || in.empty())
    {
        std::printf ("cannot read %s\n", inPath.c_str());
        return;
    }

    auto v = defaults();

    for (const auto& o : overrides)
    {
        const auto eq = o.find ('=');
        if (eq == std::string::npos) { std::printf ("bad override %s (want id=value)\n", o.c_str()); return; }
        const auto id = o.substr (0, eq);
        const auto value = (float) std::atof (o.c_str() + eq + 1);
        bool found = false;
        for (size_t i = 0; i < specs().size(); ++i)
            if (id == specs()[i].id) { v[i] = value; found = true; }
        if (! found) { std::printf ("no parameter id %s (see schema)\n", id.c_str()); return; }
    }

    const auto n = in[0].size();
    std::vector<std::vector<float>> ch (2, std::vector<float> (n, 0.0f));
    ch[0] = in[0];
    ch[1] = in.size() > 1 ? in[1] : in[0];

    ReverbDsp dsp;
    dsp.prepare (rate, 512, 2);
    dsp.setParams (v.data(), (int) v.size());

    std::vector<float> zl (512, 0.0f), zr (512, 0.0f);
    for (int k = 0; k < (int) (0.2 * rate) / 512; ++k)
    {
        std::fill (zl.begin(), zl.end(), 0.0f);
        std::fill (zr.begin(), zr.end(), 0.0f);
        float* z[] { zl.data(), zr.data() };
        dsp.process (z, 2, 512);
    }

    for (size_t at = 0; at < n; at += 512)
    {
        float* chans[] { ch[0].data() + at, ch[1].data() + at };
        dsp.setParams (v.data(), (int) v.size());
        dsp.process (chans, 2, (int) std::min<size_t> (512, n - at));
    }

    float peak = 0.0f;
    for (const auto& c : ch) for (const auto x : c) peak = std::max (peak, std::abs (x));

    std::printf ("%s: %s  (%zu samples at %g Hz, peak %.1f dBFS)", bmo::measure::writeWav (outPath, ch, rate) ? "wrote" : "COULD NOT WRITE",
                 outPath.c_str(), n, rate, 20.0 * std::log10 (std::max (peak, 1.0e-9f)));
    for (const auto& o : overrides) std::printf ("  %s", o.c_str());
    std::printf ("\n");
}

void analyseWav (const std::string& path)
{
    std::vector<std::vector<float>> ch;
    double rate = 0.0;

    if (! bmo::measure::readWav (path, ch, rate) || ch.empty())
    {
        std::printf ("cannot read %s\n", path.c_str());
        return;
    }

    const auto& l = ch[0];
    const auto& r = ch.size() > 1 ? ch[1] : ch[0];
    const auto n  = l.size();
    const auto ms = [rate] (size_t i) { return 1000.0 * (double) i / rate; };
    const auto at = [rate] (double t) { return (size_t) (t * 0.001 * rate); };

    // The direct sound: the largest sample in the first 300 ms. A wet-only
    // render has no direct, so its largest early sample is the first
    // reflection and every time below is relative to that instead; the
    // caller knows which it rendered.
    size_t t0 = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < std::min (n, at (300.0)); ++i)
    {
        const auto a = std::max (std::abs (l[i]), std::abs (r[i]));
        if (a > peak) { peak = a; t0 = i; }
    }

    std::printf ("%s\n  %zu samples at %g Hz, %zu channel(s); reference peak %.4f (%.1f dBFS) at %.1f ms\n",
                 path.c_str(), n, rate, ch.size(), (double) peak, 20.0 * std::log10 (std::max (peak, 1.0e-9f)), ms (t0));

    const auto energy = [&] (size_t from, size_t to)
    {
        double e = 0.0;
        for (size_t i = std::min (from, n); i < std::min (to, n); ++i)
            e += (double) l[i] * l[i] + (double) r[i] * r[i];
        return e;
    };

    // Early taps: local maxima of |L| + |R| above -40 dB re the reference,
    // at least 0.5 ms apart, in the first 120 ms after it.
    std::printf ("  early arrivals (local maxima above -40 dB re reference, first 120 ms):\n");
    std::printf ("    %8s %8s %8s %8s\n", "ms", "dB", "L", "R");
    size_t last = 0;
    int count = 0;
    for (size_t i = t0 + 1; i + 1 < std::min (n, t0 + at (120.0)) && count < 40; ++i)
    {
        const auto a = std::abs (l[i]) + std::abs (r[i]);
        if (a > std::abs (l[i - 1]) + std::abs (r[i - 1]) && a >= std::abs (l[i + 1]) + std::abs (r[i + 1])
             && a > peak * 0.01f && (last == 0 || i - last >= at (0.5)))
        {
            std::printf ("    %8.2f %8.1f %8.4f %8.4f\n", ms (i) - ms (t0), 20.0 * std::log10 (0.5 * a / peak), (double) l[i], (double) r[i]);
            last = i;
            ++count;
        }
    }

    // Energy against time, and the decay: Schroeder backward integration,
    // T60 from the -5..-35 dB fit.
    {
        const auto total = energy (t0, n);
        std::printf ("  energy: first 20 ms %.1f dB, 20-80 ms %.1f dB, 80-500 ms %.1f dB of the total after the reference\n",
                     10.0 * std::log10 (std::max (energy (t0, t0 + at (20.0)) / total, 1.0e-12)),
                     10.0 * std::log10 (std::max (energy (t0 + at (20.0), t0 + at (80.0)) / total, 1.0e-12)),
                     10.0 * std::log10 (std::max (energy (t0 + at (80.0), t0 + at (500.0)) / total, 1.0e-12)));

        std::vector<double> edc (n - t0, 0.0);
        double acc = 0.0;
        for (size_t i = n; i-- > t0;)
        {
            acc += (double) l[i] * l[i] + (double) r[i] * r[i];
            edc[i - t0] = acc;
        }

        const auto dbAt = [&] (size_t i) { return 10.0 * std::log10 (std::max (edc[i] / edc[0], 1.0e-30)); };
        size_t i5 = 0, i35 = 0;
        for (size_t i = 0; i < edc.size(); ++i) { if (i5 == 0 && dbAt (i) <= -5.0) i5 = i; if (dbAt (i) <= -35.0) { i35 = i; break; } }

        if (i35 > i5 && i5 > 0)
        {
            const auto t60 = 2.0 * (ms (i35) - ms (i5)) / 1000.0;
            std::printf ("  decay: EDC -5 dB at %.1f ms, -35 dB at %.1f ms -> T60 (T30 x 2) = %.2f s\n", ms (i5), ms (i35), t60);
        }
        else
            std::printf ("  decay: EDC never reaches -35 dB in the file\n");

        // Where the energy stops arriving: the time after which less than
        // 0.1 % of the total remains.
        for (size_t i = 0; i < edc.size(); ++i)
            if (edc[i] / edc[0] < 1.0e-3) { std::printf ("  99.9 %% of the energy has arrived by %.1f ms\n", ms (i)); break; }
    }

    // Normalised echo density, Abel-Huang, 20 ms window, on the mono sum.
    {
        std::printf ("  normalised echo density (20 ms window, mono):");
        const auto w = at (20.0);
        for (const auto centre : { 10.0, 20.0, 30.0, 50.0, 80.0, 120.0, 200.0, 350.0 })
        {
            const auto c = t0 + at (centre);
            if (c + w / 2 >= n) break;
            double s2 = 0.0;
            for (size_t i = c - w / 2; i < c + w / 2; ++i) { const auto m = 0.5 * (l[i] + r[i]); s2 += m * m; }
            const auto sigma = std::sqrt (s2 / (double) w);
            int above = 0;
            for (size_t i = c - w / 2; i < c + w / 2; ++i) if (std::abs (0.5 * (l[i] + r[i])) > sigma) ++above;
            std::printf ("  %.0f ms: %.2f", centre, (double) above / (double) w / 0.3173);
        }
        std::printf ("\n");
    }

    // L/R correlation, early and late.
    if (ch.size() > 1)
    {
        const auto gamma = [&] (size_t from, size_t to)
        {
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (size_t i = std::min (from, n); i < std::min (to, n); ++i) { lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; }
            return ll > 0.0 && rr > 0.0 ? lr / std::sqrt (ll * rr) : 0.0;
        };
        std::printf ("  L/R correlation: 0-80 ms %.3f, 80-500 ms %.3f, 0.5-2 s %.3f\n",
                     gamma (t0 + 1, t0 + at (80.0)), gamma (t0 + at (80.0), t0 + at (500.0)), gamma (t0 + at (500.0), t0 + at (2000.0)));
    }

    // Magnitude of the first 80 ms after the reference, octave-smoothed, and
    // its ripple about a straight tilt -- the same two figures the ER tests
    // print for this engine.
    {
        const auto to = std::min (n, t0 + at (80.0));
        std::vector<double> f, m;
        for (double hz = 250.0; hz <= 8000.0; hz *= 2.0)
        {
            double p = 0.0;
            for (int k = -12; k <= 12; ++k)
            {
                const auto fk = hz * std::pow (2.0, (double) k / 24.0);
                const auto w = 2.0 * 3.14159265358979 * fk / rate;
                double re = 0.0, im = 0.0;
                for (size_t i = t0; i < to; ++i) { const auto x = 0.5 * (l[i] + r[i]); re += x * std::cos (w * (double) (i - t0)); im -= x * std::sin (w * (double) (i - t0)); }
                p += re * re + im * im;
            }
            f.push_back (std::log2 (hz));
            m.push_back (10.0 * std::log10 (std::max (p / 25.0, 1.0e-24)));
        }
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (size_t i = 0; i < f.size(); ++i) { sx += f[i]; sy += m[i]; sxx += f[i] * f[i]; sxy += f[i] * m[i]; }
        const auto nn = (double) f.size();
        const auto slope = (nn * sxy - sx * sy) / (nn * sxx - sx * sx);
        const auto icept = (sy - slope * sx) / nn;
        double lo = 1.0e9, hi = -1.0e9;
        std::printf ("  first 80 ms, octave-smoothed magnitude re 1 kHz:");
        const auto ref = m[2];
        for (size_t i = 0; i < f.size(); ++i)
        {
            std::printf ("  %.0f Hz %+.1f", std::pow (2.0, f[i]), m[i] - ref);
            const auto residual = m[i] - (icept + slope * f[i]);
            lo = std::min (lo, residual); hi = std::max (hi, residual);
        }
        std::printf ("\n    tilt %.2f dB/octave, ripple about the tilt %.1f dB peak to peak\n", slope, hi - lo);
    }
}

void printTables (int maxTries)
{
    // Prints the six per-type tables as C++ rows for TapTables.h, after
    // re-seeding each geometry until 11 section 6's spacing and flamming rules
    // pass. The seed that passed is printed with the rows: a failing table is
    // re-seeded, not patched (10 section 8).
    namespace is = imagesource;

    for (int t = 0; t < numTypes; ++t)
    {
        const char* why = "";
        const auto g = is::reseeded (is::geometryFor (t), maxTries, &why);
        const auto rows = is::table (g);

        std::printf ("    // %s: beta %.2f, seed %u%s%s\n", kTypeNames[t], (double) g.beta, g.seed,
                     why[0] ? "  -- AUDIT FAILED: " : "", why);
        std::printf ("    {\n");

        for (const auto& im : rows)
            std::printf ("        { %8.3ff, %7.4ff, %6.3ff },   // order %d, %5.2f m, raw %6.1f dB\n",
                         (double) im.timeMs, (double) im.gain, (double) im.pan, im.order,
                         (double) im.distanceM, (double) (20.0f * std::log10 (im.rawGain)));

        std::printf ("    },\n");
    }
}

void usage()
{
    std::printf ("usage: measure_reverb <latency|tail|taps [size]|constants|schema|tables [tries]\n"
                 "                       |bench [rate [block [worst|default]]]|ir [type [size [density [variation]]]]\n"
                 "                       |samples [density [from [to]]]\n"
                 "                       |stimulus <out.wav> [rate]|irwav <out.wav> [type [size [density [variation]]]]\n"
                 "                       |render <in.wav> <out.wav> [id=value ...]|analyse <in.wav>...>
");
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

    if (mode == "ir")
    {
        printIr (argc > 2 ? std::atoi (argv[2]) : 0,
                 argc > 3 ? (float) std::atof (argv[3]) : kReferenceSizeM,
                 argc > 4 ? (float) std::atof (argv[4]) : 0.0f,
                 argc > 5 ? std::atoi (argv[5]) : 0);
        return 0;
    }

    if (mode == "stimulus" && argc > 2)
    {
        writeStimulus (argv[2], argc > 3 ? std::atof (argv[3]) : kSampleRate);
        return 0;
    }

    if (mode == "irwav" && argc > 2)
    {
        writeIrWav (argv[2],
                    argc > 3 ? std::atoi (argv[3]) : 0,
                    argc > 4 ? (float) std::atof (argv[4]) : kReferenceSizeM,
                    argc > 5 ? (float) std::atof (argv[5]) : 0.0f,
                    argc > 6 ? std::atoi (argv[6]) : 0);
        return 0;
    }

    if (mode == "render" && argc > 3)
    {
        std::vector<std::string> overrides;
        for (int i = 4; i < argc; ++i)
            overrides.emplace_back (argv[i]);
        renderWav (argv[2], argv[3], overrides);
        return 0;
    }

    if (mode == "analyse" && argc > 2)
    {
        for (int i = 2; i < argc; ++i)
            analyseWav (argv[i]);
        return 0;
    }

    if (mode == "bench")
    {
        printBench (argc > 2 ? std::atof (argv[2]) : 48000.0,
                    argc > 3 ? std::atoi (argv[3]) : 128,
                    argc > 4 ? std::string (argv[4]) == "worst" : true);
        return 0;
    }

    if (mode == "samples")
    {
        // Raw samples of the ER-only IR over a range: a debugging aid.
        auto v = erOnly();
        v[Index::erdensity] = argc > 2 ? (float) std::atof (argv[2]) : 0.0f;
        const auto from = argc > 3 ? std::atoi (argv[3]) : 0;
        const auto to   = argc > 4 ? std::atoi (argv[4]) : from + 40;
        const auto ir   = renderIr (v, kSampleRate, 0.5f);

        for (int i = std::max (0, from); i < std::min (to, (int) ir.l.size()); ++i)
            std::printf ("  %5d %10.6f %10.6f\n", i, (double) ir.l[(size_t) i], (double) ir.r[(size_t) i]);

        return 0;
    }

    if (mode == "tables")
    {
        printTables (argc > 2 ? std::atoi (argv[2]) : 400);
        return 0;
    }

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
