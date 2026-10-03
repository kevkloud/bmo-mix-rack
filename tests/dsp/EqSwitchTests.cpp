// BMO EQ: what its switches do to the audio at the moment they move.
//
// EqDspTests holds what the module does once it has settled; this file holds
// the transitions -- a control changing while a signal passes, or after one
// has stopped -- which none of those tests can see, because each of them
// builds a fresh core, sets it once and measures the result.

#include "modules/eq/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace bmo::eq;

namespace
{
    int failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    std::string rateName (double fs)
    {
        return std::to_string ((int) std::lround (fs / 100.0) / 10) + "."
             + std::to_string ((int) std::lround (fs / 100.0) % 10) + " kHz";
    }

    /** Render through one core, with the parameters chosen per block from
        the block's first sample. Both channels carry the same signal; the
        left is returned. A first block shorter than the rest, as hosts
        sometimes send, lets a later sample start a block at any size. */
    std::vector<float> render (DspCore& core, std::vector<float> signal, int block,
                               const std::function<DspCore::Params (size_t)>& paramsAt,
                               int numChannels = 2, int firstBlock = 0)
    {
        std::vector<float> right = signal;

        for (size_t start = 0; start < signal.size();)
        {
            const auto size = (size_t) (start == 0 && firstBlock > 0 ? firstBlock : block);
            const auto n = (int) std::min (size, signal.size() - start);
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
        constexpr double amplitude = 0.17782794;   // -18 dBFS RMS
        const auto length = (size_t) (0.8 * fs);
        const auto sw     = (size_t) (0.3 * fs) / (size_t) block * (size_t) block;

        double worst = 0.0;

        for (int k = 0; k < 4; ++k)
        {
            std::vector<float> x (length);
            for (size_t i = 0; i < length; ++i)
                x[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));

            DspCore core;
            core.prepare (fs, block, 1, a.oversampling);
            core.setParams (a);

            const auto y = render (core, x, block, [&] (size_t s) { return s >= sw ? b : a; }, 1);

            const auto before = largestStep (y, sw - (size_t) (0.15 * fs), sw);
            const auto after  = largestStep (y, (size_t) (0.6 * fs), length);
            const auto during = largestStep (y, sw, sw + (size_t) (0.25 * fs));

            worst = std::max (worst, during / std::max (before, after));
        }

        return worst;
    }

    /** A trim move judged against the signal at the level in use: the
        largest sample-to-sample step after the move, against what the
        steady signal's own largest step is at the gain each sample has.
        A gain that moves smoothly scores about 1; one that jumps scores its
        jump against the signal's own slope. The gain at each sample is read
        off the same render with the trims held at 0 dB, so the chain must be
        near linear: EQ out, 1x, and an input trim no higher than 0 dB. */
    double trimStepRatio (double fs, int block, DspCore::Params a, DspCore::Params b, double hz)
    {
        constexpr double amplitude = 0.17782794;   // -18 dBFS RMS
        const auto length = (size_t) (0.5 * fs);
        const auto sw     = (size_t) (0.1 * fs) / (size_t) block * (size_t) block;

        auto flat = a;
        flat.inputGainDb = flat.outputLevelDb = 0.0f;

        double worst = 0.0;

        for (int k = 0; k < 4; ++k)
        {
            std::vector<float> x (length);
            for (size_t i = 0; i < length; ++i)
                x[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));

            DspCore refCore, core;
            refCore.prepare (fs, block, 1, flat.oversampling);
            refCore.setParams (flat);
            core.prepare (fs, block, 1, a.oversampling);
            core.setParams (a);

            const auto ref = render (refCore, x, block, [&] (size_t) { return flat; }, 1);
            const auto y   = render (core, x, block, [&] (size_t s) { return s >= sw ? b : a; }, 1);

            const auto own = largestStep (ref, (size_t) (0.05 * fs), length);

            for (size_t n = sw + 1; n < length; ++n)
            {
                if (std::abs (ref[n]) < 0.25 * amplitude || std::abs (ref[n - 1]) < 0.25 * amplitude)
                    continue;

                const auto gain = std::max (std::abs (y[n] / ref[n]), std::abs (y[n - 1] / ref[n - 1]));
                worst = std::max (worst, (double) std::abs (y[n] - y[n - 1]) / (gain * own));
            }
        }

        return worst;
    }

    std::string ratioText (double r)
    {
        return std::to_string (std::round (r * 100.0) / 100.0).substr (0, 5) + "x";
    }
}

int main()
{
    //== 1. A cut switched back on does not replay what it heard before ======
    // Low Cut and High Cut used to keep their filter state while switched
    // off, frozen at whatever the signal was doing when they went off, and
    // switching one back on released it: a 100 Hz tone stopped, 0.75 s of
    // digital silence, Low Cut 360 back on, and the output peaked at -6.5 dBFS.
    // A filter that has never been used gives about 4e-11 there.
    {
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int highCut = 0; highCut < 2; ++highCut)
                for (int choice = 1; choice <= (highCut ? 5 : 4); ++choice)
                {
                    DspCore::Params on;
                    (highCut ? on.lpfIndex : on.hpfIndex) = choice;
                    DspCore::Params off = on;
                    (highCut ? off.lpfIndex : off.hpfIndex) = 0;

                    DspCore core;
                    core.prepare (fs, 512, 2, on.oversampling);
                    core.setParams (on);

                    // On with a tone, off while it plays, the tone stops, and
                    // the cut comes back on 0.75 s into the silence. The
                    // chain's own tail has died away to about 1e-10 by then,
                    // so anything near the bound is the filter's.
                    const auto offAt    = (size_t) (0.5 * fs) / 512 * 512 + 3 * 512;
                    const auto silentAt = (size_t) (0.75 * fs);
                    const auto onAt     = (size_t) (1.5 * fs) / 512 * 512;
                    const auto hz       = highCut ? 3000.0 : 100.0;

                    std::vector<float> x ((size_t) (2.0 * fs));
                    for (size_t i = 0; i < silentAt; ++i)
                        x[i] = (float) (0.5 * std::sin (2.0 * kPi * hz * (double) i / fs));

                    const auto y = render (core, x, 512, [&] (size_t s)
                                           { return s >= offAt && s < onAt ? off : on; });

                    double peak = 0.0;
                    for (size_t i = onAt; i < y.size(); ++i)
                        peak = std::max (peak, (double) std::abs (y[i]));

                    check (peak < 1.0e-6,
                           std::string (highCut ? "High Cut " : "Low Cut ")
                               + std::to_string ((int) (highCut ? lpfFreqHz (choice) : hpfFreqHz (choice)))
                               + " re-enabled in silence at " + rateName (fs)
                               + " must stay silent, peaked at " + std::to_string (peak));
                }
    }

    //== 2. The switches fade rather than step ===============================
    // Measured on 6f6b8c3 against the steady signal's own largest step: Low
    // Cut 360 -> Off 62-74x, Phase 14.5x, EQ In off 3.4-4.2x (a step of a
    // whole unit of full scale with the mid at +18), Hi-Q 1.7-2.2x. The bound
    // is 1.5x, at 44.1, 48 and 96 kHz, at every oversampling factor, both
    // ways, for every cut choice. The cuts are driven where they bite and
    // shift phase most among the probe's tones: 100 Hz for Low Cut, 5 kHz for
    // High Cut.
    //
    // The band this holds is 30 Hz and up, not everything: a fixed 10 ms
    // fade adds a slope of about the signal's size over 10 ms, while a tone's
    // own slope falls with its frequency, so below about 30 Hz Phase and a
    // cut switching on cross 1.5x. Section 2c asserts the 25 Hz figures as a
    // separate, documented bound (EqNetwork::kSwitchFadeMs says why 10 ms).
    {
        constexpr double kBound = 1.5;

        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int os : { 1, 2, 4, 8 })
            {
                DspCore::Params base;
                base.oversampling = os;

                const auto where = " at " + rateName (fs) + ", " + std::to_string (os) + "x";
                const auto both = [&] (const std::string& name, DspCore::Params p, DspCore::Params q, double hz)
                {
                    const auto on  = switchStepRatio (fs, p, q, hz);
                    const auto off = switchStepRatio (fs, q, p, hz);
                    check (on < kBound, name + " on" + where + " steps " + ratioText (on));
                    check (off < kBound, name + " off" + where + " steps " + ratioText (off));
                };

                for (int choice = 1; choice <= 4; ++choice)
                {
                    auto p = base; p.hpfIndex = choice;
                    both ("Low Cut " + std::to_string ((int) hpfFreqHz (choice)), base, p, 100.0);
                }

                for (int choice = 1; choice <= 5; ++choice)
                {
                    auto p = base; p.lpfIndex = choice;
                    both ("High Cut " + std::to_string ((int) lpfFreqHz (choice)), base, p, 5000.0);
                }

                {
                    auto p = base; p.phaseInvert = true;
                    both ("Phase", base, p, 1000.0);
                }

                {
                    auto in = base;  in.midGainDb = 18.0f;
                    auto out = in;   out.eqIn = false;
                    both ("EQ In (mid +18)", out, in, 1600.0);

                    auto hin = base; hin.hfGainDb = 16.0f;
                    auto hout = hin; hout.eqIn = false;
                    both ("EQ In (high shelf +16)", hout, hin, 12000.0);
                }

                {
                    auto wide = base; wide.midGainDb = 18.0f;
                    auto narrow = wide; narrow.midHiQ = true;
                    both ("Hi-Q (mid +18)", wide, narrow, 1600.0);
                }
            }
    }

    //== 2c. Below the band: the 25 Hz bound ===============================
    // Not the 1.5x bound, by design: at 25 Hz a 10 ms fade is a quarter of a
    // cycle, and Phase reaches 1.61x and Low Cut 1.58x (worst of 44.1, 48
    // and 96 kHz, 1x and 2x, every choice, both ways; measured on ICE QUEEN
    // at 54b0b72). The bound is 1.65x, that figure with 3 % margin, so
    // a change that makes the low end worse fails here rather than passing
    // silently below the band section 2 covers. Raising the fade to hold
    // 1.5x at 25 Hz would slow every switch for every signal; that is the
    // owner's decision, not this test's.
    {
        constexpr double kLowToneBound = 1.65;

        double worstPhase = 0.0, worstCut = 0.0;

        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int os : { 1, 2 })
            {
                DspCore::Params base;
                base.oversampling = os;

                auto flipped = base; flipped.phaseInvert = true;
                worstPhase = std::max ({ worstPhase, switchStepRatio (fs, base, flipped, 25.0),
                                                     switchStepRatio (fs, flipped, base, 25.0) });

                for (int choice = 1; choice <= 4; ++choice)
                {
                    auto cut = base; cut.hpfIndex = choice;
                    worstCut = std::max ({ worstCut, switchStepRatio (fs, base, cut, 25.0),
                                                     switchStepRatio (fs, cut, base, 25.0) });
                }
            }

        check (worstPhase < kLowToneBound, "Phase at 25 Hz steps " + ratioText (worstPhase)
                                               + ", past its documented " + ratioText (kLowToneBound));
        check (worstCut < kLowToneBound, "Low Cut at 25 Hz steps " + ratioText (worstCut)
                                             + ", past its documented " + ratioText (kLowToneBound));
    }

    //== 2b. EQ In brought back in silence does not replay either ===========
    // EQ In out of circuit stops the network, and a stopped network holds its
    // state exactly as a stopped cut does: the same fault as section 1, one
    // switch along.
    {
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            DspCore::Params in;
            in.midGainDb = 18.0f;
            DspCore::Params out = in;
            out.eqIn = false;

            DspCore core;
            core.prepare (fs, 512, 2, in.oversampling);
            core.setParams (in);

            const auto offAt    = (size_t) (0.5 * fs) / 512 * 512 + 3 * 512;
            const auto silentAt = (size_t) (0.75 * fs);
            const auto onAt     = (size_t) (1.5 * fs) / 512 * 512;

            std::vector<float> x ((size_t) (2.0 * fs));
            for (size_t i = 0; i < silentAt; ++i)
                x[i] = (float) (0.5 * std::sin (2.0 * kPi * 1600.0 * (double) i / fs));

            const auto y = render (core, x, 512, [&] (size_t s)
                                   { return s >= offAt && s < onAt ? out : in; });

            double peak = 0.0;
            for (size_t i = onAt; i < y.size(); ++i)
                peak = std::max (peak, (double) std::abs (y[i]));

            check (peak < 1.0e-6, "EQ In (mid +18) back in, in silence, at " + rateName (fs)
                                      + " must stay silent, peaked at " + std::to_string (peak));
        }
    }

    //== 3. Changing the oversampling dips through zero instead of cutting ===
    // A change of oversampling changes the latency, so no blend of before
    // and after exists: the two are not aligned in time. It used to reset
    // every stage on the spot, which gave 28-65 samples of silence (below
    // 1e-4, 65 dB under the tone's peak) and then a jump the size of the
    // latency change (6.8-9.4x the tone's own step). Wanted: a fade down,
    // the change, a fade up, with no run of silence beyond the bottom of the
    // dip, nothing above the 1.5x bound, and the latency reported for the
    // new factor from the first process() after the change, exactly as
    // before.
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int from : { 1, 2, 4, 8 })
                for (int to : { 1, 2, 4, 8 })
                {
                    if (from == to)
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
                        // under the threshold for about 0.07 ms at any rate
                        // (the fade's length and the tone's slope per sample
                        // both scale with it). 0.1 ms is the bound.
                        DspCore core;
                        core.prepare (fs, 512, 1, from);
                        core.setParams (a);

                        std::vector<float> x ((size_t) (0.5 * fs));
                        for (size_t i = 0; i < x.size(); ++i)
                            x[i] = (float) (0.17782794 * std::sin (2.0 * kPi * 1000.0 * (double) i / fs + 0.3));

                        const auto sw = (size_t) (0.2 * fs) / 512 * 512;
                        const auto y = render (core, x, 512, [&] (size_t s) { return s >= sw ? b : a; }, 1);

                        int run = 0, longest = 0;
                        for (size_t i = sw; i < y.size(); ++i)
                        {
                            run = std::abs (y[i]) < 1.0e-4f ? run + 1 : 0;
                            longest = std::max (longest, run);
                        }

                        check (longest <= (int) (0.0001 * fs),
                               "oversampling " + where + " leaves " + std::to_string (longest)
                                   + " samples of silence");
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

    //== 3b. After the dip the module is the new factor's, whatever happened
    // Section 3 measures steps against a 1 kHz tone, which cannot see the dry
    // and wet halves of Mix 50 landing out of line after the change -- a comb
    // that is steady, so no step. Here the module after a change is held
    // against a fresh instance at the new factor, on three tones across the
    // band with the EQ and Auto Gain doing something: once the dip and the
    // chain's own memory are past, the two must agree to float noise. And
    // the dip is interrupted three ways -- prepare() and reset() at its
    // midpoint, and the request withdrawn before the bottom -- each held
    // against what a fresh instance does from that point.
    {
        DspCore::Params settings;
        settings.mixPercent = 50.0f;
        settings.midGainDb  = 6.0f;
        settings.lfGainDb   = -4.0f;
        settings.autoGain   = true;

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

        // y from `from` on against z, which starts `offset` samples into y.
        const auto worstDifference = [] (const std::vector<float>& y, const std::vector<float>& z,
                                         size_t from, size_t offset)
        {
            double worst = 0.0;
            for (size_t i = from; i < y.size(); ++i)
                worst = std::max (worst, (double) std::abs (y[i] - z[i - offset]));
            return worst;
        };

        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int from : { 1, 2, 4, 8 })
                for (int to : { 1, 2, 4, 8 })
                {
                    if (from == to)
                        continue;

                    auto a = settings; a.oversampling = from;
                    auto b = settings; b.oversampling = to;

                    const auto where = std::to_string (from) + "x -> " + std::to_string (to) + "x at " + rateName (fs);
                    const auto length = (size_t) (0.8 * fs);
                    const auto x = tones (fs, length);
                    constexpr size_t block = 64;   // fine enough to land inside a 10 ms dip
                    const auto sw  = (size_t) (0.2 * fs) / block * block;
                    const auto mid = sw + (size_t) (0.005 * fs) / block * block;   // halfway down the dip

                    // A completed change: from 0.4 s after it, a fresh instance
                    // at the new factor, run from the start, is what it gives.
                    {
                        DspCore moved, fresh;
                        moved.prepare (fs, (int) block, 1, from);
                        moved.setParams (a);
                        fresh.prepare (fs, (int) block, 1, to);
                        fresh.setParams (b);

                        const auto y = render (moved, x, (int) block, [&] (size_t s) { return s >= sw ? b : a; }, 1);
                        const auto z = render (fresh, x, (int) block, [&] (size_t) { return b; }, 1);

                        const auto worst = worstDifference (y, z, sw + (size_t) (0.4 * fs), 0);
                        check (worst < 1.0e-6, "Mix 50 after oversampling " + where
                                                   + " differs from a fresh instance at the new factor by "
                                                   + std::to_string (worst));
                    }

                    // Interrupted at the midpoint of the fade down.
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
                            if (start >= mid && how == 2) p = a;   // A -> B -> A before the bottom

                            core.setParams (p);
                            float* channels[1] { y.data() + start };
                            core.process (channels, 1, (int) std::min (block, length - start));

                            if (start == mid && how < 2)
                                check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (to),
                                       std::string (names[how]) + " mid-dip, " + where
                                           + ": latency is the new factor's");
                        }

                        if (how == 2)
                        {
                            // Withdrawn: once the gain is back at 1, nothing
                            // changed at all -- the instance never switched.
                            DspCore never;
                            never.prepare (fs, (int) block, 1, from);
                            never.setParams (a);
                            const auto z = render (never, x, (int) block, [&] (size_t) { return a; }, 1);

                            const auto worst = worstDifference (y, z, mid + (size_t) (0.012 * fs), 0);
                            check (worst == 0.0, "oversampling " + where + " withdrawn mid-dip differs from "
                                                     "never switching by " + std::to_string (worst));
                            check (core.getLatencySamples() == bmo::Oversampler::latencyForFactor (from),
                                   "oversampling " + where + " withdrawn mid-dip: latency is the old factor's");
                        }
                        else
                        {
                            // prepare() or reset() mid-dip: from that point on,
                            // exactly what a fresh instance at the new factor
                            // gives on the same input.
                            DspCore fresh;
                            fresh.prepare (fs, (int) block, 1, to);
                            fresh.setParams (b);
                            const std::vector<float> rest (x.begin() + (long) mid, x.end());
                            const auto z = render (fresh, rest, (int) block, [&] (size_t) { return b; }, 1);

                            const auto worst = worstDifference (y, z, mid, mid);
                            check (worst == 0.0, std::string (names[how]) + " mid-dip, " + where
                                                     + ", differs from a fresh instance by " + std::to_string (worst));
                        }
                    }
                }

        // A request reset away and then withdrawn leaves nothing behind: the
        // latency is the running factor's.
        for (int from : { 1, 2, 4, 8 })
            for (int to : { 1, 2, 4, 8 })
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

    //== 4. Auto Gain is at its level from the first block ===================
    // Auto Gain's figure comes from the EQ's settings, not from the signal,
    // so it is known before the first sample. It used to start from unity
    // after prepare() and glide: +6.60 dB at 2 ms, +3.42 at 20 ms, +0.79 at
    // 60 ms above where it settled, with the low shelf at +16 and the mid at
    // +12. Measured as the level with Auto Gain on against the same core with
    // it off, so the chain's own start-up is the same on both sides and only
    // Auto Gain is compared.
    {
        struct Setting { const char* name; float lf, mid, hf; };
        const Setting settings[] { { "low +16, mid +12", 16.0f, 12.0f, 0.0f },
                                   { "high -16, mid -18", 0.0f, -18.0f, -16.0f } };

        const auto rmsOf = [] (const std::vector<float>& y, size_t from, size_t length)
        {
            double acc = 0.0;
            for (size_t i = from; i < from + length; ++i) acc += (double) y[i] * y[i];
            return std::sqrt (acc / (double) length);
        };

        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int os : { 1, 2 })
                for (const auto& setting : settings)
                    for (int start = 0; start < 3; ++start)
                    {
                        // 0: prepare, then setParams. 1: setParams, prepare,
                        // setParams, which is how a module is driven. 2: the
                        // same followed by reset(), as a host does on play.
                        const auto renderWith = [&] (bool autoGain)
                        {
                            DspCore::Params p;
                            p.lfGainDb = setting.lf; p.midGainDb = setting.mid; p.hfGainDb = setting.hf;
                            p.oversampling = os;
                            p.autoGain = autoGain;

                            DspCore core;
                            if (start > 0) core.setParams (p);
                            core.prepare (fs, 512, 1, os);
                            core.setParams (p);
                            if (start == 2) core.reset();

                            std::vector<float> x ((size_t) (0.5 * fs));
                            for (size_t i = 0; i < x.size(); ++i)
                                x[i] = (float) (0.17782794 * std::sin (2.0 * kPi * 1000.0 * (double) i / fs));

                            return render (core, x, 512, [&] (size_t) { return p; }, 1);
                        };

                        const auto on  = renderWith (true);
                        const auto off = renderWith (false);

                        // The first 512-sample block after the latency, and the
                        // last 50 ms, where everything has settled.
                        const auto lat  = (size_t) bmo::Oversampler::latencyForFactor (os);
                        const auto tail = (size_t) (0.05 * fs);

                        const auto first   = 20.0 * std::log10 (rmsOf (on, lat, 512) / rmsOf (off, lat, 512));
                        const auto settled = 20.0 * std::log10 (rmsOf (on, on.size() - tail, tail)
                                                                / rmsOf (off, off.size() - tail, tail));

                        const char* how[] { "prepare()", "setParams(), prepare()", "prepare() and reset()" };
                        check (std::abs (first - settled) < 0.5,
                               std::string ("Auto Gain (") + setting.name + ") in the first block after "
                                   + how[start] + " at " + rateName (fs) + (os == 1 ? ", 1x" : ", 2x")
                                   + " is " + std::to_string (first - settled) + " dB from where it settles");
                    }
    }

    //== 5. The trims move every sample, in the same time at any block size =
    // The trims were smoothed once per 32-sample sub-block, linearly in gain:
    // Output -24 -> +24 dB rose 19.3 dB in its first sample, 0 -> +6 dB
    // arrived as 71 steps of 0.28 dB, and because a host block shorter than
    // 32 samples still counted as a sub-block, the same move ran 32 times
    // faster at a block size of 1. Judged against the signal at the level
    // in use (trimStepRatio), at 100 Hz and 1 kHz, 44.1/48/96 kHz, host
    // blocks 1/32/441/512: nothing above 1.5x.
    {
        struct Move { const char* name; float in0, in1, out0, out1; };
        const Move moves[] { { "Output -24 -> +24", 0.0f, 0.0f, -24.0f, 24.0f },
                             { "Output +24 -> -24", 0.0f, 0.0f, 24.0f, -24.0f },
                             { "Output 0 -> +6",    0.0f, 0.0f, 0.0f, 6.0f },
                             { "Input -24 -> 0",    -24.0f, 0.0f, 0.0f, 0.0f },
                             { "Input 0 -> -24",    0.0f, -24.0f, 0.0f, 0.0f } };

        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 1, 32, 441, 512 })
                for (const auto& move : moves)
                    for (double hz : { 100.0, 1000.0 })
                    {
                        DspCore::Params a;
                        a.eqIn = false;
                        a.oversampling = 1;
                        a.inputGainDb = move.in0;
                        a.outputLevelDb = move.out0;
                        auto b = a;
                        b.inputGainDb = move.in1;
                        b.outputLevelDb = move.out1;

                        const auto ratio = trimStepRatio (fs, block, a, b, hz);
                        check (ratio < 1.5, std::string (move.name) + " at " + std::to_string ((int) hz) + " Hz, "
                                                + rateName (fs) + ", block " + std::to_string (block)
                                                + " steps " + ratioText (ratio) + " the signal's own at its level");
                    }

        // The same move at every block size is the same audio: the move lands
        // on a sample that starts a block for all four (a shorter first block
        // puts it there for 441), and the renders must agree to the bit.
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int which = 0; which < 2; ++which)
            {
                const auto sw = (size_t) (0.1 * fs) / 512 * 512;

                DspCore::Params a;
                a.eqIn = false;
                a.oversampling = 1;
                auto b = a;
                if (which == 0) { a.outputLevelDb = -24.0f; b.outputLevelDb = 24.0f; }
                else            { a.inputGainDb   = -24.0f; b.inputGainDb   = 0.0f; }

                std::vector<float> x (sw + (size_t) (0.4 * fs));
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] = (float) (0.17782794 * std::sin (2.0 * kPi * 100.0 * (double) i / fs));

                const auto renderAt = [&] (int block)
                {
                    DspCore core;
                    core.prepare (fs, block, 1, 1);
                    core.setParams (a);
                    return render (core, x, block, [&] (size_t s) { return s >= sw ? b : a; }, 1,
                                   (int) (sw % (size_t) block));
                };

                const auto reference = renderAt (512);

                for (int block : { 1, 32, 441 })
                {
                    const auto y = renderAt (block);

                    double worst = 0.0;
                    for (size_t i = sw; i < y.size(); ++i)
                        worst = std::max (worst, (double) std::abs (y[i] - reference[i]));

                    check (worst == 0.0, std::string (which ? "Input" : "Output") + " move at block "
                                             + std::to_string (block) + ", " + rateName (fs)
                                             + " differs from block 512 by " + std::to_string (worst));
                }
            }
    }

    //== 6. A trim lands exactly on its target, and the landing is not a step
    // A one-pole in float32 stalls short of a target that is not zero: near
    // 24 dB one float step is 1.9e-6 dB, and once the pole's increment falls
    // under half of that it rounds away. On d8cdd5f that left a +/-24 dB move
    // 8.4e-4 dB short at 44.1 kHz and 1.8e-3 at 96 kHz, for ever: the gain
    // was never dbToGain of the setting, the output was never what a fresh
    // instance at that setting gives, and the pow ran every sample. Wanted:
    // exactly dbToGain (the figure a fresh instance snaps to) within 250 ms
    // of any move -- 48 dB down to 1e-3 dB is 10.8 time constants of 20 ms,
    // 216 ms -- held from then on, and the landing too small a step to measure against a
    // 100 Hz tone.
    {
        struct Move { float from, to; };
        const Move moves[] { { 0.0f, 6.0f }, { 0.0f, -24.0f }, { -24.0f, 24.0f },
                             { 24.0f, -24.0f }, { 0.0f, -3.5f } };

        const auto moveName = [] (const Move& m)
        {
            return std::to_string (m.from).substr (0, std::to_string (m.from).find ('.') + 2) + " -> "
                 + std::to_string (m.to).substr (0, std::to_string (m.to).find ('.') + 2) + " dB";
        };

        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const auto& move : moves)
            {
                TrimSmoother trim;
                trim.prepare (fs, 20.0);
                trim.snap (move.from);
                trim.setTarget (move.to);

                const auto exact = std::pow (10.0f, move.to * 0.05f);
                const auto landBy = (int) (0.25 * fs);

                double landingStep = 0.0;
                int landedAt = -1;
                float previous = trim.next();

                for (int n = 1; n < landBy + (int) fs; ++n)
                {
                    const auto g = trim.next();
                    const auto stepDb = std::abs (20.0 * std::log10 ((double) g / (double) previous));

                    if (landedAt < 0 && g == exact)
                    {
                        landedAt = n;
                        landingStep = stepDb;
                    }
                    else if (landedAt > 0 && g != exact)
                    {
                        landedAt = -2;   // left the target again
                    }

                    previous = g;
                }

                const auto where = "trim " + moveName (move) + " at " + rateName (fs);
                check (landedAt > 0 && landedAt <= landBy,
                       where + " lands exactly on dbToGain of its target within 250 ms and stays"
                           + (landedAt == -1 ? " (never landed)" : landedAt == -2 ? " (left it)"
                                              : " (landed at " + std::to_string (landedAt) + " samples)"));
                // The landing changes the gain by a fraction f, so it moves a
                // tone of amplitude A by at most f * A in one sample. A 100 Hz
                // tone's own largest step is 2 pi 100 / fs of A; the landing
                // must take less than half of the 1.5x bound's headroom.
                const auto fraction = std::pow (10.0, landingStep / 20.0) - 1.0;
                const auto ownStep  = 2.0 * kPi * 100.0 / fs;
                check (fraction < 0.25 * ownStep,
                       where + " lands with a step of " + std::to_string (landingStep) + " dB, "
                           + std::to_string (fraction / ownStep) + " of a 100 Hz tone's own step");
            }

        // Through the whole module: after an Output move the output is what
        // a fresh instance held at the target gives, to the bit. Output is
        // the last gain in the chain and nothing upstream depends on it, so
        // the two must agree exactly once the trim has landed.
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (const auto& move : moves)
            {
                DspCore::Params a;
                a.outputLevelDb = move.from;
                a.midGainDb = 6.0f;
                auto b = a;
                b.outputLevelDb = move.to;

                std::vector<float> x ((size_t) (0.8 * fs));
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] = (float) (0.17782794 * std::sin (2.0 * kPi * 440.0 * (double) i / fs));

                const auto sw = (size_t) (0.1 * fs) / 512 * 512;

                DspCore moved, fresh;
                moved.prepare (fs, 512, 1, a.oversampling);
                moved.setParams (a);
                fresh.prepare (fs, 512, 1, b.oversampling);
                fresh.setParams (b);

                const auto y = render (moved, x, 512, [&] (size_t s) { return s >= sw ? b : a; }, 1);
                const auto z = render (fresh, x, 512, [&] (size_t) { return b; }, 1);

                double worst = 0.0;
                for (size_t i = sw + (size_t) (0.25 * fs); i < y.size(); ++i)
                    worst = std::max (worst, (double) std::abs (y[i] - z[i]));

                check (worst == 0.0, "Output " + moveName (move) + " at " + rateName (fs)
                                         + ": 250 ms on, the output differs from a fresh instance at the target by "
                                         + std::to_string (worst));
            }
    }

    //== 7. An oversampling change costs no callback more than both paths ===
    // cd173eb warmed the new path at the bottom of the dip by running it over
    // 141 samples of missed input inside one callback: about 405 us, 242 % of
    // a 32-sample block at 192 kHz. The new path now runs alongside the old
    // one on the live input while the dip goes down, so no callback does
    // more than both paths' worth of a block. Counted, not timed: the work a
    // callback does is the oversampled samples it processes, summed over
    // channels and paths, and the bound is
    //
    //     channels x block x (old factor + new factor) + kSlack, kSlack = 0
    //
    // -- no constant is needed, because nothing is processed but the live
    // input. Away from a change a callback does exactly one path's worth:
    // the second path stops when the dip turns.
    {
        constexpr unsigned long long kSlack = 0;

        for (double fs : { 48000.0, 192000.0 })
            for (int block : { 32, 512 })
                for (int from : { 1, 2, 4, 8 })
                    for (int to : { 1, 2, 4, 8 })
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

                        const auto bound = 2ull * (unsigned long long) (block * (from + to)) + kSlack;

                        check (steadyBefore, where + ": a steady callback does one path's work");
                        check (worst <= bound, where + ": a callback during the change processed " + std::to_string (worst)
                                                   + " oversampled samples, past both paths' " + std::to_string (bound));
                        check (last == 2ull * (unsigned long long) (block * to),
                               where + ": after the change a callback does one path's work again ("
                                   + std::to_string (last) + ")");
                    }
    }

    if (failures == 0)
        std::cout << "All EQ switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
