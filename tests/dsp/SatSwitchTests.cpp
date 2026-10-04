// BMO Saturator: what its switches do to the audio at the moment they move.
//
// SatDspTests holds what the module does once it has settled; this file holds
// the transitions -- a control changing while a signal passes -- which none of
// those tests can see, because each of them builds a fresh core, sets it once
// and measures the result.
//
// **The default run is a subset; `sat_switch_tests --long` runs every row.**
// The default keeps, in every section, the rows that failed on the code the
// section was written against, at 48 kHz and one other rate, with the
// extreme settings; ctest runs the default. Run --long before merging any
// change to modules/sat/dsp.

#include "modules/sat/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace bmo::sat;

namespace
{
    int failures = 0, checks = 0;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kAmplitude = 0.17782794;   // a -18 dBFS RMS sine's peak

    void check (bool ok, const std::string& what)
    {
        ++checks;
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    std::string rateName (double fs)
    {
        return std::to_string ((int) std::lround (fs / 100.0) / 10) + "."
             + std::to_string ((int) std::lround (fs / 100.0) % 10) + " kHz";
    }

    std::string ratioText (double r)
    {
        return std::to_string (std::round (r * 100.0) / 100.0).substr (0, 5) + "x";
    }

    std::vector<float> tone (double fs, double hz, double amplitude, double seconds, double phase = 0.0)
    {
        std::vector<float> x ((size_t) (seconds * fs));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / fs + phase));
        return x;
    }

    /** Render through one core, with the parameters chosen per block from
        the block's first sample. Both channels carry the same signal; the
        left is returned. */
    std::vector<float> render (DspCore& core, std::vector<float> signal, int block,
                               const std::function<DspCore::Params (size_t)>& paramsAt,
                               int numChannels = 1)
    {
        std::vector<float> right = signal;

        for (size_t start = 0; start < signal.size();)
        {
            const auto n = (int) std::min ((size_t) block, signal.size() - start);
            core.setParams (paramsAt (start));
            float* channels[2] { signal.data() + start, right.data() + start };
            core.process (channels, numChannels, n);
            start += (size_t) n;
        }

        return signal;
    }

    double largestStep (const std::vector<float>& y, size_t from, size_t to)
    {
        double m = 0.0;
        for (size_t i = std::max<size_t> (from, 1); i < to && i < y.size(); ++i)
            m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
        return m;
    }

    /** The bound the repository holds a switch to: the largest sample-to-
        sample step after it, against the steady signal's own largest step
        before and after, worst of four starting phases of a -18 dBFS RMS
        sine (an eighth of a cycle apart, so across half a cycle) so a switch
        cannot hide by landing where the two paths happen to agree. The
        switch lands on a block boundary, as a host's does. */
    double switchStepRatio (double fs, const DspCore::Params& a, const DspCore::Params& b,
                            double hz, int block = 512)
    {
        const auto length = (size_t) (0.8 * fs);
        const auto sw     = (size_t) (0.3 * fs) / (size_t) block * (size_t) block;

        double worst = 0.0;

        for (int k = 0; k < 4; ++k)
        {
            DspCore core;
            core.prepare (fs, block, 1, a.oversampling);
            core.setParams (a);

            const auto y = render (core, tone (fs, hz, kAmplitude, 0.8, k * kPi / 4.0), block,
                                   [&] (size_t s) { return s >= sw ? b : a; });

            const auto before = largestStep (y, sw - (size_t) (0.15 * fs), sw);
            const auto after  = largestStep (y, (size_t) (0.6 * fs), length);
            const auto during = largestStep (y, sw, sw + (size_t) (0.25 * fs));

            worst = std::max (worst, during / std::max (before, after));
        }

        return worst;
    }

    /** Complex amplitude at `hz` over [from, to), Hann-windowed. */
    std::complex<double> phasor (const std::vector<float>& x, double fs, double hz, size_t from, size_t to)
    {
        std::complex<double> acc = 0.0;
        double weights = 0.0;

        for (size_t i = from; i < to; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) (i - from) / (double) (to - from));
            const auto ph = 2.0 * kPi * hz * (double) i / fs;
            acc += w * (double) x[i] * std::complex<double> (std::cos (ph), -std::sin (ph));
            weights += w;
        }

        return 2.0 * acc / weights;
    }

    const int kFactors[] { 1, 2, 4, 8 };

    /** --long runs every row of every grid below; the default run keeps the
        subset `pick` and `keepPair` choose. scripts/build.sh tests the Debug
        build, where the whole grid took 259 s on ICE QUEEN (2026-10-03). */
    bool longRun = false;

    /** `all` under --long, `kept` by default. Every value in `kept` is in
        `all`, so the default run is a subset and asserts the same bounds. */
    template <typename T>
    std::vector<T> pick (std::initializer_list<T> all, std::initializer_list<T> kept)
    {
        return longRun ? std::vector<T> (all) : std::vector<T> (kept);
    }

    /** The oversampling changes the default run keeps: up from 1x, where
        the latency starts at zero, to the nearest and the farthest factor;
        all the way back down; and down between two factors that both have
        latency. 8x -> 1x and 1x -> 8x are the rows section 1 quoted failing
        on the unfixed code. */
    bool keepPair (int from, int to)
    {
        return longRun || (from == 1 && to == 2) || (from == 1 && to == 8)
                       || (from == 8 && to == 1) || (from == 4 && to == 2);
    }
}

int main (int argc, char** argv)
{
    longRun = argc > 1 && std::string (argv[1]) == "--long";

    //== 1. Changing the oversampling dips through zero instead of cutting ===
    // A change of oversampling changes the latency, so no blend of before
    // and after exists: the two are not aligned in time. It used to reset
    // every stage on the spot, which gave 40-70 samples of exact zero and
    // then a jump the size of the latency change, up to 16x the tone's own
    // step, at Mix 50 as well as 100. Wanted: a fade down, the change, a fade
    // up, with no run of silence beyond the bottom of the dip, nothing above
    // the 1.5x bound, the dry and wet paths still aligned once it is over,
    // and the latency reported for the new factor from the first process()
    // after the change, exactly as before.
    {
        for (double fs : pick ({ 44100.0, 48000.0, 96000.0 }, { 44100.0, 48000.0 }))
            for (int from : kFactors)
                for (int to : kFactors)
                {
                    if (from == to || ! keepPair (from, to))
                        continue;

                    for (float mix : { 100.0f, 50.0f })
                    {
                        DspCore::Params a;
                        a.oversampling = from;
                        a.mixPercent = mix;
                        DspCore::Params b = a;
                        b.oversampling = to;

                        const auto where = std::to_string (from) + "x -> " + std::to_string (to) + "x at "
                                         + rateName (fs) + ", mix " + std::to_string ((int) mix);

                        const auto ratio = switchStepRatio (fs, a, b, 1000.0);
                        check (ratio < 1.5, "oversampling " + where + " steps " + ratioText (ratio));

                        // The longest run of silence after the change. A
                        // dip touches zero at one sample, and where the
                        // tone crosses zero there too the two together stay
                        // under the threshold for a few samples. 0.1 ms is
                        // the bound.
                        DspCore core;
                        core.prepare (fs, 512, 1, from);
                        core.setParams (a);

                        const auto sw = (size_t) (0.2 * fs) / 512 * 512;
                        const auto y = render (core, tone (fs, 1000.0, kAmplitude, 0.5, 0.3), 512,
                                               [&] (size_t s) { return s >= sw ? b : a; });

                        int run = 0, longest = 0, zeros = 0, longestZeros = 0;
                        for (size_t i = sw; i < y.size(); ++i)
                        {
                            run = std::abs (y[i]) < 1.0e-4f ? run + 1 : 0;
                            longest = std::max (longest, run);
                            zeros = y[i] == 0.0f ? zeros + 1 : 0;
                            longestZeros = std::max (longestZeros, zeros);
                        }

                        check (longest <= (int) (0.0001 * fs),
                               "oversampling " + where + " leaves " + std::to_string (longest)
                                   + " samples of silence");

                        // And exact zero for no longer than the one sample at
                        // the bottom of the dip.
                        check (longestZeros <= 1,
                               "oversampling " + where + " holds exact zero for "
                                   + std::to_string (longestZeros) + " samples");
                    }

                    // Dry and wet stay aligned through the change. At Mix 50,
                    // with Drive and Tone at zero so the wet path is nearly a
                    // wire, the response after the change must be what the
                    // new factor's wet path and a dry path delayed by the new
                    // latency sum to. A dry path left at the old delay combs:
                    // at 70 samples' misalignment the sum has notches every
                    // 690 Hz at 48 kHz.
                    {
                        DspCore::Params a;
                        a.oversampling = from;
                        a.mixPercent = 50.0f;
                        a.driveAmount = 0.0f;
                        a.toneAmount = 0.0f;
                        DspCore::Params b = a;
                        b.oversampling = to;
                        DspCore::Params wetOnly = b;
                        wetOnly.mixPercent = 100.0f;

                        double worst = 0.0;

                        for (double hz : { 100.0, 345.0, 690.0, 1000.0, 2500.0, 6000.0, 12000.0 })
                        {
                            const auto x = tone (fs, hz, 0.01, 0.6);
                            const auto sw = (size_t) 2048;

                            DspCore core;
                            core.prepare (fs, 512, 1, from);
                            core.setParams (a);
                            const auto y = render (core, x, 512, [&] (size_t s) { return s >= sw ? b : a; });

                            DspCore wetCore;
                            wetCore.prepare (fs, 512, 1, to);
                            wetCore.setParams (wetOnly);
                            const auto w = render (wetCore, x, 512, [&] (size_t) { return wetOnly; });

                            const auto s0 = (size_t) (0.3 * fs), s1 = x.size();
                            const auto in = phasor (x, fs, hz, s0, s1);
                            const auto latency = bmo::Oversampler::latencyForFactor (to);
                            const auto predicted = 0.5 * phasor (w, fs, hz, s0, s1) / in
                                                 + 0.5 * std::polar (1.0, -2.0 * kPi * hz * latency / fs);
                            const auto measured = phasor (y, fs, hz, s0, s1) / in;

                            worst = std::max (worst, std::abs (20.0 * std::log10 (std::abs (measured) / std::abs (predicted))));
                        }

                        check (worst < 0.05, "oversampling " + std::to_string (from) + "x -> " + std::to_string (to)
                                                 + "x at " + rateName (fs) + ", mix 50 is off the aligned sum by "
                                                 + std::to_string (worst) + " dB after the change");
                    }

                    // Reported latency: the new factor's from the first
                    // process() after the change, as it always was.
                    DspCore core;
                    core.prepare (fs, 512, 2, from);
                    DspCore::Params p;
                    p.oversampling = from;
                    core.setParams (p);

                    std::vector<float> l (64, 0.0f), r (64, 0.0f);
                    float* channels[2] { l.data(), r.data() };
                    core.process (channels, 2, 64);

                    check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (from),
                           "latency before the change is the old factor's");

                    p.oversampling = to;
                    core.setParams (p);
                    core.process (channels, 2, 1);

                    check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (to),
                           "latency " + std::to_string (from) + "x -> " + std::to_string (to)
                               + "x is the new factor's from the first process() after the change");
                }
    }

    //== 2. Phase and Sat In cross over instead of stepping ==================
    // Each changed the signal path in one sample: Phase stepped the output by
    // twice the signal (4.2x the tone's own step in the review's probe, up to
    // 13x at the worst phase), Sat In by the difference between the shaped
    // and the plain signal (2.0x at Drive 40, 6.4x worst case). Each now fades
    // over kSwitchFadeMs. Both directions, Drive 40 and 100, 1x and 2x.
    {
        for (double fs : pick ({ 44100.0, 48000.0, 96000.0 }, { 44100.0, 48000.0 }))
            for (int os : { 1, 2 })
                for (float drive : { 40.0f, 100.0f })
                    for (int which = 0; which < 2; ++which)
                        for (bool on : { true, false })
                        {
                            DspCore::Params a;
                            a.oversampling = os;
                            a.driveAmount = drive;
                            auto& switched = which == 0 ? a.phaseInvert : a.saturationIn;
                            switched = ! on;
                            DspCore::Params b = a;
                            (which == 0 ? b.phaseInvert : b.saturationIn) = on;

                            const auto ratio = switchStepRatio (fs, a, b, 1000.0);
                            check (ratio < 1.5, std::string (which == 0 ? "Phase " : "Sat In ") + (on ? "on" : "off")
                                                    + " at " + rateName (fs) + ", " + std::to_string (os) + "x, Drive "
                                                    + std::to_string ((int) drive) + " steps " + ratioText (ratio));
                        }
    }

    //== 2b. Sat In back on does not replay what the stage heard before =======
    // Switched out, the stage stops and holds whatever the signal left in its
    // filters. Brought back in after the signal has stopped, it must start
    // from rest rather than release that.
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int os : { 1, 2 })
            {
                DspCore::Params on;
                on.oversampling = os;
                on.driveAmount = 100.0f;
                DspCore::Params off = on;
                off.saturationIn = false;

                DspCore core;
                core.prepare (fs, 512, 1, os);
                core.setParams (on);

                const auto offAt    = (size_t) (0.5 * fs) / 512 * 512;
                const auto silentAt = (size_t) (0.75 * fs);
                const auto onAt     = (size_t) (1.5 * fs) / 512 * 512;

                auto x = tone (fs, 7000.0, 0.5, 2.0);
                std::fill (x.begin() + (long) silentAt, x.end(), 0.0f);

                const auto y = render (core, x, 512, [&] (size_t s) { return s >= offAt && s < onAt ? off : on; });

                double peak = 0.0;
                for (size_t i = onAt; i < y.size(); ++i)
                    peak = std::max (peak, (double) std::abs (y[i]));

                check (peak < 1.0e-6, "Sat In back on in silence at " + rateName (fs) + ", "
                                          + std::to_string (os) + "x peaks at " + std::to_string (peak));
            }
    }

    //== 3. The same audio at every host block size ==========================
    // The smoothers and Auto Gain's detector advance once per 32-sample
    // control period, and a host block shorter than that used to make every
    // call a period of its own: at block 7 a +12 dB input step had reached
    // +6.2 dB more 10 ms after it than at block 32, and at block 1 Auto Gain
    // ran 32 times too fast and acted on 200 ms sections. The control periods
    // now run on the stream, whatever the host's blocks, so the same input
    // and the same automation give the same output, sample for sample.
    // Every parameter moves at once at sample 225792, the least common
    // multiple of the block sizes, so each host delivers it at the same
    // sample.
    {
        for (double fs : { 44100.0, 48000.0 })
            for (int scenario = 0; scenario < 3; ++scenario)
            {
                constexpr size_t change = 225792;

                std::vector<float> x ((size_t) (5.5 * fs));
                unsigned seed = 12345u;
                for (auto& v : x)
                {
                    seed = seed * 1664525u + 1013904223u;
                    v = 0.25f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
                }

                const auto paramsAt = [&] (size_t s)
                {
                    DspCore::Params p;
                    p.toneAmount = 100.0f;
                    p.autoGain = scenario != 0;
                    p.driveAmount = scenario == 1 ? 100.0f : 40.0f;

                    if (scenario != 1 && s >= change)
                    {
                        p.inputGainDb   = 12.0f;
                        p.driveAmount   = 100.0f;
                        p.toneAmount    = 20.0f;
                        p.mixPercent    = 60.0f;
                        p.outputLevelDb = -6.0f;
                        p.phaseInvert   = true;
                        p.saturationIn  = scenario == 2;
                        p.oversampling  = 2;
                    }

                    return p;
                };

                const auto renderAt = [&] (int block)
                {
                    DspCore core;
                    core.prepare (fs, 512, 2, 1);
                    core.setParams (paramsAt (0));
                    return render (core, x, block, paramsAt, 2);
                };

                const auto reference = renderAt (512);
                const char* names[] { "everything moves", "Auto Gain at Drive 100", "Auto Gain while everything moves" };

                for (int block : { 1, 7, 32, 441 })
                {
                    const auto y = renderAt (block);

                    double worst = 0.0;
                    for (size_t i = 0; i < y.size(); ++i)
                        worst = std::max (worst, (double) std::abs (y[i] - reference[i]));

                    check (worst == 0.0, std::string (names[scenario]) + " at block " + std::to_string (block)
                                             + ", " + rateName (fs) + " differs from block 512 by "
                                             + std::to_string (worst));
                }
            }
    }

    //== 4. Auto Gain is at its level from the first block ===================
    // prepare() threw Auto Gain's reading away and restarted the makeup at
    // unity, so with Tone at 100 (which the makeup pulls down by about 7 dB
    // on noise) every prepare() mid-stream put the output +6.8 dB high for
    // 10 ms and settled over about 70 ms. Measured as the level with Auto
    // Gain on against the same core with it off, so the chain's own start-up
    // is the same on both sides and only Auto Gain is compared: the first
    // 512-sample block after the latency, against the 50 ms before the
    // prepare() or reset(). Bound 0.5 dB.
    //
    // A fresh instance is deliberately not here. It has heard nothing, so it
    // starts at unity and glides to its first reading, as it always has; a
    // reading taken from the first few milliseconds overshoots on a
    // programme's onset and breaks sat_dsp's crest-factor test, which is the
    // owner's call and not this test's.
    {
        struct Setting { const char* name; float drive, tone; bool noise; };
        const Setting settings[] { { "Drive 40, Tone 100, noise", 40.0f, 100.0f, true },
                                   { "Drive 100, Tone 0, 1 kHz", 100.0f, 0.0f, false } };

        const auto rmsOf = [] (const std::vector<float>& y, size_t from, size_t length)
        {
            double acc = 0.0;
            for (size_t i = from; i < from + length; ++i) acc += (double) y[i] * y[i];
            return std::sqrt (acc / (double) length);
        };

        for (double fs : pick ({ 44100.0, 48000.0, 96000.0 }, { 44100.0, 48000.0 }))
            for (int os : { 1, 2 })
                for (const auto& setting : settings)
                    for (int start = 3; start < 6; ++start)
                    {
                        // 2 s in, after setParams() and prepare() the way a
                        // module is driven: 3, prepare() again, as a host does
                        // on a transport or buffer change; 4, reset(); 5,
                        // prepare() and then reset(), as a host does on play.
                        const auto again = (size_t) (2.0 * fs) / 512 * 512;

                        std::vector<float> x;
                        if (setting.noise)
                        {
                            x.resize ((size_t) (3.0 * fs));
                            unsigned seed = 777u;
                            for (auto& v : x)
                            {
                                seed = seed * 1664525u + 1013904223u;
                                v = 0.3f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
                            }
                        }
                        else
                        {
                            x = tone (fs, 1000.0, kAmplitude, 3.0);
                        }

                        const auto renderWith = [&] (bool autoGain)
                        {
                            DspCore::Params p;
                            p.driveAmount = setting.drive;
                            p.toneAmount = setting.tone;
                            p.oversampling = os;
                            p.autoGain = autoGain;

                            DspCore core;
                            core.setParams (p);
                            core.prepare (fs, 512, 1, os);
                            core.setParams (p);

                            auto y = x;
                            for (size_t at = 0; at < y.size(); at += 512)
                            {
                                if (at == again && (start == 3 || start == 5)) core.prepare (fs, 512, 1, os);
                                if (at == again && (start == 4 || start == 5)) core.reset();
                                core.setParams (p);
                                float* channels[1] { y.data() + at };
                                core.process (channels, 1, (int) std::min<size_t> (512, y.size() - at));
                            }

                            return y;
                        };

                        const auto on  = renderWith (true);
                        const auto off = renderWith (false);

                        const auto lat     = (size_t) bmo::Oversampler::latencyForFactor (os);
                        const auto window  = (size_t) (0.05 * fs);
                        const auto from     = again + lat;
                        const auto settleAt = again - window;

                        const auto first   = 20.0 * std::log10 (rmsOf (on, from, 512) / rmsOf (off, from, 512));
                        const auto settled = 20.0 * std::log10 (rmsOf (on, settleAt, window) / rmsOf (off, settleAt, window));

                        const char* how[] { "", "", "", "prepare() mid-stream", "reset() mid-stream",
                                            "prepare() and reset() mid-stream" };
                        check (std::abs (first - settled) < 0.5,
                               std::string ("Auto Gain (") + setting.name + ") in the first block after "
                                   + how[start] + " at " + rateName (fs) + ", " + std::to_string (os)
                                   + "x is " + std::to_string (first - settled) + " dB from where it settles");
                    }
    }

    //== 5. An oversampling change costs no callback more than both paths ===
    // The first version of the dip warmed the new path at the bottom by
    // replaying 141 samples of missed input inside one callback: about
    // 240 us, 1.4 blocks at 192 kHz / 32. The new path now runs alongside the
    // old one on the live input while the dip goes down, so no callback does
    // more than both paths' worth of a block. Counted, not timed: the work a
    // callback does is the oversampled samples it processes, summed over
    // channels and paths, and the bound is
    //
    //     channels x block x (old factor + new factor)
    //
    // with no slack, because nothing is processed but the live input. Away
    // from a change a callback does exactly one path's worth.
    {
        for (double fs : { 48000.0, 192000.0 })
            for (int block : pick ({ 32, 512 }, { 32 }))
                for (int from : kFactors)
                    for (int to : kFactors)
                    {
                        if (from == to)
                            continue;

                        DspCore core;
                        core.prepare (fs, block, 2, from);
                        DspCore::Params p;
                        p.oversampling = from;
                        core.setParams (p);

                        std::vector<float> l ((size_t) block), r ((size_t) block);
                        float* channels[2] { l.data(), r.data() };

                        const auto where = std::to_string (from) + "x -> " + std::to_string (to) + "x at "
                                         + rateName (fs) + ", block " + std::to_string (block);
                        const auto blocks = (int) (0.1 * fs) / block + 2;

                        unsigned long long worst = 0, last = 0;
                        bool steadyBefore = true;

                        for (int b = 0; b < 3 * blocks; ++b)
                        {
                            for (int i = 0; i < block; ++i)
                                l[(size_t) i] = r[(size_t) i] = 0.1f * std::sin (0.05f * (float) (b * block + i));

                            if (b == blocks)
                            {
                                p.oversampling = to;
                                core.setParams (p);
                            }

                            const auto before = core.oversampledSamplesProcessed();
                            core.process (channels, 2, block);
                            const auto work = core.oversampledSamplesProcessed() - before;

                            if (b < blocks)
                                steadyBefore = steadyBefore && work == 2ull * (unsigned long long) (block * from);
                            else
                                worst = std::max (worst, work);

                            last = work;
                        }

                        const auto bound = 2ull * (unsigned long long) (block * (from + to));

                        check (steadyBefore, where + ": a steady callback does one path's work");
                        check (worst <= bound, where + ": a callback during the change processed " + std::to_string (worst)
                                                   + " oversampled samples, past both paths' " + std::to_string (bound));
                        check (last == 2ull * (unsigned long long) (block * to),
                               where + ": after the change a callback does one path's work again ("
                                   + std::to_string (last) + ")");
                    }
    }

    //== 5b. An interrupted change leaves nothing behind ======================
    // prepare() or reset() at the midpoint of the fade down must give, from
    // that point on, exactly what a fresh instance at the new factor gives on
    // the same input; a request withdrawn before the bottom must leave the
    // output, once the gain is back at 1, exactly what it would have been had
    // nothing been asked. Mix 50, Drive 60, a three-tone signal.
    {
        DspCore::Params settings;
        settings.mixPercent  = 50.0f;
        settings.driveAmount = 60.0f;

        const auto tones = [] (double fs, size_t length)
        {
            std::vector<float> x (length);
            for (size_t i = 0; i < length; ++i)
            {
                const auto t = (double) i / fs;
                x[i] = (float) (0.06 * std::sin (2.0 * kPi * 100.0 * t)
                              + 0.06 * std::sin (2.0 * kPi * 1000.0 * t + 0.5)
                              + 0.06 * std::sin (2.0 * kPi * 7000.0 * t + 1.0));
            }
            return x;
        };

        for (double fs : pick ({ 44100.0, 48000.0, 96000.0 }, { 44100.0, 48000.0 }))
            for (int from : kFactors)
                for (int to : kFactors)
                {
                    if (from == to || ! keepPair (from, to))
                        continue;

                    auto a = settings; a.oversampling = from;
                    auto b = settings; b.oversampling = to;

                    const auto where = std::to_string (from) + "x -> " + std::to_string (to) + "x at " + rateName (fs);
                    const auto length = (size_t) (0.5 * fs);
                    const auto x = tones (fs, length);
                    constexpr size_t block = 64;   // fine enough to land inside a 10 ms dip
                    const auto sw  = (size_t) (0.2 * fs) / block * block;
                    const auto mid = sw + (size_t) (0.005 * fs) / block * block;

                    for (int how = 0; how < 3; ++how)
                    {
                        const char* names[] { "prepare()", "reset()", "withdrawn" };

                        DspCore core;
                        core.prepare (fs, (int) block, 1, from);
                        core.setParams (a);

                        std::vector<float> y = x;
                        for (size_t start = 0; start < length; start += block)
                        {
                            auto p = start >= sw ? b : a;

                            if (start == mid && how == 0) core.prepare (fs, (int) block, 1, to);
                            if (start == mid && how == 1) core.reset();
                            if (start >= mid && how == 2) p = a;

                            core.setParams (p);
                            float* channels[1] { y.data() + start };
                            core.process (channels, 1, (int) std::min (block, length - start));
                        }

                        double worst = 0.0;

                        if (how == 2)
                        {
                            DspCore never;
                            never.prepare (fs, (int) block, 1, from);
                            never.setParams (a);
                            const auto z = render (never, x, (int) block, [&] (size_t) { return a; });

                            for (size_t i = mid + (size_t) (0.012 * fs); i < length; ++i)
                                worst = std::max (worst, (double) std::abs (y[i] - z[i]));

                            check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (from),
                                   "oversampling " + where + " withdrawn mid-dip: latency is the old factor's");
                        }
                        else
                        {
                            DspCore fresh;
                            fresh.prepare (fs, (int) block, 1, to);
                            fresh.setParams (b);
                            const std::vector<float> rest (x.begin() + (long) mid, x.end());
                            const auto z = render (fresh, rest, (int) block, [&] (size_t) { return b; });

                            for (size_t i = mid; i < length; ++i)
                                worst = std::max (worst, (double) std::abs (y[i] - z[i - mid]));
                        }

                        check (worst == 0.0, "oversampling " + where + ", " + names[how] + " mid-dip, differs from "
                                                 + (how == 2 ? "never switching" : "a fresh instance") + " by "
                                                 + std::to_string (worst));
                    }
                }

        // A request reset away and then withdrawn leaves nothing behind: the
        // latency is the running factor's.
        for (int from : kFactors)
            for (int to : kFactors)
            {
                if (from == to)
                    continue;

                DspCore core;
                core.prepare (48000.0, 64, 1, from);
                auto p = settings;
                p.oversampling = from;
                core.setParams (p);

                std::vector<float> buffer (64, 0.0f);
                float* channels[1] { buffer.data() };
                core.process (channels, 1, 64);

                p.oversampling = to;   core.setParams (p); core.process (channels, 1, 64);
                core.reset();
                p.oversampling = from; core.setParams (p); core.process (channels, 1, 64);

                check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (from),
                       "oversampling " + std::to_string (from) + "x -> " + std::to_string (to)
                           + "x, reset(), back to " + std::to_string (from) + "x: latency reports "
                           + std::to_string (core.getLatencySamples()) + ", the running factor's is "
                           + std::to_string (bmo::Oversampler::latencyForFactor (from)));
            }
    }

    std::cout << checks << " checks, " << failures << " failures"
              << (longRun ? "" : " (the default subset; --long runs every row)") << "\n";
    if (failures == 0)
        std::cout << "All Saturator switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
