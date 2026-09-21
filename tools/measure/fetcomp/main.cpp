/*
    Offline measurement harness for BMO FET.

    Links the DSP core directly -- no plugin host, no GUI, no JUCE -- the same
    shape as tools/measure/opto and tools/measure/vcomp.

        measure_fetcomp latency            reported vs measured delay per factor
        measure_fetcomp positions          the attack/release position -> time law
        measure_fetcomp curve  [voicing]   delivered GR and local slope per ratio
        measure_fetcomp timing [voicing]   attack overshoot table, release times
        measure_fetcomp thd    [voicing]   THD and H2/H3 vs level at 10/20/30 dB GR
        measure_fetcomp alias  [voicing]   the alias floor per factor per rate
        measure_fetcomp aliasorigin [voicing] which harmonic is in the image bin
        measure_fetcomp slam   [voicing]   LF ripple H3 against the release detents
        measure_fetcomp allbuttons         the plateau, the standing bias, the lag
        measure_fetcomp bench  [voicing]   ns/sample, for the CPU budget

    `voicing` is `blue` or `black`; Black is the default, as the parameter is.

    **Every recorded result names the machine it ran on (AURORA / ICE QUEEN) in
    testing-notes/.** The numbers this prints are measurements of *this code*,
    not of any hardware: everything docs/1176-comp/10-dsp-spec.md marks
    CALIBRATE is a first-pass value in modules/fetcomp/dsp/Calibration.h and
    has not been heard.
*/

#include "modules/fetcomp/dsp/DspCore.h"
#include "tools/measure/Wav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bmo::fetcomp;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

std::vector<float> sine (double hz, double seconds, double amplitude, double rate)
{
    const auto n = (size_t) (seconds * rate);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / rate));

    return out;
}

/** Runs a buffer through a prepared core in blocks and returns the left
    channel. Both channels get the same signal, so the shared detector is
    reading what a mono source would give it. */
std::vector<float> render (DspCore& core, const std::vector<float>& source, int block = 512)
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

/** One bin, by Goertzel, over a window of the output. */
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

double peakOver (const std::vector<float>& v, size_t from)
{
    double worst = 0.0;

    for (size_t i = from; i < v.size(); ++i)
        worst = std::max (worst, (double) std::abs (v[i]));

    return worst;
}

double dbOf (double linear) { return 20.0 * std::log10 (std::max (linear, 1.0e-12)); }

Voicing voicingFrom (const std::string& s)
{
    return s == "blue" ? Voicing::blue : Voicing::black;
}

const char* nameOf (Voicing v) { return v == Voicing::blue ? "Blue" : "Black"; }

/** The delivered reduction at one source level, after everything has settled.
    2 s of tone, read over the last 200 ms -- comfortably more than ten times
    the slowest release this is ever run at here. */
double deliveredGrDb (DspCore::Params p, double sourceDb, double rate = kSampleRate,
                      double toneHz = 1000.0, double seconds = 2.0)
{
    const auto amplitude = std::pow (10.0, sourceDb / 20.0);
    auto core = prepared (p, rate);
    const auto out = render (core, sine (toneHz, seconds, amplitude, rate));
    const auto from = (size_t) ((seconds - 0.2) * rate);

    // Reduction is measured across the **cell**, so the INPUT drive counts as
    // signal arriving at it and not as compression. Leaving it out is how the
    // first version of this tool reported a flat slope at every depth.
    return sourceDb + (double) p.inputDb - dbOf (peakOver (out, from));
}

/** The `input` gain that lands on a target reduction, by bisection. The loop
    is monotone in drive over this range, which is what makes a bisection
    legitimate rather than merely convenient. */
float driveForGr (DspCore::Params p, double targetGrDb, double sourceDb = -18.0,
                  double rate = kSampleRate)
{
    auto low = -20.0, high = 60.0;

    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5 * (low + high);
        p.inputDb = (float) mid;

        if (deliveredGrDb (p, sourceDb, rate) < targetGrDb)
            low = mid;
        else
            high = mid;
    }

    return (float) (0.5 * (low + high));
}

//==============================================================================
int latency()
{
    std::printf ("%-8s %-10s %-10s\n", "factor", "reported", "measured");

    auto failures = 0;

    for (const auto factor : { 1, 2, 4 })
    {
        DspCore::Params p;
        p.oversampling = factor;
        auto core = prepared (p);

        std::vector<float> impulse (1024, 0.0f);
        impulse[0] = 0.02f;                  // below threshold: a delay, not a slam

        const auto out = render (core, impulse);

        int best = 0;
        auto peak = 0.0f;

        for (int n = 0; n < (int) out.size(); ++n)
            if (std::abs (out[(size_t) n]) > peak)
            {
                peak = std::abs (out[(size_t) n]);
                best = n;
            }

        const auto reported = DspCore::latencyFor (factor);
        std::printf ("%-8d %-10d %-10d%s\n", factor, reported, best,
                     reported == best ? "" : "   MISMATCH");

        if (reported != best)
            ++failures;
    }

    std::printf ("\n0 / 40 / 60 at Off / 2x / 4x, zero at the default, and the same\n"
                 "in both voicings -- docs/1176-comp/10-dsp-spec.md 11.\n");

    return failures == 0 ? 0 : 1;
}

int positions()
{
    std::printf ("%-10s %-14s %-14s %-10s\n", "position", "attack", "release", "alpha");

    for (int i = 1; i <= 7; ++i)
    {
        const auto p = (float) i;
        std::printf ("%-10d %-14.1f %-14.1f %-10.4f\n", i,
                     (double) attackMicrosecondsFor (p),
                     (double) releaseMillisecondsFor (p),
                     attackStepFor (p, kSampleRate));
    }

    std::printf ("\nMicroseconds and milliseconds. 7 is the fast end: the parameter is\n"
                 "the knob position and it runs backwards, like the hardware's does.\n"
                 "alpha is the attack one-pole's step at 48 kHz, Off.\n");

    return 0;
}

int curve (Voicing v)
{
    std::printf ("Delivered gain reduction, 1 kHz, %s, Off, attack 4 / release 1.\n"
                 "Source level in dBFS; the `input` knob is at 0.\n\n", nameOf (v));

    std::printf ("%-8s %-10s %-10s %-10s %-10s\n", "in dBFS", "4:1", "8:1", "12:1", "20:1");

    for (double level = -40.0; level <= 20.5; level += 4.0)
    {
        std::printf ("%-8.0f", level);

        for (const auto r : { Ratio::four, Ratio::eight, Ratio::twelve, Ratio::twenty })
        {
            DspCore::Params p;
            p.ratio = r;
            p.voicing = v;
            p.releasePosition = 1.0f;
            std::printf (" %-9.2f", deliveredGrDb (p, level));
        }

        std::printf ("\n");
    }

    std::printf ("\nLocal slope, d(in)/d(out) in dB, at each depth -- the sag is the\n"
                 "divider law and is defended in 10 section 12, not a fitting error.\n\n");
    std::printf ("%-8s %-10s %-10s %-10s %-10s\n", "GR dB", "4:1", "8:1", "12:1", "20:1");

    for (const auto target : { 5.0, 10.0, 15.0, 20.0, 25.0, 30.0 })
    {
        std::printf ("%-8.0f", target);

        for (const auto r : { Ratio::four, Ratio::eight, Ratio::twelve, Ratio::twenty })
        {
            DspCore::Params p;
            p.ratio = r;
            p.voicing = v;
            p.releasePosition = 1.0f;

            const auto drive = driveForGr (p, target);
            p.inputDb = drive;

            const auto a = deliveredGrDb (p, -18.5);
            const auto b = deliveredGrDb (p, -17.5);

            // out = in - GR, so the slope is 1 / (1 - dGR/din).
            const auto slope = 1.0 / std::max (1.0e-6, 1.0 - (b - a) / 1.0);
            std::printf (" %-9.2f", slope);
        }

        std::printf ("\n");
    }

    return 0;
}

/** A step into a settled level, one sample at a time, with the reduction the
    core reports after each. Block size 1 so the reported figure is that
    sample's, not a block maximum. */
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

int timing (Voicing v)
{
    std::printf ("First-sample overshoot, %s: a step to 20 dB of steady-state GR at\n"
                 "20:1, Off. 10 section 12's table, at 48 kHz, is 4.70 / 3.18 / 1.92 /\n"
                 "0.98 / 0.39 / 0.09 / 0.008 dB, and the acceptance is <= 0.05 dB at\n"
                 "position 7 with the seven figures strictly monotone.\n\n", nameOf (v));

    std::printf ("%-10s %-12s %-12s\n", "position", "44.1 kHz", "48 kHz");

    for (int i = 1; i <= 7; ++i)
    {
        std::printf ("%-10d", i);

        for (const auto rate : { 44100.0, 48000.0 })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.voicing = v;
            p.attackPosition = (float) i;
            p.releasePosition = 1.0f;
            p.inputDb = driveForGr (p, 20.0, -18.0, rate);

            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            const auto before = (size_t) (0.01 * rate);
            const auto gr = stepResponse (p, rate, amplitude, before, (size_t) (0.05 * rate));

            std::printf (" %-11.3f", gr.back() - gr[before]);
        }

        std::printf ("\n");
    }

    std::printf ("\nHow long the reduction takes to reach half its settled value, in ms\n"
                 "after the step, per factor. **This is the honest form of \"oversampling\n"
                 "must not change the timing\"**: the first-sample figure above cannot be\n"
                 "compared across factors at all, because the oversampler's own\n"
                 "linear-phase transition is 40 to 60 base samples wide -- longer than\n"
                 "every attack on the knob -- so any sample-resolution measurement of a\n"
                 "step through it is measuring the half-band filter. A symmetric filter\n"
                 "does leave the half-way crossing where it was, so that is what is\n"
                 "compared.\n\n");

    std::printf ("%-10s %-12s %-12s %-12s\n", "position", "Off ms", "2x ms", "4x ms");

    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        std::printf ("%-10.0f", (double) position);

        for (const auto factor : { 1, 2, 4 })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.voicing = v;
            p.attackPosition = position;
            p.releasePosition = 1.0f;
            p.oversampling = factor;
            p.inputDb = driveForGr (p, 20.0, -18.0, kSampleRate);

            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            const auto before = (size_t) (0.01 * kSampleRate);
            const auto gr = stepResponse (p, kSampleRate, amplitude, before,
                                          (size_t) (0.05 * kSampleRate));

            const auto half = 0.5 * gr.back();
            auto at = gr.size() - 1;

            for (size_t n = before; n < gr.size(); ++n)
                if (gr[n] >= half) { at = n; break; }

            std::printf (" %-11.3f", 1000.0 * (double) (at - before) / kSampleRate);
        }

        std::printf ("\n");
    }

    std::printf ("\nRelease, 63 %% recovery from the %.0f dB reference depth, 20:1, in ms.\n"
                 "The law says 1100 / 234.5 / 50 ms at positions 1 / 4 / 7, and the\n"
                 "figure is only honest at that depth: smoothing is in the control\n"
                 "domain, so the 63 %% point *in dB* moves with how deep it started.\n\n"
                 "The two columns are the same knob on different programme. After a\n"
                 "short burst the fast branch owns the recovery; after a sustained\n"
                 "passage the slow branch has charged and owns it instead, which is\n"
                 "the whole of the programme dependence and needs no control.\n\n",
                 kReleaseReferenceGrDb);

    std::printf ("%-10s %-12s %-12s %-12s\n", "position", "law ms", "burst ms", "sustained ms");

    for (const auto position : { 1.0f, 4.0f, 7.0f })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.voicing = v;
        p.releasePosition = position;
        p.inputDb = driveForGr (p, kReleaseReferenceGrDb);

        std::printf ("%-10.0f %-12.1f", (double) position,
                     (double) releaseMillisecondsFor (position));

        for (const auto holdSeconds : { 0.02, 4.0 })
        {
            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            auto core = prepared (p);

            const auto hold = (size_t) (holdSeconds * kSampleRate);
            auto signal = sine (1000.0, holdSeconds, amplitude, kSampleRate);
            signal.resize (hold + (size_t) (30.0 * kSampleRate), 0.0f);

            auto left = signal, right = signal;

            double measured = -1.0, startGr = 0.0;

            for (size_t n = 0; n < left.size(); n += 16)
            {
                const auto count = (int) std::min ((size_t) 16, left.size() - n);
                float* channels[2] { left.data() + n, right.data() + n };
                core.process (channels, 2, count);

                if (n + (size_t) count == hold)
                    startGr = (double) core.currentGainReductionDb();

                if (n >= hold && measured < 0.0 && startGr > 0.0
                      && (double) core.currentGainReductionDb() <= 0.37 * startGr)
                    measured = 1000.0 * (double) (n - hold) / kSampleRate;
            }

            std::printf (" %-11.1f", measured);
        }

        std::printf ("\n");
    }

    return 0;
}

int thd (Voicing v)
{
    std::printf ("THD and the first two harmonics, %s, 4:1, release 1 (1.1 s),\n"
                 "1 kHz, Off. A2's published condition is 10 dB GR: < 0.5 %% for\n"
                 "Black, with the 2nd about 10 dB above the 3rd.\n\n", nameOf (v));

    std::printf ("%-10s %-10s %-10s %-10s %-10s\n", "GR dB", "THD %", "H2 dB", "H3 dB", "input dB");

    for (const auto target : { 0.0, 6.0, 10.0, 20.0, 30.0 })
    {
        DspCore::Params p;
        p.ratio = Ratio::four;
        p.voicing = v;
        p.releasePosition = 1.0f;
        p.inputDb = target > 0.0 ? driveForGr (p, target) : -20.0f;

        const auto amplitude = std::pow (10.0, -18.0 / 20.0);
        auto core = prepared (p);
        const auto out = render (core, sine (1000.0, 3.0, amplitude, kSampleRate));

        const auto from = (size_t) (2.0 * kSampleRate);
        const auto count = (size_t) (0.5 * kSampleRate);

        const auto h1 = magnitudeAt (out, 1000.0, kSampleRate, from, count);
        double sum = 0.0, h2 = 0.0, h3 = 0.0;

        for (int n = 2; n <= 10; ++n)
        {
            const auto h = magnitudeAt (out, 1000.0 * n, kSampleRate, from, count);
            sum += h * h;

            if (n == 2) h2 = h;
            if (n == 3) h3 = h;
        }

        std::printf ("%-10.0f %-10.4f %-10.1f %-10.1f %-10.1f\n", target,
                     100.0 * std::sqrt (sum) / std::max (h1, 1.0e-12),
                     dbOf (h2 / std::max (h1, 1.0e-12)),
                     dbOf (h3 / std::max (h1, 1.0e-12)),
                     (double) p.inputDb);
    }

    return 0;
}

int alias (Voicing v)
{
    std::printf ("Alias floor, %s, 20:1, fastest attack and release.\n"
                 "A tone at 0.1875*fs: its third harmonic is above Nyquist and folds\n"
                 "to 0.4375*fs, a bin no harmonic of the tone can occupy, so what is\n"
                 "measured there is aliasing and nothing else. Targets: -60 dB at\n"
                 "Off, -80 at 2x, -90 at 4x (10 section 9).\n\n", nameOf (v));

    std::printf ("%-10s %-8s %-10s %-10s %-10s\n", "rate", "factor", "10 dB GR", "20 dB GR", "30 dB GR");

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const auto factor : { 1, 2, 4 })
        {
            std::printf ("%-10.0f %-8d", rate, factor);

            for (const auto target : { 10.0, 20.0, 30.0 })
            {
                DspCore::Params p;
                p.ratio = Ratio::twenty;
                p.voicing = v;
                p.attackPosition = 7.0f;
                p.releasePosition = 7.0f;
                p.oversampling = factor;

                const auto toneHz = 0.1875 * rate;
                const auto amplitude = std::pow (10.0, -18.0 / 20.0);

                p.inputDb = driveForGr (p, target, -18.0, rate);

                auto core = prepared (p, rate);
                const auto out = render (core, sine (toneHz, 1.0, amplitude, rate));

                // 16384 samples, and the tone at exactly 0.1875*fs, so both
                // the tone and the image land on whole Goertzel bins and every
                // product of the control's ripple lands on one too. With a
                // window that does not divide evenly the fundamental's own
                // leakage sits around -80 dB and *is* what gets reported as
                // the alias floor, which is how the first run of this had 2x
                // and 4x looking no better than Off.
                const auto from = (size_t) (0.5 * rate);
                constexpr size_t count = 16384;

                const auto tone  = magnitudeAt (out, toneHz, rate, from, count);
                const auto image = magnitudeAt (out, 0.4375 * rate, rate, from, count);

                std::printf (" %-9.1f", dbOf (image / std::max (tone, 1.0e-12)));
            }

            std::printf ("\n");
        }
    }

    return 0;
}

/** Where the alias floor actually comes from.

    `alias` measures one bin and cannot say what put energy in it. This detunes
    the tone by a known delta and separates the candidates, because a harmonic
    that folds arrives displaced by k times that delta:

        tone f = (0.1875 + d) * fs, d = fs/512

        k=3  at Off/2x/4x  ->  0.4375*fs - 3d     (the third harmonic, which
                               the decimation filter is supposed to remove)
        k=13 at 2x         ->  0.4375*fs + 13d    (aliased inside the 2x domain,
                               landing BELOW base Nyquist where no decimation
                               filter can reach it)
        k=19 at 2x and 4x  ->  0.4375*fs - 19d    (the same trap one octave up)

    Every one of those lands on a whole Goertzel bin for a 16384-sample window,
    so the three are read cleanly and independently. If the floor is the third
    harmonic surviving the filter's transition band, the energy is at -3d and
    the others are empty. If it is in-domain aliasing of a non-bandlimited
    detector, the energy is at +13d and -19d, and oversampling cannot fix it --
    it only changes which harmonic lands on the bin. */
int aliasOrigin (Voicing v)
{
    constexpr double rate  = kSampleRate;
    constexpr size_t count = 16384;
    constexpr double d     = rate / 512.0;          // 32 bins of the window

    const auto toneHz = 0.1875 * rate + d;
    const auto nominal = 0.4375 * rate;

    std::printf ("Where the alias floor comes from, %s, 20:1, fastest attack and\n"
                 "release, 20 dB GR, 48 kHz. Tone detuned to %.4f Hz (0.1875*fs + fs/512)\n"
                 "so that harmonics which fold onto the image bin separate by k*delta.\n"
                 "Every figure is dB relative to the fundamental.\n\n", nameOf (v), toneHz);

    std::printf ("%-8s %-12s", "factor", "image bin");

    const int ks[] = { 3, 13, 19 };

    for (const auto k : ks)
        std::printf (" H%-2d @ %-9s", k, k == 13 ? "+13d" : (k == 3 ? "-3d" : "-19d"));

    std::printf ("\n");

    for (const auto factor : { 1, 2, 4 })
    {
        DspCore::Params p;
        p.ratio = Ratio::twenty;
        p.voicing = v;
        p.attackPosition = 7.0f;
        p.releasePosition = 7.0f;
        p.oversampling = factor;

        const auto amplitude = std::pow (10.0, -18.0 / 20.0);
        p.inputDb = driveForGr (p, 20.0, -18.0, rate);

        auto core = prepared (p, rate);
        const auto out = render (core, sine (toneHz, 2.0, amplitude, rate));

        const auto from = (size_t) (0.5 * rate);
        const auto tone = magnitudeAt (out, toneHz, rate, from, count);

        std::printf ("%-8d %-12.1f", factor, dbOf (magnitudeAt (out, nominal, rate, from, count)
                                                     / std::max (tone, 1.0e-12)));

        for (const auto k : ks)
        {
            const auto hz = (k == 13) ? nominal + 13.0 * d : nominal - (double) k * d;

            std::printf (" %-15.1f", dbOf (magnitudeAt (out, hz, rate, from, count)
                                             / std::max (tone, 1.0e-12)));
        }

        std::printf ("\n");
    }

    std::printf ("\nThe third harmonic itself, at %.1f Hz in the oversampled domain,\n"
                 "is attenuated by the decimation filter's response 1.125x above base\n"
                 "Nyquist -- measured at -41.7 dB from the filter's own coefficients.\n",
                 3.0 * toneHz);

    return 0;
}


int slam (Voicing v)
{
    std::printf ("LF ripple, %s, 20 dB GR at 20:1, release swept over the detents.\n"
                 "With a fast release the control follows the rectified envelope and\n"
                 "ripples at 2f; a gain modulated at 2f puts a third harmonic at\n"
                 "roughly half the ripple depth. 10 section 12 derives about 4.5 %% at\n"
                 "50 ms and 0.23 %% at 1.1 s, for 50 Hz. It is wanted character; this\n"
                 "exists to bound it.\n\n", nameOf (v));

    std::printf ("%-10s %-12s %-12s\n", "position", "50 Hz H3 %", "100 Hz H3 %");

    for (int i = 1; i <= 7; ++i)
    {
        std::printf ("%-10d", i);

        for (const auto toneHz : { 50.0, 100.0 })
        {
            DspCore::Params p;
            p.ratio = Ratio::twenty;
            p.voicing = v;
            p.releasePosition = (float) i;
            p.inputDb = driveForGr (p, 20.0, -18.0, kSampleRate);

            const auto amplitude = std::pow (10.0, -18.0 / 20.0);
            auto core = prepared (p);
            const auto out = render (core, sine (toneHz, 4.0, amplitude, kSampleRate));

            const auto from = (size_t) (3.0 * kSampleRate);
            const auto count = (size_t) (0.8 * kSampleRate);

            const auto h1 = magnitudeAt (out, toneHz, kSampleRate, from, count);
            const auto h3 = magnitudeAt (out, 3.0 * toneHz, kSampleRate, from, count);

            std::printf (" %-11.3f", 100.0 * h3 / std::max (h1, 1.0e-12));
        }

        std::printf ("\n");
    }

    return 0;
}

int allButtons()
{
    std::printf ("All-buttons-in: a bias state, not a fifth ratio. Everything here is\n"
                 "shape, not a number -- 10 section 13.4 records that the whole mode is\n"
                 "fitted by ear against one paper's prose.\n\n");

    {
        DspCore::Params p;
        p.ratio = Ratio::allButtons;
        auto core = prepared (p);
        const std::vector<float> silence (4096, 0.0f);
        render (core, silence);
        std::printf ("standing reduction at silence: %.2f dB (spec: 1-2 dB)\n\n",
                     (double) core.currentGainReductionDb());
    }

    std::printf ("%-10s %-12s %-12s %-12s\n", "in dBFS", "GR dB", "slope", "reported dB");

    auto previous = 0.0;

    for (double level = -30.0; level <= 20.5; level += 2.5)
    {
        DspCore::Params p;
        p.ratio = Ratio::allButtons;
        p.releasePosition = 1.0f;

        const auto amplitude = std::pow (10.0, level / 20.0);
        auto core = prepared (p);
        const auto out = render (core, sine (1000.0, 2.0, amplitude, kSampleRate));
        const auto gr = level - dbOf (peakOver (out, (size_t) (1.8 * kSampleRate)));

        std::printf ("%-10.1f %-12.2f %-12.2f %-12.2f\n", level, gr,
                     level > -30.0 ? (gr - previous) / 2.5 : 0.0,
                     (double) core.currentGainReductionDb());
        previous = gr;
    }

    std::printf ("\nThe flat top and the turn-over past it are the fitted collapse in\n"
                 "Detector.h; the law on its own is monotone and cannot produce them.\n");

    return 0;
}

int bench (Voicing v)
{
    std::printf ("ns/sample, Release build, 48 kHz, 512-sample blocks, stereo, %s.\n"
                 "Budget (11): 2.0x LTV Comp at defaults, 3.0x at the heaviest\n"
                 "setting. Run measure_ltvcomp in the same session on the same box\n"
                 "for the baseline -- a figure without one says nothing.\n\n", nameOf (v));

    std::printf ("%-28s %-12s\n", "setting", "ns/sample");

    struct Case { const char* name; Ratio ratio; int factor; float attack; };

    const Case cases[]
    {
        { "defaults (Off, 4:1)",      Ratio::four,       1, 4.0f },
        { "Off, 20:1, fastest",       Ratio::twenty,     1, 7.0f },
        { "2x, 20:1, fastest",        Ratio::twenty,     2, 7.0f },
        { "4x, all-buttons, fastest", Ratio::allButtons, 4, 7.0f },
    };

    for (const auto& c : cases)
    {
        DspCore::Params p;
        p.ratio = c.ratio;
        p.voicing = v;
        p.oversampling = c.factor;
        p.attackPosition = c.attack;
        p.inputDb = 12.0f;

        auto core = prepared (p);
        auto signal = sine (220.0, 10.0, 0.4, kSampleRate);
        auto left = signal, right = signal;

        const auto started = std::chrono::steady_clock::now();
        constexpr int kPasses = 8;

        for (int pass = 0; pass < kPasses; ++pass)
            for (size_t n = 0; n < left.size(); n += 512)
            {
                const auto count = (int) std::min ((size_t) 512, left.size() - n);
                float* channels[2] { left.data() + n, right.data() + n };
                core.process (channels, 2, count);
            }

        const auto elapsed = std::chrono::duration<double, std::nano> (
                                 std::chrono::steady_clock::now() - started).count();

        std::printf ("%-28s %-12.1f\n", c.name,
                     elapsed / (double) (kPasses * (int) left.size()));
    }

    return 0;
}


//==============================================================================
// `gen` and `render`: the two modes 11 section 3 lists that were missing, and
// the reason the listening pass had no WAVs to listen to. measure_vcomp is
// the model; the signal generators below are its, so a BMO FET render and a
// BMO LTV Comp render of the same source can be heard against each other.
//
// WAVs go to packages/fetcomp-listening/ (gitignored). **Nothing here writes
// into the repo**, and no audio is ever committed -- 11 section 3 is explicit
// that the regression object is state, not audio.
//==============================================================================

/** The suite's test voice: a harmonic stack under a plucked envelope, gated
    into phrases, normalised to -18 dBFS RMS -- the level every measurement in
    this tool uses as its source. Peaks around -3.6 dBFS, a 14.4 dB crest. */
std::vector<float> voice (double seconds)
{
    const auto samples = (size_t) (seconds * kSampleRate);
    std::vector<float> out (samples);
    double sumSquares = 0.0;

    for (size_t i = 0; i < samples; ++i)
    {
        const auto t = (double) i / kSampleRate;
        const auto beat = std::fmod (t, 0.55);
        const auto envelope = (std::fmod (t, 3.0) < 1.6 ? 1.0 : 0.0)
                            * (beat < 0.01 ? beat / 0.01 : std::exp (-(beat - 0.01) * 7.0));

        double sum = 0.0;

        for (int h = 1; h <= 120; ++h)
            sum += std::pow ((double) h, -1.4) * std::sin (2.0 * 2.0 * kPi * 75.0 * (double) h * t);

        out[i] = (float) (envelope * sum);
        sumSquares += (double) out[i] * out[i];
    }

    const auto rms = std::sqrt (sumSquares / (double) samples);
    const auto gain = rms > 0.0 ? std::pow (10.0, -18.0 / 20.0) / rms : 1.0;

    for (auto& v : out)
        v = (float) (v * gain);

    return out;
}

/** A hard transient over a sustained bed: what the attack knob is for, and
    the one signal where positions 1 and 7 should not sound alike. */
std::vector<float> transients()
{
    constexpr double total = 4.0;
    auto out = sine (60.0, total, std::pow (10.0, -30.0 / 20.0), kSampleRate);

    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto t = (double) i / kSampleRate;
        const auto beat = std::fmod (t, 0.8);

        // A click with almost no rise: 20 us of attack has to be audible on
        // this or the position law is decorative.
        const auto hit = beat < 0.0004 ? 1.0 : std::exp (-(beat - 0.0004) * 26.0);

        out[i] = (float) ((double) out[i]
                            + hit * 0.7 * std::sin (2.0 * kPi * 1800.0 * beat));
    }

    return out;
}

/** A sustained low tone that makes the release ripple audible as character
    rather than as a number -- the listening counterpart of `slam`. */
std::vector<float> lowTone()
{
    return sine (50.0, 4.0, std::pow (10.0, -18.0 / 20.0), kSampleRate);
}

int gen (int argc, char** argv)
{
    const std::string what = argc > 2 ? argv[2] : "";
    std::string outPath;

    for (int i = 2; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--out")
            outPath = argv[i + 1];

    if (outPath.empty())
    {
        std::fprintf (stderr, "gen needs --out file.wav\n");
        return 1;
    }

    std::vector<float> signal;

    if      (what == "voice")      signal = voice (6.0);
    else if (what == "transients") signal = transients();
    else if (what == "low")        signal = lowTone();
    else if (what == "sine")       signal = sine (1000.0, 2.0,
                                                  std::pow (10.0, -18.0 / 20.0), kSampleRate);
    else
    {
        std::fprintf (stderr, "gen <voice|transients|low|sine> --out file.wav\n");
        return 1;
    }

    if (! bmo::measure::writeWav (outPath, { signal }, kSampleRate))
    {
        std::fprintf (stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("wrote %s, %zu samples at %.0f Hz\n",
                 outPath.c_str(), signal.size(), kSampleRate);
    return 0;
}

/** Arbitrary WAV in, BMO FET out, every parameter a flag.

    Prints the settings and the delivered reduction alongside writing the
    file, because 11 section 3 asks that a number and a listen always be of
    the same render -- a WAV with no figures beside it is how a listening note
    ends up citing a measurement nobody can reproduce. */
int renderMode (int argc, char** argv)
{
    std::string inPath, outPath;

    const auto flag = [argc, argv] (const char* name, double fallback)
    {
        for (int i = 2; i + 1 < argc; ++i)
            if (std::string (argv[i]) == name)
                return std::atof (argv[i + 1]);

        return fallback;
    };

    for (int i = 2; i + 1 < argc; ++i)
    {
        const std::string a { argv[i] };

        if (a == "--in")  inPath  = argv[i + 1];
        if (a == "--out") outPath = argv[i + 1];
    }

    if (outPath.empty())
    {
        std::fprintf (stderr,
                      "render [--in in.wav] --out out.wav [--input db] [--output db]\n"
                      "       [--attack 1..7] [--release 1..7] [--ratio 0..4]\n"
                      "       [--mix pct] [--os 1|2|4] [--voicing blue|black]\n"
                      "Without --in, the test voice is used.\n");
        return 1;
    }

    std::vector<std::vector<float>> channels;
    auto rate = kSampleRate;

    if (! inPath.empty())
    {
        if (! bmo::measure::readWav (inPath, channels, rate))
        {
            std::fprintf (stderr, "could not read %s\n", inPath.c_str());
            return 1;
        }
    }
    else
    {
        channels = { voice (6.0) };
    }

    if (channels.empty() || channels[0].empty())
    {
        std::fprintf (stderr, "no samples in the source\n");
        return 1;
    }

    auto voicingName = std::string ("black");

    for (int i = 2; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--voicing")
            voicingName = argv[i + 1];

    DspCore::Params p;
    p.voicing         = voicingFrom (voicingName);
    p.inputDb         = (float) flag ("--input",   0.0);
    p.outputDb        = (float) flag ("--output",  0.0);
    p.attackPosition  = (float) flag ("--attack",  4.0);
    p.releasePosition = (float) flag ("--release", 4.0);
    p.mixPercent      = (float) flag ("--mix",   100.0);
    p.oversampling    = (int)   flag ("--os",      1.0);

    const auto ratioIndex = (int) flag ("--ratio", 0.0);
    p.ratio = (Ratio) std::clamp (ratioIndex, 0, 4);

    // Mono in, mono out; stereo in, both channels through the shared
    // detector, which is the whole point of there being no LINK switch.
    const auto mono = channels.size() == 1;
    auto left  = channels[0];
    auto right = mono ? channels[0] : channels[1];

    DspCore core;
    core.prepare (rate, (int) left.size(), mono ? 1 : 2);
    core.setParams (p);

    float* data[2] { left.data(), right.data() };
    core.process (data, mono ? 1 : 2, (int) left.size());

    std::vector<std::vector<float>> out;
    out.push_back (left);

    if (! mono)
        out.push_back (right);

    if (! bmo::measure::writeWav (outPath, out, rate))
    {
        std::fprintf (stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("wrote %s\n"
                 "  voicing %s, ratio index %d, input %.2f dB, output %.2f dB\n"
                 "  attack position %.2f, release position %.2f, mix %.1f %%, %dx\n"
                 "  reported reduction at the end of the render: %.2f dB\n"
                 "  rate %.0f Hz, %zu samples, %zu channel(s)\n",
                 outPath.c_str(), nameOf (p.voicing), ratioIndex,
                 (double) p.inputDb, (double) p.outputDb,
                 (double) p.attackPosition, (double) p.releasePosition,
                 (double) p.mixPercent, p.oversampling,
                 (double) core.currentGainReductionDb(),
                 rate, left.size(), out.size());

    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string mode { argc > 1 ? argv[1] : "latency" };
    const auto v = voicingFrom (argc > 2 ? argv[2] : "black");

    if (mode == "latency")    return latency();
    if (mode == "positions")  return positions();
    if (mode == "curve")      return curve (v);
    if (mode == "timing")     return timing (v);
    if (mode == "thd")        return thd (v);
    if (mode == "alias")      return alias (v);
    if (mode == "aliasorigin") return aliasOrigin (v);
    if (mode == "slam")       return slam (v);
    if (mode == "allbuttons") return allButtons();
    if (mode == "bench")      return bench (v);
    if (mode == "gen")        return gen (argc, argv);
    if (mode == "render")     return renderMode (argc, argv);

    std::fprintf (stderr, "usage: measure_fetcomp <latency|positions|curve|timing|thd|alias"
                          "|aliasorigin|slam|allbuttons|bench> [blue|black]\n");
    return 2;
}
