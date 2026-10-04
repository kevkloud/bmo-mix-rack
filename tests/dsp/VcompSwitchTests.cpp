// LTV Comp: what its controls do to the audio at the moment they move, and
// what a stage that was out of use brings back with it when it returns.
//
// VcompDspTests holds what the module does once it has settled; this file
// holds the transitions -- a control changing while a signal passes, or after
// one has stopped -- which none of those tests can see, because each of them
// builds a fresh core, sets it once and measures the result.

#include "modules/vcomp/dsp/DspCore.h"
#include "modules/vcomp/presets/FactoryPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace bmo::vcomp;
using Params = DspCore::Params;

namespace
{
    int failures = 0;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> kRates { 44100.0, 48000.0, 96000.0, 192000.0 };

    // How the sections are run: mono, or stereo with the right channel a
    // different signal from the left and one of the two judged; and whether
    // the sections that only a mono pass needs are run.
    int  gChannels = 1;
    bool gTakeRight = false;
    bool gHeavy = true;
    std::string gMode;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << gMode << '\n'; ++failures; }
    }

    std::string rateName (double fs)
    {
        return std::to_string ((int) std::lround (fs / 100.0) / 10) + "."
             + std::to_string ((int) std::lround (fs / 100.0) % 10) + " kHz";
    }

    std::string fixed (double v, int places = 2)
    {
        char text[64];
        std::snprintf (text, sizeof text, "%.*f", places, v);
        return text;
    }

    double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

    Params standard (float amount, float outputDb = 0.0f)
    {
        Params p;
        p.amountPercent = amount;
        p.outputDb = outputDb;
        return p;
    }

    Params complexMode (float amount, float attackMs, float releaseMs, bool arc, float sidechainHz,
                        float lowHz, float highHz, float outputDb = 0.0f)
    {
        auto p = standard (amount, outputDb);
        p.complex     = true;
        p.attackMs    = attackMs;
        p.releaseMs   = releaseMs;
        p.arc         = arc;
        p.sidechainHz = sidechainHz;
        p.lowThruHz   = lowHz;
        p.highThruHz  = highHz;
        return p;
    }

    /** Render through a fresh core, mono, with the parameters chosen per
        block from the block's first sample and handed over every block, as
        the engine does. setParams comes before prepare, which is the
        engine's order. */
    std::vector<float> render (double fs, std::vector<float> signal, int block,
                               const std::function<Params (size_t)>& paramsAt)
    {
        DspCore core;
        core.setParams (paramsAt (0));
        core.prepare (fs, block, gChannels);

        // In stereo the right channel carries the same signal half a cycle
        // of nothing in particular away -- delayed 37 samples and at half
        // the level -- so the two channels' splits see different audio.
        std::vector<float> right (signal.size(), 0.0f);
        for (size_t i = 37; i < signal.size(); ++i)
            right[i] = 0.5f * signal[i - 37];

        for (size_t start = 0; start < signal.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, signal.size() - start);
            core.setParams (paramsAt (start));
            float* channels[2] { signal.data() + start, right.data() + start };
            core.process (channels, gChannels, n);
        }

        return gTakeRight ? right : signal;
    }

    /** A tone whose level changes at given times: `segments` is a list of
        (start second, dBFS peak), the last running to `seconds`. A level of
        -999 is digital silence. */
    std::vector<float> toneSegments (double fs, double hz, double seconds,
                                     std::vector<std::pair<double, double>> segments,
                                     double phase = 0.0)
    {
        std::vector<float> x ((size_t) (seconds * fs));

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto t = (double) i / fs;
            auto db = -999.0;

            for (const auto& s : segments)
                if (t >= s.first)
                    db = s.second;

            x[i] = db < -900.0 ? 0.0f : (float) (dbToLin (db) * std::sin (2.0 * kPi * hz * (double) i / fs + phase));
        }

        return x;
    }

    /** The worst level difference, in dB, between two renders over 10 ms
        windows from `from` to `to` seconds. */
    double worstWindowDb (const std::vector<float>& y, const std::vector<float>& ref, double fs,
                          double from, double to, double windowSec = 0.01)
    {
        const auto w = (size_t) (windowSec * fs);
        double worst = 0.0;

        for (auto a = (size_t) (from * fs); a + w <= std::min ((size_t) (to * fs), y.size()); a += w)
        {
            double ey = 0.0, er = 0.0;

            for (size_t i = a; i < a + w; ++i)
            {
                ey += (double) y[i] * y[i];
                er += (double) ref[i] * ref[i];
            }

            if (er <= 0.0 && ey <= 0.0)
                continue;

            worst = std::max (worst, std::abs (10.0 * std::log10 (std::max (ey, 1.0e-300) / std::max (er, 1.0e-300))));
        }

        return worst;
    }

    double peakFrom (const std::vector<float>& y, size_t from)
    {
        double p = 0.0;
        for (size_t i = from; i < y.size(); ++i)
            p = std::max (p, (double) std::abs (y[i]));
        return p;
    }

    double largestStep (const std::vector<float>& y, size_t from, size_t to)
    {
        double m = 0.0;
        for (size_t i = std::max<size_t> (from, 1); i < to && i < y.size(); ++i)
            m = std::max (m, (double) std::abs (y[i] - y[i - 1]));
        return m;
    }

    /** The bound the repository holds a switch to: the largest sample-to-
        sample step in the 250 ms after it, against the steady signal's own
        largest step before it and once everything has settled after it,
        worst of four starting phases of a -18 dBFS RMS sine an eighth of a
        cycle apart, so a switch cannot hide by landing where the two paths
        happen to agree. `moves` is a list of (second, parameters): the
        parameters in force from that time on, landing on the block
        boundary at or before it. Block 64, as hosts often use, so that a
        change lands somewhere other than where a 512 block would put it.

        `widest`, when given, is a setting held throughout whose steady
        signal also counts as the signal's own: for a move that passes
        through a louder state than either end -- a gate sent to its rail
        and straight back opens part of the way and closes again -- the
        signal at its loudest is the one to compare a step with. */
    double stepRatio (double fs, double hz, const std::vector<std::pair<double, Params>>& moves,
                      const Params* widest = nullptr, double seconds = 1.2, double firstMove = 0.4,
                      double judgeFor = 0.25)
    {
        constexpr double amplitude = 0.17782794 * 1.41421356;   // -18 dBFS RMS
        double worst = 0.0;

        for (int k = 0; k < 4; ++k)
        {
            std::vector<float> x ((size_t) (seconds * fs));
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));

            std::vector<std::pair<size_t, Params>> at;
            for (const auto& m : moves)
                at.push_back ({ (size_t) (m.first * fs) / 64 * 64, m.second });

            const auto y = render (fs, x, 64, [&] (size_t s)
            {
                auto p = at.front().second;
                for (const auto& a : at)
                    if (s >= a.first)
                        p = a.second;
                return p;
            });

            const auto sw     = (size_t) (firstMove * fs) / 64 * 64;
            const auto last   = at.back().first;
            const auto before = largestStep (y, sw - (size_t) (0.15 * fs), sw);
            const auto after  = largestStep (y, y.size() - (size_t) (0.2 * fs), y.size());
            const auto during = largestStep (y, sw, last + (size_t) (judgeFor * fs));

            auto own = std::max (before, after);

            if (widest != nullptr)
            {
                const auto w = render (fs, x, 64, [&] (size_t) { return *widest; });
                own = std::max (own, largestStep (w, y.size() - (size_t) (0.2 * fs), y.size()));
            }

            worst = std::max (worst, during / own);
        }

        return worst;
    }

    /** The level of a move, sample by sample, as the mean energy of an
        ensemble of renders: four phases of a sine an eighth of a cycle apart
        (whose energies sum to a constant, so the mean is the level with no
        window needed to average the cycle out), or `seeds` renders of white
        noise. Returned as the worst 2 ms level against the input's over
        [from, to) seconds, in dB, low and high. At AMOUNT 0 every band takes
        a gain of exactly 1, so the split on its own is an allpass and any
        departure from the input's level is the move's. */
    struct LevelSwing { double low = 0.0, high = 0.0; };

    LevelSwing levelSwing (double fs, double hz, int seeds, const std::function<Params (size_t)>& paramsAt,
                           double seconds, double from, double to)
    {
        const auto n = (size_t) (seconds * fs);
        std::vector<double> ey (n, 0.0), ex (n, 0.0);
        const auto members = hz > 0.0 ? 4 : seeds;

        for (int k = 0; k < members; ++k)
        {
            std::vector<float> x (n);
            unsigned state = 12345u + 7919u * (unsigned) k;

            for (size_t i = 0; i < n; ++i)
            {
                if (hz > 0.0)
                {
                    x[i] = (float) (0.25 * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));
                }
                else
                {
                    state = state * 1664525u + 1013904223u;
                    x[i] = (float) (0.25 * ((double) (state >> 8) / 8388608.0 - 1.0));
                }
            }

            const auto y = render (fs, x, 64, paramsAt);

            for (size_t i = 0; i < n; ++i)
            {
                ey[i] += (double) y[i] * y[i];
                ex[i] += (double) x[i] * x[i];
            }
        }

        LevelSwing s { 1.0e9, -1.0e9 };
        const auto w = std::max<size_t> (1, (size_t) (0.002 * fs));

        for (auto a = (size_t) (from * fs); a + w <= std::min (n, (size_t) (to * fs)); a += w / 2)
        {
            double sy = 0.0, sx = 0.0;
            for (size_t i = a; i < a + w; ++i) { sy += ey[i]; sx += ex[i]; }

            const auto db = 10.0 * std::log10 (sy / sx);
            s.low  = std::min (s.low, db);
            s.high = std::max (s.high, db);
        }

        return s;
    }
}

void sections()
{
    //== 1. A stage out of use does not bring back what it heard before ======
    //
    // Every piece of state the module can switch out of use, and what it
    // brings back when it returns. The question for each is the same: once
    // it is back in use, is the output what an instance that had it in use
    // all along would give?
    //
    // 1a. ARC's slow branch. With ARC off it used to stop -- not clear, stop
    //     -- so turning ARC back on, however long afterwards, released the
    //     reduction it had been holding when ARC went off: 3 s at -6 dBFS, ARC
    //     off, 10 s of silence, ARC on, and a -30 dBFS tone came out 12.4 dB
    //     low in its first 50 ms and 14.1 dB low half a second in. The same
    //     through COMPLEX, which is what loading the Manual preset (ARC off)
    //     and then any other does. All three branches now listen all the
    //     time, so whichever ARC setting comes back finds the state an
    //     instance that had it all along would have.
    //
    //     Judged in 10 ms windows from 50 ms after the return, against an
    //     instance with the returning setting throughout: the 50 ms is the
    //     switch's own 10 ms crossfade (1b) and the ATTACK, which takes up
    //     the difference between what the two settings were reducing by at
    //     that moment -- the difference ARC exists to make.
    {
        struct Case
        {
            const char* name;
            Params before, during, after, reference;    // `reference` replaces `during`
            std::vector<std::pair<double, double>> level;
            double offAt, onAt;
        };

        const auto arcOn  = complexMode (80.0f, 5.0f, 200.0f, true,  20.0f, kLowThruOffHz, kHighThruOffHz, -12.0f);
        auto       arcOff = arcOn;
        arcOff.arc = false;

        const auto stdMode   = standard (80.0f, -12.0f);
        const auto manualArc = complexMode (50.0f, 5.0f, 150.0f, true, kStandardSidechainHz, kLowThruOffHz, kHighThruOffHz, -12.0f);
        auto       manual    = manualArc;
        manual.arc = false;
        const auto fastVocal = complexMode (65.0f, 0.8f, 90.0f, true, 120.0f, kLowThruOffHz, kHighThruOffHz, -12.0f);

        const std::vector<std::pair<double, double>> afterSilence { { 0.0, -6.0 }, { 3.0, -999.0 }, { 13.0, -30.0 } };
        const std::vector<std::pair<double, double>> underTone    { { 0.0, -6.0 }, { 3.0, -30.0 } };

        const Case cases[] {
            { "ARC off, 10 s of silence, ARC on",                       arcOn,   arcOff, arcOn,   arcOn,   afterSilence, 3.0, 13.0 },
            { "COMPLEX with ARC off, 10 s of silence, standard",        stdMode, arcOff, stdMode, arcOn,   afterSilence, 3.0, 13.0 },
            { "the Manual preset, 10 s of silence, Fast Vocal",         fastVocal, manual, fastVocal, manualArc, afterSilence, 3.0, 13.0 },
            { "ARC off for 1 s under a -30 dBFS tone, ARC on",          arcOn,   arcOff, arcOn,   arcOn,   underTone,    3.0, 4.0 },
            { "ARC on for 0.2 s into a release, ARC off",               arcOff,  arcOn,  arcOff,  arcOff,  underTone,    3.0, 3.2 },
        };

        for (const auto fs : kRates)
            for (const auto& c : cases)
            {
                const auto x = toneSegments (fs, 1000.0, c.onAt + 5.0, c.level);
                const auto offAt = (size_t) (c.offAt * fs) / 64 * 64;
                const auto onAt  = (size_t) (c.onAt * fs) / 64 * 64;

                const auto y   = render (fs, x, 64, [&] (size_t s) { return s < offAt ? c.before : s < onAt ? c.during : c.after; });
                const auto ref = render (fs, x, 64, [&] (size_t s) { return s < offAt ? c.before : s < onAt ? c.reference : c.after; });

                const auto from = (double) onAt / fs;
                const auto worst = worstWindowDb (y, ref, fs, from + 0.05, from + 5.0);

                check (worst <= 0.1, std::string (c.name) + " at " + rateName (fs)
                                         + ": from 50 ms after the return the output is " + fixed (worst)
                                         + " dB from an instance that had the returning setting all along");
            }
    }

    // 1b. ARC switched while the reduction is moving does not step. The three
    //     branches disagree most just after a sustained phrase, and with a
    //     fast ATTACK the gain followed the switch in a few samples: ARC off
    //     0.3 s into the release of a 3 s phrase at -6 dBFS moved the gain
    //     by about 10 dB at once. The release stage's output now crosses
    //     from one setting's figure to the other's in 10 ms.
    {
        for (const auto fs : kRates)
            for (const auto hz : { 150.0, 1000.0 })
                for (const auto toOn : { false, true })
                {
                    double ratio = 0.0;

                    // Worst of four starting phases an eighth of a cycle apart,
                    // so the switch cannot hide where the signal is flat.
                    for (int k = 0; k < 4; ++k)
                    {
                        const auto on = complexMode (80.0f, 0.1f, 200.0f, true, 20.0f, kLowThruOffHz, kHighThruOffHz, -12.0f);
                        auto off = on;
                        off.arc = false;

                        const auto x = toneSegments (fs, hz, 4.0, { { 0.0, -6.0 }, { 3.0, -24.0 } }, k * kPi / 4.0);
                        const auto sw = (size_t) (3.3 * fs) / 64 * 64;
                        const auto& a = toOn ? off : on;
                        const auto& b = toOn ? on : off;

                        // Before ARC goes on, it has been off through the phrase
                        // too, so the switch is from one setting held throughout
                        // to the other.
                        const auto y = render (fs, x, 64, [&] (size_t s) { return s < sw ? a : b; });

                        double steady = 0.0, during = 0.0;
                        for (auto i = (size_t) (3.6 * fs); i < y.size(); ++i)
                            steady = std::max (steady, (double) std::abs (y[i] - y[i - 1]));
                        for (auto i = sw - 2000; i < sw; ++i)
                            steady = std::max (steady, (double) std::abs (y[i] - y[i - 1]));
                        for (auto i = sw; i < sw + (size_t) (0.05 * fs); ++i)
                            during = std::max (during, (double) std::abs (y[i] - y[i - 1]));

                        ratio = std::max (ratio, during / steady);
                    }

                    check (ratio < 1.5, std::string (toOn ? "ARC on" : "ARC off") + " 0.3 s into the release of a 3 s phrase, "
                                            + fixed (hz, 0) + " Hz, ATTACK 0.1 ms, at " + rateName (fs)
                                            + " steps " + fixed (ratio) + "x the signal's own");
                }
    }

    // 1c. The gate's hold. Moved to its rail the gate is off, and an off gate
    //     holds nothing -- but the hold counter used to stop where it was, so
    //     a gate turned back on in the quiet after a loud passage held itself
    //     open for up to 40 ms before it began to close, which a gate moved
    //     there from its rail does not. Its envelope is not kept for later:
    //     an envelope depends on the threshold, and a gate at its rail has
    //     none, so the right state to return to is the rail's, open.
    {
        for (const auto fs : kRates)
        {
            auto gated = standard (55.0f);
            gated.gateDb = -30.0f;
            const auto railed = standard (55.0f);

            const auto x = toneSegments (fs, 1000.0, 2.5, { { 0.0, -10.0 }, { 1.0, -50.0 } });
            const auto offAt = (size_t) (0.9 * fs) / 64 * 64;
            const auto onAt  = (size_t) (1.5 * fs) / 64 * 64;

            const auto y   = render (fs, x, 64, [&] (size_t s) { return s < offAt ? gated : s < onAt ? railed : gated; });
            const auto ref = render (fs, x, 64, [&] (size_t s) { return s < onAt ? railed : gated; });

            const auto worst = worstWindowDb (y, ref, fs, (double) onAt / fs, 2.5);
            check (worst <= 0.1, "GATE at its rail and back, in the quiet after a loud passage, at " + rateName (fs)
                                     + ": " + fixed (worst) + " dB from a gate moved off its rail without that history");
        }
    }

    // 1d. The band split's filters. Each side of the split is in circuit
    //     only while its control is off its rail, and a side left out used to
    //     keep its filter state frozen while the other side ran on -- only the
    //     whole split being bypassed cleared it. So with LOW THRU and HIGH
    //     THRU both in, moving one to its rail and back replayed what that
    //     side's filters (and the low band's alignment allpass) held when it
    //     went out, into whatever was playing when it came back. Here the
    //     side comes back 0.8 s into digital silence, after the module's own
    //     tail has died away, so anything near the bound is the replay.
    {
        const auto base = complexMode (50.0f, 5.0f, 200.0f, true, kStandardSidechainHz, 160.0f, 6000.0f, -12.0f);

        struct Side { const char* name; Params out; };
        auto lowOut = base;   lowOut.lowThruHz  = kLowThruOffHz;
        auto highOut = base;  highOut.highThruHz = kHighThruOffHz;
        auto bothOut = base;  bothOut.complex   = false;

        const Side sides[] {
            { "LOW THRU to its rail and back",  lowOut },
            { "HIGH THRU to its rail and back", highOut },
            { "COMPLEX off and on",             bothOut },
        };

        for (const auto fs : kRates)
            for (const auto& side : sides)
            {
                std::vector<float> x ((size_t) (2.5 * fs));
                for (size_t i = 0; i < (size_t) (1.2 * fs); ++i)
                {
                    const auto t = (double) i / fs;
                    x[i] = (float) (0.1 * (std::sin (2.0 * kPi * 100.0 * t) + std::sin (2.0 * kPi * 3000.0 * t)
                                           + std::sin (2.0 * kPi * 9000.0 * t)));
                }

                const auto offAt = (size_t) (1.0 * fs) / 64 * 64;
                const auto onAt  = (size_t) (2.0 * fs) / 64 * 64;

                const auto y = render (fs, x, 64, [&] (size_t s) { return s >= offAt && s < onAt ? side.out : base; });
                const auto peak = peakFrom (y, onAt);

                check (peak < 1.0e-6, std::string (side.name) + ", back in digital silence, at " + rateName (fs)
                                          + ": peaks at " + std::to_string (peak));
            }
    }

    //== 2. GATE moves without a step, to its rails and back ================
    //
    // Moved to its rail, the gate used to drop its envelope in one sample:
    // a closed gate snapped open, 19.6 dB in a sample, 144x the steady
    // signal's own largest step at 1 kHz and 1062x at 150 Hz. At the rail it
    // now opens at its own opening rate, the same 3 ms it opens at when a
    // signal crosses its threshold, and is exactly inert again once open.
    //
    // A -18 dBFS RMS sine peaks at -15 dBFS, so GATE -10 holds it closed and
    // GATE -40 leaves it open.
    //
    // At AMOUNT 0 the gate is the only thing acting, and that is where the
    // bound is held. With the compressor working behind it, any opening --
    // a signal crossing the threshold included -- hands the compressor a
    // level it takes ATTACK to catch, and that overshoot is the compressor's
    // onset, not a step in the gate: GATE -10 -> -40 at AMOUNT 55 measures
    // 1.55x at 1 kHz and 1.80x at 150 Hz on the code that predates this
    // section. So at AMOUNT 55 a move to the rail is held to that: no worse
    // than the gate opening for a signal.
    {
        auto at = [] (float gateDb, float amount = 0.0f)
        {
            auto p = standard (amount);
            p.gateDb = gateDb;
            return p;
        };

        struct Move { const char* name; std::vector<std::pair<double, Params>> moves; };

        const Move moves[] {
            { "GATE -10 -> -60 (closed, to the rail)",          { { 0.0, at (-10.0f) }, { 0.4, at (-60.0f) } } },
            { "GATE -10 -> -40 (closed, opens)",                { { 0.0, at (-10.0f) }, { 0.4, at (-40.0f) } } },
            { "GATE -60 -> -10 (from the rail, closes)",        { { 0.0, at (-60.0f) }, { 0.4, at (-10.0f) } } },
            { "GATE -40 -> -60 (open, to the rail)",            { { 0.0, at (-40.0f) }, { 0.4, at (-60.0f) } } },
            { "GATE -10 -> -60 -> -10 (to the rail and back)",  { { 0.0, at (-10.0f) }, { 0.4, at (-60.0f) }, { 0.402, at (-10.0f) } } },
            { "GATE -60 -> -10 -> -60 (closing, to the rail)",  { { 0.0, at (-60.0f) }, { 0.4, at (-10.0f) }, { 0.5, at (-60.0f) } } },
        };

        const auto open = at (kGateOffDb);

        for (const auto fs : kRates)
            for (const auto hz : { 150.0, 1000.0 })
            {
                for (const auto& m : moves)
                {
                    const auto ratio = stepRatio (fs, hz, m.moves, &open);
                    check (ratio < 1.5, std::string (m.name) + ", " + fixed (hz, 0) + " Hz, at " + rateName (fs)
                                            + " steps " + fixed (ratio) + "x the signal's own");
                }

                const auto openWorking = at (kGateOffDb, 55.0f);
                const auto opens  = stepRatio (fs, hz, { { 0.0, at (-10.0f, 55.0f) }, { 0.4, at (-40.0f, 55.0f) } }, &openWorking);
                const auto railed = stepRatio (fs, hz, { { 0.0, at (-10.0f, 55.0f) }, { 0.4, openWorking } }, &openWorking);

                check (railed <= opens * 1.05,
                       "GATE -10 -> -60 at AMOUNT 55, " + fixed (hz, 0) + " Hz, at " + rateName (fs) + " steps "
                           + fixed (railed) + "x the signal's own, against " + fixed (opens)
                           + "x for the gate opening to -40");
            }
    }

    //== 3. The band split moves without a step ===========================
    //
    // COMPLEX, LOW THRU and HIGH THRU used to switch the band split in and
    // out, and move its crossovers, in one sample: on a 150 Hz tone LOW THRU
    // 200 -> 20 stepped 104x the signal's own largest step, COMPLEX on ->
    // off 144x, HIGH THRU 20k -> 6k 35x. A side of the split now fades in
    // and out over 10 ms, and a crossover that is in circuit glides to a new
    // frequency rather than jumping there.
    //
    // Every move across each control's whole range, from its rail and back
    // to it and between two settings that are both in, with the other side
    // in and out; COMPLEX both ways, with the four detector knobs moved as
    // well and without; a move reversed 2 ms in. AMOUNT 55, so the two bands
    // are taking different gains and the move is audible as a level change
    // as well as a phase one.
    {
        auto at = [] (float lowHz, float highHz, bool complex = true)
        {
            auto p = complexMode (55.0f, 5.0f, 200.0f, true, kStandardSidechainHz, lowHz, highHz);
            p.complex = complex;
            return p;
        };

        auto knobs = complexMode (55.0f, 0.1f, 20.0f, false, 500.0f, 200.0f, 6000.0f);
        auto knobsOff = knobs;
        knobsOff.complex = false;

        constexpr float lo = kLowThruOffHz, hi = kHighThruOffHz;

        struct Move { const char* name; std::vector<std::pair<double, Params>> moves; };

        std::vector<Move> moves {
            { "COMPLEX off -> on (bands only)",       { { 0.0, at (200, 6000, false) }, { 0.4, at (200, 6000) } } },
            { "COMPLEX on -> off (bands only)",       { { 0.0, at (200, 6000) }, { 0.4, at (200, 6000, false) } } },
            { "COMPLEX off -> on (knobs moved)",      { { 0.0, knobsOff }, { 0.4, knobs } } },
            { "COMPLEX on -> off (knobs moved)",      { { 0.0, knobs }, { 0.4, knobsOff } } },
            { "LOW THRU 20 -> 200",                   { { 0.0, at (lo, hi) },   { 0.4, at (200, hi) } } },
            { "LOW THRU 200 -> 20",                   { { 0.0, at (200, hi) },  { 0.4, at (lo, hi) } } },
            { "LOW THRU 20 -> 500",                   { { 0.0, at (lo, hi) },   { 0.4, at (500, hi) } } },
            { "LOW THRU 500 -> 20",                   { { 0.0, at (500, hi) },  { 0.4, at (lo, hi) } } },
            { "LOW THRU 21 -> 500",                   { { 0.0, at (21, hi) },   { 0.4, at (500, hi) } } },
            { "LOW THRU 500 -> 21",                   { { 0.0, at (500, hi) },  { 0.4, at (21, hi) } } },
            { "LOW THRU 100 -> 400",                  { { 0.0, at (100, hi) },  { 0.4, at (400, hi) } } },
            { "LOW THRU 21 -> 500, HIGH THRU 6k",     { { 0.0, at (21, 6000) }, { 0.4, at (500, 6000) } } },
            { "LOW THRU 500 -> 20, HIGH THRU 6k",     { { 0.0, at (500, 6000) },{ 0.4, at (lo, 6000) } } },
            { "LOW THRU 20 -> 300 -> 20",             { { 0.0, at (lo, hi) },   { 0.4, at (300, hi) }, { 0.402, at (lo, hi) } } },
            { "HIGH THRU 20k -> 6k",                  { { 0.0, at (lo, hi) },   { 0.4, at (lo, 6000) } } },
            { "HIGH THRU 6k -> 20k",                  { { 0.0, at (lo, 6000) }, { 0.4, at (lo, hi) } } },
            { "HIGH THRU 20k -> 2k",                  { { 0.0, at (lo, hi) },   { 0.4, at (lo, 2000) } } },
            { "HIGH THRU 2k -> 20k",                  { { 0.0, at (lo, 2000) }, { 0.4, at (lo, hi) } } },
            { "HIGH THRU 19999 -> 2k",                { { 0.0, at (lo, 19999) },{ 0.4, at (lo, 2000) } } },
            { "HIGH THRU 2k -> 19999",                { { 0.0, at (lo, 2000) }, { 0.4, at (lo, 19999) } } },
            { "HIGH THRU 2k -> 19999, LOW THRU 200",  { { 0.0, at (200, 2000) },{ 0.4, at (200, 19999) } } },
            { "HIGH THRU 2k -> 20k, LOW THRU 200",    { { 0.0, at (200, 2000) },{ 0.4, at (200, hi) } } },
            { "HIGH THRU 20k -> 3k -> 20k",           { { 0.0, at (lo, hi) },   { 0.4, at (lo, 3000) }, { 0.402, at (lo, hi) } } },
        };

        // A knob dragged across its whole range and back, one new value a
        // millisecond, as automation or a mouse delivers it: every block
        // starts a new glide from wherever the last one had got to.
        auto sweep = [&] (const char* name, bool low, float from, float to)
        {
            Move m { name, { { 0.0, low ? at (from, hi) : at (lo, from) } } };

            for (int step = 1; step <= 400; ++step)
            {
                const auto there = step <= 200 ? step / 200.0 : (400 - step) / 200.0;
                const auto hz = (float) (from * std::pow (to / from, there));
                m.moves.push_back ({ 0.4 + step * 0.001, low ? at (hz, hi) : at (lo, hz) });
            }

            moves.push_back (m);
        };

        sweep ("LOW THRU dragged 21 -> 500 -> 21",    true,  21.0f,   500.0f);
        sweep ("HIGH THRU dragged 2k -> 19999 -> 2k", false, 2000.0f, 19999.0f);

        for (const auto fs : kRates)
            for (const auto hz : { 150.0, 1000.0 })
                for (const auto& m : moves)
                {
                    // COMPLEX on with the knobs moved puts SIDECHAIN at 500 Hz, so a
                    // 150 Hz tone stops driving the compressor and comes up on its
                    // makeup into the limiter -- for as long as it sits in the band
                    // that takes the makeup, which is until LOW THRU has glided past
                    // it. That is the settings, not the move; the loudest it is in
                    // transit is that setting with LOW THRU not yet in, and a step
                    // is judged against the signal at that level.
                    auto inTransit = knobs;
                    inTransit.lowThruHz = lo;
                    const auto widest = std::string (m.name) == "COMPLEX off -> on (knobs moved)" ? &inTransit : nullptr;
                    // Judged over the whole transition, not the first 0.25 s: a
                    // knob's glide, and the low side's way in by its edge, take
                    // up to a second, and until 2026-10-03 nothing after the
                    // first quarter of it was looked at.
                    const auto ratio = stepRatio (fs, hz, m.moves, widest, 2.6, 0.4, 1.95);
                    check (ratio < 1.5, std::string (m.name) + ", " + fixed (hz, 0) + " Hz, at " + rateName (fs)
                                            + " steps " + fixed (ratio) + "x the signal's own");
                }

        // In silence: the same moves 0.75 s into digital silence after a
        // tone, when the module's own tail has died away. A side that comes
        // in starts from rest, and a crossover gliding through silence makes
        // none of its own.
        for (const auto fs : kRates)
            for (const auto& m : moves)
            {
                std::vector<float> x ((size_t) (1.6 * fs));
                for (size_t i = 0; i < (size_t) (0.4 * fs); ++i)
                    x[i] = (float) (0.25 * std::sin (2.0 * kPi * 150.0 * (double) i / fs)
                                    + 0.25 * std::sin (2.0 * kPi * 3000.0 * (double) i / fs));

                std::vector<std::pair<size_t, Params>> moveAt;
                for (const auto& mv : m.moves)
                    moveAt.push_back ({ (size_t) ((mv.first > 0.0 ? mv.first + 0.75 : 0.0) * fs) / 64 * 64, mv.second });

                const auto y = render (fs, x, 64, [&] (size_t s)
                {
                    auto p = moveAt.front().second;
                    for (const auto& a : moveAt)
                        if (s >= a.first)
                            p = a.second;
                    return p;
                });

                const auto peak = peakFrom (y, moveAt[1].first);
                check (peak < 1.0e-6, std::string (m.name) + " in digital silence at " + rateName (fs)
                                          + " puts out a peak of " + std::to_string (peak));
            }
    }

    if (! gHeavy)
        return;

    //== 4. A smoothed parameter lands on its setting, exactly ==============
    //
    // AMOUNT and OUTPUT are smoothed, and the smoother used to stall short
    // of its target and stay there: a float one-pole whose step had rounded
    // away, 0.0007 to 0.011 short (dB of OUTPUT, % of AMOUNT) depending on
    // the rate and the move. So a setting held after a move never sounded
    // the way that setting sounds from a fresh instance. It now lands, in
    // bounded time, and then does no work.
    {
        // The smoother on its own: every move lands exactly on its target
        // within 200 ms and stays, and the last step onto the target is no
        // bigger than kSmootherLandWithin and the pole's own step at that
        // point, which is coeff of it: under 1 % of it at any of these rates.
        struct Move { float from, to; };
        const Move smootherMoves[] { { 0.0f, 100.0f }, { 100.0f, 0.0f }, { 0.0f, 55.0f }, { 55.0f, 54.9f },
                                     { -24.0f, 24.0f }, { 24.0f, -24.0f }, { 0.0f, -3.5f } };

        for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const auto& m : smootherMoves)
            {
                Smoother s;
                s.prepare (fs, 15.0);
                s.snap (m.from);
                s.setTarget (m.to);

                long landedAt = -1;
                auto last = m.from, landingStep = 0.0f;
                auto stays = true;

                for (long n = 0; n < (long) (0.5 * fs); ++n)
                {
                    const auto v = s.tick();

                    if (landedAt < 0 && v == m.to)
                    {
                        landedAt = n;
                        landingStep = std::abs (v - last);
                    }
                    else if (landedAt >= 0 && v != m.to)
                    {
                        stays = false;
                    }

                    last = v;
                }

                const auto name = "smoother " + fixed (m.from, 1) + " -> " + fixed (m.to, 1) + " at " + rateName (fs);
                check (landedAt >= 0 && landedAt < (long) (0.2 * fs) && stays,
                       name + " lands exactly on its target within 200 ms and stays"
                           + (landedAt < 0 ? std::string (" (never landed; " + std::to_string (last) + ")")
                                           : " (landed at " + fixed (1000.0 * (double) landedAt / fs, 1) + " ms)"));
                check (landingStep <= kSmootherLandWithin * 1.01f,
                       name + ": its last step onto the target is " + std::to_string (landingStep));
            }

        // Through the module: after a move of either, the output becomes bit
        // for bit what a fresh instance held at the target gives, once the
        // smoother has landed and the limiter, which the move may have
        // driven, has let go: from 1 s after the move.
        //
        // Bit for bit wherever the move leaves no history behind it but the
        // smoother's: OUTPUT is applied after the detector, so its moves are
        // checked under heavy compression, and AMOUNT on a tone below the
        // knee at both ends, where it moves only the makeup. An AMOUNT move
        // under compression also changes what the detector heard while it
        // moved, and the detector's one-poles are float: once two of them
        // are within a rounding step of the same steady state they can stay
        // a step apart for good, which no smoother can settle. That case is
        // held to 1e-4 dB instead, from 6 s after the move, once ARC's 2 s
        // release has forgotten it.
        struct Knob { const char* name; Params from, to; double toneDb; bool exact; };
        const Knob knobs[] {
            { "OUTPUT -24 -> -6",                    standard (55.0f, -24.0f), standard (55.0f, -6.0f), -15.0, true },
            { "OUTPUT +24 -> -6 (off the ceiling)",  standard (55.0f, 24.0f),  standard (55.0f, -6.0f), -15.0, true },
            { "OUTPUT -3.5 -> -12",                  standard (55.0f, -3.5f),  standard (55.0f, -12.0f), -15.0, true },
            { "AMOUNT 20 -> 55, below the knee",     standard (20.0f),         standard (55.0f),        -40.0, true },
            { "AMOUNT 0 -> 55, below the knee",      standard (0.0f),          standard (55.0f),        -40.0, true },
            { "AMOUNT 55 -> 30, below the knee",     standard (55.0f),         standard (30.0f),        -40.0, true },
            { "AMOUNT 20 -> 55, compressing",        standard (20.0f),         standard (55.0f),        -15.0, false },
            { "AMOUNT 100 -> 55, compressing",       standard (100.0f),        standard (55.0f),        -15.0, false },
        };

        for (const auto fs : kRates)
            for (const auto& k : knobs)
            {
                const auto x = toneSegments (fs, 1000.0, 8.0, { { 0.0, k.toneDb } });
                const auto sw = (size_t) (0.2 * fs) / 64 * 64;
                const auto from = sw + (size_t) ((k.exact ? 1.0 : 6.0) * fs);

                const auto y     = render (fs, x, 64, [&] (size_t s) { return s < sw ? k.from : k.to; });
                const auto fresh = render (fs, x, 64, [&] (size_t) { return k.to; });

                auto differ = 0;
                auto worstDb = 0.0;

                for (size_t i = from; i < y.size(); ++i)
                {
                    differ += y[i] != fresh[i] ? 1 : 0;

                    if (std::abs (fresh[i]) > 0.25 * dbToLin (k.toneDb))
                        worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (std::abs ((double) y[i] / fresh[i]))));
                }

                if (k.exact)
                    check (differ == 0, std::string (k.name) + " at " + rateName (fs) + ": from 1 s after the move, "
                                            + std::to_string (differ) + " samples differ from a fresh instance at the target ("
                                            + std::to_string (worstDb) + " dB)");
                else
                    check (worstDb <= 1.0e-4, std::string (k.name) + " at " + rateName (fs) + ": from 6 s after the move, "
                                                  + std::to_string (worstDb) + " dB from a fresh instance at the target");
            }
    }

    //== 5. A side of the split comes in and goes out without a dip =========
    //
    // A side used to fade from the dry signal to the split's reconstruction,
    // which is an allpass of it, and the two are in anti-phase at the
    // crossover: half way through the 10 ms fade a tone at the crossover
    // frequency cancelled completely, and anything within about an octave of
    // it dipped by more than 3 dB. A side now comes in with its crossover at
    // the far edge of its range, where the allpass is a wire across the
    // audio band, and glides to its setting; it goes out the same way round.
    //
    // Measured as the level of an ensemble (levelSwing), at AMOUNT 0 so the
    // split is the only thing acting, for a tone at the crossover, an octave
    // either side of it and white noise; the worst 2 ms is held to within
    // 1 dB of the input's level, either way, through the whole transition.
    {
        auto at = [] (float lowHz, float highHz, bool complex = true)
        {
            auto p = complexMode (0.0f, 5.0f, 200.0f, true, kStandardSidechainHz, lowHz, highHz);
            p.complex = complex;
            return p;
        };

        constexpr float lo = kLowThruOffHz, hi = kHighThruOffHz;

        struct Move { std::string name; Params from, to; std::vector<double> crossovers; };
        std::vector<Move> moves;

        for (const auto fc : { 30.0f, 200.0f, 500.0f })
        {
            moves.push_back ({ "LOW THRU 20 -> " + fixed (fc, 0), at (lo, hi), at (fc, hi), { fc } });
            moves.push_back ({ "LOW THRU " + fixed (fc, 0) + " -> 20", at (fc, hi), at (lo, hi), { fc } });
        }

        for (const auto fc : { 2000.0f, 6000.0f, 15000.0f })
        {
            moves.push_back ({ "HIGH THRU 20k -> " + fixed (fc, 0), at (lo, hi), at (lo, fc), { fc } });
            moves.push_back ({ "HIGH THRU " + fixed (fc, 0) + " -> 20k", at (lo, fc), at (lo, hi), { fc } });
        }

        moves.push_back ({ "HIGH THRU 20k -> 6000, LOW THRU 200", at (200, hi), at (200, 6000), { 200.0, 6000.0 } });
        moves.push_back ({ "LOW THRU 200 -> 20, HIGH THRU 6000",  at (200, 6000), at (lo, 6000), { 200.0, 6000.0 } });
        // COMPLEX is not here: it is a switch, and a switch goes through a
        // dip by decision (section 10), which this section would read as one.

        for (const auto fs : kRates)
            for (const auto& m : moves)
            {
                std::vector<double> tones;
                for (const auto fc : m.crossovers)
                    for (const auto f : { fc, 0.5 * fc, 2.0 * fc })
                        if (f < 0.45 * fs && f >= 20.0)
                            tones.push_back (f);
                tones.push_back (0.0);   // noise

                const auto sw = (size_t) (0.5 * fs) / 64 * 64;

                for (const auto hz : tones)
                {
                    const auto s = levelSwing (fs, hz, 16, [&] (size_t i) { return i < sw ? m.from : m.to; },
                                               1.6, (double) sw / fs, (double) sw / fs + 1.0);

                    const auto what = m.name + ", " + (hz > 0.0 ? fixed (hz, 0) + " Hz" : std::string ("noise"))
                                    + ", at " + rateName (fs);

                    check (s.low >= -1.0, what + " dips " + fixed (s.low) + " dB");
                    check (s.high <= 1.0, what + " rises " + fixed (s.high) + " dB");
                }
            }
    }

}

//== 6. The split stays flat while a crossover glides ==========================
//
// A crossover in circuit glides rather than jumps, and a gliding allpass is not
// quite an allpass: how far a tone's level wobbles while one sweeps past it
// depends on how many of the tone's cycles the sweep takes per octave (see
// kCrossoverGlideCycles). Full-range glides of each side, alone and with the
// other side in, at AMOUNT 0 so the split is all that acts, for tones across
// the band and white noise: the worst 2 ms level is held to within 1 dB of the
// input's either way. The worst figure is printed.
void glideFlatness()
{
    auto at = [] (float lowHz, float highHz)
    {
        return complexMode (0.0f, 5.0f, 200.0f, true, kStandardSidechainHz, lowHz, highHz);
    };

    constexpr float lo = kLowThruOffHz, hi = kHighThruOffHz;

    struct Move { const char* name; Params from, to; };
    const Move moves[] {
        { "LOW THRU 21 -> 500",                  at (21, hi),     at (500, hi) },
        { "LOW THRU 500 -> 21",                  at (500, hi),    at (21, hi) },
        { "HIGH THRU 2k -> 19999",               at (lo, 2000),   at (lo, 19999) },
        { "HIGH THRU 19999 -> 2k",               at (lo, 19999),  at (lo, 2000) },
        { "LOW THRU 21 -> 500, HIGH THRU 6k",    at (21, 6000),   at (500, 6000) },
        { "HIGH THRU 2k -> 19999, LOW THRU 200", at (200, 2000),  at (200, 19999) },
    };

    double worst = 0.0;
    std::string where;

    for (const auto fs : { 48000.0, 192000.0 })
        for (const auto& m : moves)
            for (const auto hz : { 30.0, 60.0, 150.0, 400.0, 1000.0, 3000.0, 8000.0, 15000.0, 0.0 })
            {
                const auto sw = (size_t) (0.5 * fs) / 64 * 64;
                const auto s = levelSwing (fs, hz, 8, [&] (size_t i) { return i < sw ? m.from : m.to; },
                                           2.0, (double) sw / fs, (double) sw / fs + 1.2);

                const auto what = std::string (m.name) + ", " + (hz > 0.0 ? fixed (hz, 0) + " Hz" : std::string ("noise"))
                                + ", at " + rateName (fs);

                check (s.low >= -1.0 && s.high <= 1.0,
                       what + ": the level moves " + fixed (s.low) + " / +" + fixed (s.high) + " dB while it glides");

                if (std::max (-s.low, s.high) > worst)
                {
                    worst = std::max (-s.low, s.high);
                    where = what;
                }
            }

    std::cout << "glide flatness: worst " << fixed (worst) << " dB (" << where << ")\n";
}

//== 7. COMPLEX back on agrees with an instance that never left it =============
//
// COMPLEX off for a second under a steady programme, then on again, against an
// instance that had it on throughout. Everything the module keeps listening
// while unused (ARC's three branches, the gate) lands where the other has it;
// what has to catch up is the split coming back in by its edges (up to about
// 1 s for LOW THRU, see BandSplit) and the release branches settling from
// standard mode's times to the knobs'. Held to: within 0.1 dB, in 10 ms
// windows, from 1.5 s after COMPLEX comes back. The time it takes is printed.
void complexReturn()
{
    const auto on = complexMode (55.0f, 5.0f, 150.0f, false, 120.0f, 160.0f, 6000.0f);
    auto off = on;
    off.complex = false;

    for (const auto fs : kRates)
    {
        std::vector<float> x ((size_t) (8.0 * fs));

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto t = (double) i / fs;
            const auto env = dbToLin (-20.0 + 8.0 * std::sin (2.0 * kPi * 3.0 * t));
            x[i] = (float) (env * 0.5 * (std::sin (2.0 * kPi * 110.0 * t) + std::sin (2.0 * kPi * 440.0 * t)
                                         + 0.5 * std::sin (2.0 * kPi * 2500.0 * t) + 0.3 * std::sin (2.0 * kPi * 9000.0 * t)));
        }

        const auto offAt = (size_t) (2.0 * fs) / 64 * 64;
        const auto onAt  = (size_t) (3.0 * fs) / 64 * 64;

        const auto y   = render (fs, x, 64, [&] (size_t s) { return s >= offAt && s < onAt ? off : on; });
        const auto ref = render (fs, x, 64, [&] (size_t) { return on; });

        const auto w = (size_t) (0.01 * fs);
        auto lastApart = onAt;

        for (auto a = onAt; a + w <= y.size(); a += w)
        {
            double ey = 0.0, er = 0.0;
            for (size_t i = a; i < a + w; ++i) { ey += (double) y[i] * y[i]; er += (double) ref[i] * ref[i]; }

            if (std::abs (10.0 * std::log10 (std::max (ey, 1.0e-300) / std::max (er, 1.0e-300))) > 0.1)
                lastApart = a + w;
        }

        const auto agreeAfter = (double) (lastApart - onAt) / fs;
        check (agreeAfter <= 1.5, "COMPLEX back on at " + rateName (fs) + " agrees with an instance that never left it within 0.1 dB only "
                                      + fixed (agreeAfter) + " s later");

        std::cout << "COMPLEX back on at " << rateName (fs) << ": within 0.1 dB of an instance that never left it after "
                  << fixed (agreeAfter) << " s\n";
    }
}

//== 8. A gate back off its rail closes at its own closing rate ================
//
// At its rail the gate is off and has no envelope, so a gate moved back off it
// in silence starts open and closes the way any gate does when its threshold
// rises over a quiet signal: no hold (an off gate holds nothing), then the
// 150 ms closing pole towards the 60 dB floor -- 4.1 dB in the first 512
// samples at 48 kHz, 40 dB at 165 ms, 58.9 dB at 0.6 s. Returning shut
// instead would drop whatever is under the threshold by up to 60 dB in one
// sample, the step section 2 exists to forbid, and would make a gate's state
// depend on a threshold it did not have while it was off.
void gateReturn()
{
    for (const auto fs : kRates)
    {
        Gate gate;
        gate.prepare (fs);
        gate.setThreshold (-30.0f);

        for (int i = 0; i < (int) (0.5 * fs); ++i) gate.process (-10.0f);
        for (int i = 0; i < (int) (2.0 * fs); ++i) gate.process (-140.0f);

        const auto shut = gate.currentAttenuationDb();

        gate.setThreshold (kGateOffDb);
        for (int i = 0; i < (int) (0.5 * fs); ++i) gate.process (-140.0f);

        check (gate.currentAttenuationDb() == 0.0f, "GATE at its rail at " + rateName (fs) + " is all the way open");

        gate.setThreshold (-30.0f);

        const auto pole = std::exp (-1.0 / (fs * (double) kGateCloseMs * 0.001));
        auto expected = 0.0, worst = 0.0;

        for (int n = 1; n <= (int) (0.6 * fs); ++n)
        {
            gate.process (-140.0f);
            expected = pole * expected + (1.0 - pole) * (double) kGateRangeDb;
            worst = std::max (worst, std::abs ((double) gate.currentAttenuationDb() - expected));
        }

        check (shut > 59.9f, "the gate was shut before its rail at " + rateName (fs));
        check (worst <= 0.1, "GATE back off its rail in silence at " + rateName (fs) + " closes "
                                  + fixed (worst, 4) + " dB away from its own closing curve");

        if (fs == 48000.0)
        {
            Gate g;
            g.prepare (fs);
            g.setThreshold (-30.0f);
            double at512 = 0.0, at165 = 0.0, at600 = 0.0;

            for (int n = 1; n <= (int) (0.6 * fs); ++n)
            {
                g.process (-140.0f);
                if (n == 512)                    at512 = g.currentAttenuationDb();
                if (n == (int) (0.165 * fs))     at165 = g.currentAttenuationDb();
                if (n == (int) (0.6 * fs))       at600 = g.currentAttenuationDb();
            }

            check (std::abs (at512 - 4.12) < 0.05 && std::abs (at165 - 40.0) < 0.1 && std::abs (at600 - 58.9) < 0.1,
                   "a gate leaving its rail in silence at 48 kHz is down " + fixed (at512) + " / " + fixed (at165)
                       + " / " + fixed (at600) + " dB at 512 samples / 165 ms / 0.6 s, not 4.12 / 40.0 / 58.9");
        }
    }
}

//== 9. Held settings render exactly as they did before any of this ============
//
// Every change in this file's sections had to leave a held setting's output
// bit for bit what it was at 6f6b8c3, the commit before the first of them.
// Two pins per row, both printed by this file built against 6f6b8c3 (MSVC
// x64, Release, /fp:precise, on ICE QUEEN): `vcomp_switch_tests
// --print-hashes`.
//
// - **The RMS and peak of each channel, held to 1e-4 dB on every compiler.**
//   The Linux and macOS jobs cannot hold a hash -- float results are promised
//   bit-identical only within one compiler and maths library -- and until
//   2026-10-03 they printed it and asserted nothing. A level this close is
//   far tighter than any change that matters and far looser than a library's
//   last-bit differences.
// - **The exact hash, on MSVC x64**, where the pins were made: there it is
//   bit for bit or nothing.
struct HeldRender { uint64_t hash; double rmsDb[2], peakDb[2]; };

HeldRender heldRender (double fs, int mode)
{
    Params p;

    if (mode == 0)
    {
        p = standard (80.0f, 6.0f);         // compressing hard, into the limiter
        p.gateDb = -40.0f;
    }
    else
    {
        p = complexMode (70.0f, 3.0f, 150.0f, true, 120.0f, 160.0f, 6000.0f, 6.0f);
        p.gateDb = -40.0f;
    }

    const auto n = (size_t) (2.0 * fs);
    std::vector<float> left (n), right (n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / fs;
        const auto env = std::fmod (t, 0.5) < 0.3 ? dbToLin (-8.0 + 6.0 * std::sin (2.0 * kPi * 2.0 * t)) : dbToLin (-55.0);
        left[i]  = (float) (env * (0.6 * std::sin (2.0 * kPi * 140.0 * t) + 0.3 * std::sin (2.0 * kPi * 1300.0 * t)
                                   + 0.1 * std::sin (2.0 * kPi * 9500.0 * t)));
        right[i] = (float) (0.7 * env * std::sin (2.0 * kPi * 230.0 * t + 0.4));
    }

    DspCore core;
    core.setParams (p);
    core.prepare (fs, 512, 2);

    for (size_t at = 0; at < n; at += 512)
    {
        const auto count = (int) std::min<size_t> (512, n - at);
        core.setParams (p);
        float* channels[2] { left.data() + at, right.data() + at };
        core.process (channels, 2, count);
    }

    HeldRender r { 1469598103934665603ull, {}, {} };
    int c = 0;

    for (const auto* side : { &left, &right })
    {
        double sum = 0.0, peak = 0.0;

        for (const auto v : *side)
        {
            uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int k = 0; k < 4; ++k) { r.hash ^= (u >> (8 * k)) & 0xffu; r.hash *= 1099511628211ull; }

            sum += (double) v * v;
            peak = std::max (peak, (double) std::abs (v));
        }

        r.rmsDb[c]  = 10.0 * std::log10 (sum / (double) side->size());
        r.peakDb[c] = 20.0 * std::log10 (peak);
        ++c;
    }

    return r;
}

struct Pinned { double fs; int mode; uint64_t hash; double rmsDb[2], peakDb[2]; };

const Pinned kPinned[] {
    { 44100.0, 0, 0xd2a8a38afda2dae0ull, { -9.694980, -9.410685 }, { -0.099999, -0.099999 } },
    { 44100.0, 1, 0xc7613ccfa6001f25ull, { -8.301412, -9.991611 }, { -0.099999, -0.099999 } },
    { 48000.0, 0, 0xd3f393ebd20ffad2ull, { -9.689943, -9.405692 }, { -0.099999, -0.099999 } },
    { 48000.0, 1, 0x18f0437f7442e53cull, { -8.306237, -9.997872 }, { -0.099999, -0.099999 } },
    { 96000.0, 0, 0x424735c13e660a0eull, { -9.725237, -9.441162 }, { -0.099999, -0.099999 } },
    { 96000.0, 1, 0xb46e7a59b3d2e1beull, { -8.335780, -10.037542 }, { -0.099999, -0.099999 } },
};

void heldIsUnchanged (bool print)
{
    for (const auto& p : kPinned)
    {
        const auto r = heldRender (p.fs, p.mode);

        if (print)
        {
            std::printf ("    { %.1f, %d, 0x%016llxull, { %.6f, %.6f }, { %.6f, %.6f } },\n", p.fs, p.mode,
                         (unsigned long long) r.hash, r.rmsDb[0], r.rmsDb[1], r.peakDb[0], r.peakDb[1]);
            continue;
        }

        const auto what = std::string (p.mode == 0 ? "standard" : "COMPLEX, both sides in") + " held at " + rateName (p.fs);

        for (int c = 0; c < 2; ++c)
        {
            check (std::abs (r.rmsDb[c] - p.rmsDb[c]) <= 1.0e-4,
                   what + ", channel " + std::to_string (c) + ": RMS " + fixed (r.rmsDb[c], 6) + " dB, at 6f6b8c3 " + fixed (p.rmsDb[c], 6));
            check (std::abs (r.peakDb[c] - p.peakDb[c]) <= 1.0e-4,
                   what + ", channel " + std::to_string (c) + ": peak " + fixed (r.peakDb[c], 6) + " dB, at 6f6b8c3 " + fixed (p.peakDb[c], 6));
        }

#if defined (_MSC_VER) && defined (_M_X64)
        check (r.hash == p.hash, what + " renders bit for bit as at 6f6b8c3");
#endif
    }
}

//== 10. COMPLEX switches through a dip ========================================
//
// The owner's decision, 2026-10-03: a *switch* that brings sides of the split
// in or out goes through a short dip, and the glide is for knobs. COMPLEX is
// that switch: the output fades to nothing, the split changes at the bottom
// with its crossovers already at their settings and warmed on the input, and
// the output comes back. Before this, a COMPLEX switch took the knob's way in
// -- each side by its edge and a glide -- and the band in transit spent up to
// a second at the wrong gain: the suite's voice +8.4 dB off where it settles
// for 0.61 s, a 60 Hz tone +7.2 dB for 0.95 s with the limiter taking 2.2 dB
// more than it settles to.
//
// Judged at AMOUNT 55 with the reviewer's knobs -- ATTACK 0.1, RELEASE 20,
// ARC off, HIGH THRU 6000, LOW THRU 200 and 500, SIDECHAIN 20 and 500 -- so
// the compressor is working and the two modes disagree about everything.

/** The suite's voice (tests/plugin/TestUtil.h voice()) at any rate. */
std::vector<float> suiteVoice (double fs, size_t n)
{
    std::vector<float> out (n);
    double sumSquares = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / fs;
        const auto beat = std::fmod (t, 0.55);
        const auto env = (std::fmod (t, 3.0) < 1.6 ? 1.0 : 0.0)
                       * (beat < 0.01 ? beat / 0.01 : std::exp (-(beat - 0.01) * 7.0));
        double sum = 0.0;

        for (int h = 1; h <= 120 && 75.0 * h <= fs * 0.49; ++h)
            sum += std::pow ((double) h, -1.4) * std::sin (2.0 * kPi * 75.0 * h * t);

        out[i] = (float) (env * sum);
        sumSquares += (double) out[i] * out[i];
    }

    const auto gain = dbToLin (-18.0) / std::sqrt (sumSquares / (double) n);

    for (auto& v : out)
        v = (float) (v * gain);

    return out;
}

/** Mono, any block size, with a hook called before each block. */
std::vector<float> renderBlocks (double fs, std::vector<float> x, int block, const std::function<Params (size_t)>& paramsAt,
                                 const std::function<void (DspCore&, size_t)>& hook = {})
{
    DspCore core;
    core.setParams (paramsAt (0));
    core.prepare (fs, block, 1);

    for (size_t start = 0; start < x.size(); start += (size_t) block)
    {
        const auto n = (int) std::min ((size_t) block, x.size() - start);
        if (hook) hook (core, start);
        core.setParams (paramsAt (start));
        auto* p = x.data() + start;
        core.process (&p, 1, n);
    }

    return x;
}

/** The limiter's reduction at each sample, read without the meter: the same
    render 24 dB lower (OUTPUT is a pure gain ahead of the limiter, and the
    detector never sees it, so that render is the pre-limiter signal less 24
    dB, with the limiter idle) against the real one. */
std::vector<float> limiterReduction (const std::vector<float>& y, const std::vector<float>& quiet)
{
    std::vector<float> r (y.size(), 0.0f);

    for (size_t i = 0; i < y.size(); ++i)
    {
        const auto pre = std::abs ((double) quiet[i]) * dbToLin (24.0);
        if (pre > 1.0e-3 && y[i] != 0.0f)
            r[i] = (float) std::max (0.0, 20.0 * std::log10 (pre / std::abs ((double) y[i])));
    }

    return r;
}

Params reviewerKnobs (float lowHz, float sidechainHz, bool complex)
{
    auto p = complexMode (55.0f, 0.1f, 20.0f, false, sidechainHz, lowHz, 6000.0f);
    p.complex = complex;
    return p;
}

/** What a COMPLEX switch does to the level, against what the compressor's own
    memory does to it.

    A switch changes the detector as well as the split, and a detector that
    has been running one RELEASE, ARC and SIDECHAIN takes its own time to
    become one that has always run the others -- with ARC on, seconds. That is
    the compressor, and no way of switching the split can hurry it. So the
    switch is judged against the same switch made with LOW THRU and HIGH THRU
    at their rails, where it moves the detector and nothing else, at the same
    sample: each is compared with an instance that was always in its new
    mode, and the difference between the two comparisons is what the split
    and the dip add. That is held to 1 dB in every 10 ms window from the end
    of the dip, and the limiter to no more than 0.5 dB over what the
    detector-only switch makes it take. The time the switch itself is more
    than 1 dB off the always-new instance is printed, beside the
    detector-only switch's, which is what explains it. */
void complexSwitchLevels()
{
    double worstExtra = 0.0, worstLimit = -1.0e9, latest = 0.0, latestDetector = 0.0;
    std::string extraWhere, limitWhere;

    for (const auto fs : { 48000.0, 96000.0 })
        for (const auto lowHz : { 200.0f, 500.0f })
            for (const auto sc : { 20.0f, 500.0f })
                for (const auto toOn : { true, false })
                {
                    const auto from = reviewerKnobs (lowHz, sc, ! toOn), to = reviewerKnobs (lowHz, sc, toOn);
                    auto railFrom = from, railTo = to;
                    railFrom.lowThruHz = railTo.lowThruHz = kLowThruOffHz;
                    railFrom.highThruHz = railTo.highThruHz = kHighThruOffHz;

                    const auto quieter = [] (Params p) { p.outputDb -= 24.0f; return p; };

                    const auto n    = (size_t) (2.5 * fs);
                    const auto sw   = (size_t) (1.0 * fs) / 64 * 64;
                    const auto half = (size_t) std::lround (0.014 * fs);    // DspCore::kComplexDipMs
                    const auto back = sw + 2 * half + 1;

                    for (const auto hz : { 60.0, 150.0, 300.0, 0.0 })
                    {
                        const auto members = hz > 0.0 ? 4 : 1;
                        std::vector<double> ey (n, 0.0), er (n, 0.0), e2 (n, 0.0), e2r (n, 0.0);
                        double limY = 0.0, limR = 0.0, lim2 = 0.0, lim2r = 0.0;

                        for (int k = 0; k < members; ++k)
                        {
                            std::vector<float> x;

                            if (hz > 0.0)
                            {
                                x.resize (n);
                                for (size_t i = 0; i < n; ++i)
                                    x[i] = (float) (0.17782794 * 1.41421356 * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));
                            }
                            else
                            {
                                x = suiteVoice (fs, n);
                            }

                            auto run = [&] (const Params& a, const Params& b, size_t at, std::vector<double>& energy, double& limit)
                            {
                                const auto y = renderBlocks (fs, x, 64, [&] (size_t s) { return s < at ? a : b; });
                                const auto q = renderBlocks (fs, x, 64, [&] (size_t s) { return s < at ? quieter (a) : quieter (b); });
                                const auto l = limiterReduction (y, q);

                                for (size_t i = 0; i < n; ++i)
                                    energy[i] += (double) y[i] * y[i];

                                for (auto i = sw; i < n; ++i)
                                    limit = std::max (limit, (double) l[i]);
                            };

                            run (from, to, sw, ey, limY);
                            run (to, to, 0, er, limR);
                            run (railFrom, railTo, sw + half, e2, lim2);
                            run (railTo, railTo, 0, e2r, lim2r);
                        }

                        const auto name = std::string (toOn ? "COMPLEX off -> on" : "COMPLEX on -> off") + ", LOW " + fixed (lowHz, 0)
                                        + ", SIDECHAIN " + fixed (sc, 0) + ", " + (hz > 0.0 ? fixed (hz, 0) + " Hz" : std::string ("voice"))
                                        + ", at " + rateName (fs);

                        const auto w = (size_t) (0.01 * fs);
                        double extra = 0.0, lastOff = 0.0, lastOffDetector = 0.0;

                        auto windowDb = [&] (const std::vector<double>& e, size_t a)
                        {
                            double s = 0.0;
                            for (size_t i = a; i < a + w; ++i) s += e[i];
                            return 10.0 * std::log10 (std::max (s, 1.0e-300));
                        };

                        for (auto a = sw; a + w <= n; a += w)
                        {
                            if (windowDb (er, a) - 10.0 * std::log10 ((double) (w * (size_t) members)) < -60.0)
                                continue;

                            const auto dy = windowDb (ey, a) - windowDb (er, a);
                            const auto d2 = windowDb (e2, a) - windowDb (e2r, a);

                            // Worse than the detector-only switch, not merely different:
                            // the split coming in settled can be nearer the always-new
                            // instance than a detector that is still catching up.
                            if (a >= back && std::abs (dy) - std::abs (d2) > extra)
                                extra = std::abs (dy) - std::abs (d2);

                            if (std::abs (dy) > 1.0) lastOff = (double) (a + w - sw) / fs;
                            if (std::abs (d2) > 1.0) lastOffDetector = (double) (a + w - sw) / fs;
                        }

                        check (extra <= 1.0,
                               name + ": from the end of the dip, " + fixed (extra) + " dB off the always-new instance"
                                   + " beyond what the same switch with the split at its rails is");

                        const auto excessY = limY - limR, excess2 = std::max (0.0, lim2 - lim2r);
                        check (excessY <= excess2 + 0.5,
                               name + ": the limiter takes " + fixed (excessY) + " dB over its settled figure, against "
                                   + fixed (excess2) + " for the switch with the split at its rails");

                        latest = std::max (latest, lastOff);
                        latestDetector = std::max (latestDetector, lastOffDetector);

                        if (extra > worstExtra) { worstExtra = extra; extraWhere = name; }
                        if (excessY - excess2 > worstLimit) { worstLimit = excessY - excess2; limitWhere = name; }
                    }
                }

    std::cout << "COMPLEX switch: worst 10 ms window from the end of the dip " << fixed (worstExtra) << " dB beyond the detector-only switch ("
              << extraWhere << "); more than 1 dB off the always-new instance until " << fixed (latest, 3) << " s, the detector-only switch until "
              << fixed (latestDetector, 3) << " s; limiter at most " << fixed (worstLimit) << " dB over the detector-only switch's excess ("
              << limitWhere << ")\n";
}

void complexSwitchSteps()
{
    double worst = 0.0;
    std::string where;

    const auto from = reviewerKnobs (200.0f, 500.0f, false), to = reviewerKnobs (200.0f, 500.0f, true);

    for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto block : { 1, 32, 441, 512 })
            for (const auto hz : { 20.0, 30.0, 60.0, 150.0, 300.0, 1000.0, 0.0 })
            {
                // Block 1 is a sample-by-sample host and costs the most to run;
                // it is judged at the rates at either end.
                if (block == 1 && fs != 48000.0 && fs != 192000.0)
                    continue;

                const auto n  = (size_t) (0.7 * fs);
                const auto sw = (size_t) (0.4 * fs) / (size_t) block * (size_t) block;

                for (int k = 0; k < (hz > 0.0 ? 2 : 1); ++k)
                {
                    std::vector<float> x;

                    if (hz > 0.0)
                    {
                        x.resize (n);
                        for (size_t i = 0; i < n; ++i)
                            x[i] = (float) (0.17782794 * 1.41421356 * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));
                    }
                    else
                    {
                        x = suiteVoice (fs, n);
                    }

                    const auto heldFrom = renderBlocks (fs, x, block, [&] (size_t) { return from; });
                    const auto heldTo   = renderBlocks (fs, x, block, [&] (size_t) { return to; });
                    const auto own = std::max (largestStep (heldFrom, sw / 2, n), largestStep (heldTo, sw / 2, n));

                    for (const auto toOn : { true, false })
                    {
                        const auto& a = toOn ? from : to;
                        const auto& b = toOn ? to : from;
                        const auto y = renderBlocks (fs, x, block, [&] (size_t s) { return s < sw ? a : b; });
                        const auto ratio = largestStep (y, sw, n) / own;

                        const auto name = std::string (toOn ? "COMPLEX off -> on" : "COMPLEX on -> off") + ", "
                                        + (hz > 0.0 ? fixed (hz, 0) + " Hz" : std::string ("voice")) + ", block "
                                        + std::to_string (block) + ", at " + rateName (fs);

                        check (ratio < 1.5, name + " steps " + fixed (ratio) + "x the signal's own");

                        if (ratio > worst) { worst = ratio; where = name; }
                    }
                }
            }

    std::cout << "COMPLEX switch: largest step " << fixed (worst) << "x the signal's own (" << where << ")\n";
}

/** COMPLEX flicked on and off for 30 s at every rate a user or automation
    might manage, on the voice, then left: nothing blows up or passes the
    ceiling, nothing reaches the limiter louder than the louder of the two
    modes held does, and the module ends up sample for sample where an
    instance that was always in the final mode is.

    "Louder" is judged ahead of the limiter -- the same renders 24 dB down,
    where it is idle -- because after it a ceiling that was clamping in one
    render and not the other moves a 10 ms window by about a dB on its own:
    the limiter remembering a peak for its 60 ms release, not the switch. */
constexpr double kTogglesForgottenWithin = 3.0;

void complexToggled()
{
    const double fs = 48000.0;
    const int block = 512;
    const auto n = (size_t) (35.0 * fs), stop = (size_t) (30.0 * fs) / (size_t) block * (size_t) block;
    const auto x = suiteVoice (fs, n);
    const auto on = complexMode (55.0f, 5.0f, 200.0f, true, kStandardSidechainHz, 200.0f, 6000.0f);
    auto off = on;
    off.complex = false;

    auto onQuiet = on, offQuiet = off;
    onQuiet.outputDb = offQuiet.outputDb = -24.0f;

    const auto heldOn  = renderBlocks (fs, x, block, [&] (size_t) { return on; });
    const auto heldOff = renderBlocks (fs, x, block, [&] (size_t) { return off; });
    const auto heldOnQ  = renderBlocks (fs, x, block, [&] (size_t) { return onQuiet; });
    const auto heldOffQ = renderBlocks (fs, x, block, [&] (size_t) { return offQuiet; });

    struct Pattern { const char* name; std::function<bool (long)> isOn; };
    std::vector<bool> random (n / (size_t) block + 1);
    unsigned state = 2463534242u;
    auto current = false;
    long nextFlip = 0;

    for (long b = 0; b < (long) random.size(); ++b)
    {
        if (b >= nextFlip)
        {
            current = ! current;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            nextFlip = b + 1 + (long) (state % 200u);
        }

        random[(size_t) b] = current;
    }

    const Pattern patterns[] {
        { "every block",       [] (long b) { return (b & 1) == 0; } },
        { "every 3 blocks",    [] (long b) { return ((b / 3) & 1) == 0; } },
        { "every 64 blocks",   [] (long b) { return ((b / 64) & 1) == 0; } },
        { "every 2048 blocks", [] (long b) { return ((b / 2048) & 1) == 0; } },
        { "at random",         [&] (long b) { return (bool) random[(size_t) b]; } },
    };

    for (const auto& pattern : patterns)
    {
        const auto finalOn = pattern.isOn ((long) (stop / (size_t) block) - 1);
        const auto y = renderBlocks (fs, x, block, [&] (size_t s)
        {
            const auto b = (long) (std::min (s, stop - 1) / (size_t) block);
            return pattern.isOn (b) ? on : off;
        });
        const auto yq = renderBlocks (fs, x, block, [&] (size_t s)
        {
            const auto b = (long) (std::min (s, stop - 1) / (size_t) block);
            return pattern.isOn (b) ? onQuiet : offQuiet;
        });

        auto finite = true;
        auto peak = 0.0;
        for (const auto v : y)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, (double) std::abs (v));
        }

        check (finite, std::string ("COMPLEX toggled ") + pattern.name + " stays finite");
        check (peak <= dbToLin ((double) kLimiterCeilingDb) * 1.0001,
               std::string ("COMPLEX toggled ") + pattern.name + " stays under the ceiling");

        const auto w = (size_t) (0.01 * fs);
        double over = -1.0e9;

        for (size_t a = 0; a + w <= stop; a += w)
        {
            double sy = 0.0, s1 = 0.0, s0 = 0.0;
            for (size_t i = a; i < a + w; ++i) { sy += (double) yq[i] * yq[i]; s1 += (double) heldOnQ[i] * heldOnQ[i]; s0 += (double) heldOffQ[i] * heldOffQ[i]; }

            const auto louder = std::max (s1, s0);
            if (10.0 * std::log10 (std::max (louder, 1.0e-300) / (double) w) < -84.0)
                continue;

            over = std::max (over, 10.0 * std::log10 (std::max (sy, 1.0e-300) / louder));
        }

        check (over <= 1.0, std::string ("COMPLEX toggled ") + pattern.name + " comes out " + fixed (over)
                                + " dB over the louder of the two modes held, ahead of the limiter");

        const auto& held = finalOn ? heldOn : heldOff;
        size_t lastDifferent = stop;
        for (auto i = stop; i < n; ++i)
            if (y[i] != held[i])
                lastDifferent = i + 1;

        const auto after = (double) (lastDifferent - stop) / fs;
        check (after <= kTogglesForgottenWithin,
               std::string ("COMPLEX toggled ") + pattern.name + " is sample for sample an instance always in the final mode only "
                   + fixed (after, 3) + " s after the toggling stops");

        std::cout << "COMPLEX toggled " << pattern.name << ": at most " << fixed (over) << " dB over the louder mode held; "
                  << "sample-exact with the final mode held " << fixed (after, 3) << " s after the last toggle\n";
    }
}

/** reset() or prepare() in the middle of a switch's dip, on the way down or
    on the way up, lands exactly where a fresh instance at the new setting
    is, from that sample on. */
void complexSwitchLifecycle()
{
    const auto from = reviewerKnobs (200.0f, 500.0f, false), to = reviewerKnobs (200.0f, 500.0f, true);

    for (const auto toOn : { true, false })
        for (const auto into : { 0.005, 0.020 })
            for (const auto action : { 0, 1, 2 })
            {
                const double fs = 48000.0, after = action == 2 ? 96000.0 : 48000.0;
                const int block = 64;
                const auto n = (size_t) (2.0 * fs);
                const auto x = suiteVoice (fs, n);
                const auto sw = (size_t) (1.0 * fs) / (size_t) block * (size_t) block;
                const auto at = (sw + (size_t) (into * fs)) / (size_t) block * (size_t) block;
                const auto& a = toOn ? from : to;
                const auto& b = toOn ? to : from;

                const auto y = renderBlocks (fs, x, block, [&] (size_t s) { return s < sw ? a : b; },
                                             [&] (DspCore& core, size_t s)
                                             {
                                                 if (s != at) return;
                                                 if (action == 0) core.reset();
                                                 else             core.prepare (after, block, 1);
                                             });

                const std::vector<float> rest (x.begin() + (long) at, x.end());
                const auto fresh = renderBlocks (after, rest, block, [&] (size_t) { return b; });

                size_t differ = 0;
                for (size_t i = 0; i < rest.size(); ++i)
                    differ += y[at + i] != fresh[i] ? 1 : 0;

                check (differ == 0, std::string (action == 0 ? "reset()" : action == 1 ? "prepare (48 kHz)" : "prepare (96 kHz)")
                                        + (into < 0.014 ? " on the way down" : " on the way up") + " of COMPLEX "
                                        + (toOn ? "off -> on" : "on -> off") + ": " + std::to_string (differ)
                                        + " samples differ from a fresh instance at the new setting");
            }
}

//== 11. Knob moves at AMOUNT 55, a side from rest, and settings before audio ===

/** A side of the split coming in from rest by its knob, under the lowest
    tones, where a sine's own sample step is smallest and so a fixed
    start-up transient counts for most. At AMOUNT 0, so the split is all that
    acts. The reviewer measured HIGH THRU 20k -> 2k stepping 1.60x on a 20 Hz
    tone at 48 kHz: the high side's crossover, parked at the top of its range,
    started from rest and faded in at once, and its start-up transient was
    heard under the fade. */
void sideFromRest()
{
    double worst = 0.0;
    std::string where;

    auto at = [] (float lowHz, float highHz)
    {
        return complexMode (0.0f, 5.0f, 200.0f, true, kStandardSidechainHz, lowHz, highHz);
    };

    struct Move { const char* name; Params from, to; };
    const Move moves[] {
        { "HIGH THRU 20k -> 2k",  at (kLowThruOffHz, kHighThruOffHz), at (kLowThruOffHz, 2000.0f) },
        { "HIGH THRU 20k -> 15k", at (kLowThruOffHz, kHighThruOffHz), at (kLowThruOffHz, 15000.0f) },
        { "LOW THRU 20 -> 200",   at (kLowThruOffHz, kHighThruOffHz), at (200.0f, kHighThruOffHz) },
    };

    for (const auto fs : kRates)
        for (const auto& m : moves)
            for (const auto hz : { 20.0, 30.0, 50.0, 100.0 })
            {
                const auto ratio = stepRatio (fs, hz, { { 0.0, m.from }, { 0.4, m.to } }, nullptr, 2.6, 0.4, 1.95);
                const auto name = std::string (m.name) + " from rest, " + fixed (hz, 0) + " Hz, at " + rateName (fs);

                check (ratio < 1.5, name + " steps " + fixed (ratio) + "x the signal's own");

                if (ratio > worst) { worst = ratio; where = name; }
            }

    std::cout << "A side from rest: largest step " << fixed (worst) << "x the signal's own (" << where << ")\n";
}

/** Sections 5 and 6 judge a knob's moves at AMOUNT 0, where the split is the
    only thing acting and the level can be read against the input. With the
    compressor working the two bands take different gains, so there is no
    one right level -- but the move has no business being outside the two it
    goes between. Every 2 ms window of the ensemble level has to sit within
    1 dB of the span of the two held settings, through the whole transition,
    at AMOUNT 55. */
void knobLevelsCompressing()
{
    auto at = [] (float lowHz, float highHz)
    {
        return complexMode (55.0f, 5.0f, 200.0f, true, kStandardSidechainHz, lowHz, highHz);
    };

    constexpr float lo = kLowThruOffHz, hi = kHighThruOffHz;

    struct Move { const char* name; Params from, to; };
    const Move moves[] {
        { "LOW THRU 20 -> 200",     at (lo, hi),    at (200, hi) },
        { "LOW THRU 200 -> 20",     at (200, hi),   at (lo, hi) },
        { "LOW THRU 21 -> 500",     at (21, hi),    at (500, hi) },
        { "LOW THRU 500 -> 21",     at (500, hi),   at (21, hi) },
        { "HIGH THRU 20k -> 6k",    at (lo, hi),    at (lo, 6000) },
        { "HIGH THRU 6k -> 20k",    at (lo, 6000),  at (lo, hi) },
        { "HIGH THRU 2k -> 19999",  at (lo, 2000),  at (lo, 19999) },
        { "HIGH THRU 19999 -> 2k",  at (lo, 19999), at (lo, 2000) },
    };

    double worst = 0.0;
    std::string where;

    for (const auto fs : { 48000.0, 192000.0 })
        for (const auto& m : moves)
            for (const auto hz : { 60.0, 150.0, 400.0, 1000.0, 3000.0, 8000.0, 0.0 })
            {
                const auto n = (size_t) (2.2 * fs);
                const auto sw = (size_t) (0.5 * fs) / 64 * 64;
                const auto members = hz > 0.0 ? 4 : 8;
                std::vector<double> ey (n, 0.0), ea (n, 0.0), eb (n, 0.0);

                for (int k = 0; k < members; ++k)
                {
                    std::vector<float> x (n);
                    unsigned state = 12345u + 7919u * (unsigned) k;

                    for (size_t i = 0; i < n; ++i)
                    {
                        if (hz > 0.0)
                        {
                            x[i] = (float) (0.25 * std::sin (2.0 * kPi * hz * (double) i / fs + k * kPi / 4.0));
                        }
                        else
                        {
                            state = state * 1664525u + 1013904223u;
                            x[i] = (float) (0.25 * ((double) (state >> 8) / 8388608.0 - 1.0));
                        }
                    }

                    const auto y = render (fs, x, 64, [&] (size_t s) { return s < sw ? m.from : m.to; });
                    const auto a = render (fs, x, 64, [&] (size_t) { return m.from; });
                    const auto b = render (fs, x, 64, [&] (size_t) { return m.to; });

                    for (size_t i = 0; i < n; ++i)
                    {
                        ey[i] += (double) y[i] * y[i];
                        ea[i] += (double) a[i] * a[i];
                        eb[i] += (double) b[i] * b[i];
                    }
                }

                const auto w = std::max<size_t> (1, (size_t) (0.002 * fs));
                double outside = 0.0;

                for (auto s = sw; s + w <= n; s += w / 2)
                {
                    double sy = 0.0, sa = 0.0, sb = 0.0;
                    for (size_t i = s; i < s + w; ++i) { sy += ey[i]; sa += ea[i]; sb += eb[i]; }

                    const auto ly = 10.0 * std::log10 (sy), la = 10.0 * std::log10 (sa), lb = 10.0 * std::log10 (sb);
                    outside = std::max ({ outside, std::min (la, lb) - ly, ly - std::max (la, lb) });
                }

                const auto name = std::string (m.name) + " at AMOUNT 55, " + (hz > 0.0 ? fixed (hz, 0) + " Hz" : std::string ("noise"))
                                + ", at " + rateName (fs);

                check (outside <= 1.0, name + ": the level strays " + fixed (outside) + " dB outside the two settings held");

                if (outside > worst) { worst = outside; where = name; }
            }

    std::cout << "Knob moves at AMOUNT 55: the level strays at most " << fixed (worst) << " dB outside the two settings held ("
              << where << ")\n";
}

/** A setting that arrives before any audio after prepare() or reset() --
    a preset loaded and the processor reset, which is how a host loads one and
    how VcompTests checks presets -- lands where a fresh instance at that
    setting is, sample for sample: no smoother moving from the old AMOUNT, no
    side walked in by its knob's edge, no ARC crossover. */
void unheardSettingsLand()
{
    const double fs = 48000.0;
    const auto n = (size_t) (2.0 * fs);
    const auto x = suiteVoice (fs, n);

    auto before = complexMode (80.0f, 5.0f, 200.0f, true, kStandardSidechainHz, kLowThruOffHz, kHighThruOffHz, 3.0f);
    auto after  = complexMode (35.0f, 0.8f, 90.0f, false, 120.0f, 160.0f, 6000.0f, -4.0f);

    struct Case { const char* name; Params from, to; };
    auto offFrom = before;
    offFrom.complex = false;

    const Case cases[] {
        { "every control moved", before, after },
        { "COMPLEX off to on with the split", offFrom, after },
    };

    for (const auto& c : cases)
        for (const auto prepareAgain : { false, true })
        {
            DspCore core;
            core.setParams (c.from);
            core.prepare (fs, 512, 1);

            auto first = std::vector<float> (x.begin(), x.begin() + (long) (n / 2));
            for (size_t at = 0; at < first.size(); at += 512)
            {
                core.setParams (c.from);
                auto* p = first.data() + at;
                core.process (&p, 1, (int) std::min<size_t> (512, first.size() - at));
            }

            if (prepareAgain) core.prepare (fs, 512, 1);
            else              core.reset();

            const std::vector<float> rest (x.begin() + (long) (n / 2), x.end());
            auto y = rest;

            for (size_t at = 0; at < y.size(); at += 512)
            {
                core.setParams (c.to);
                auto* p = y.data() + at;
                core.process (&p, 1, (int) std::min<size_t> (512, y.size() - at));
            }

            const auto fresh = renderBlocks (fs, rest, 512, [&] (size_t) { return c.to; });

            size_t differ = 0;
            for (size_t i = 0; i < y.size(); ++i)
                differ += y[i] != fresh[i] ? 1 : 0;

            check (differ == 0, std::string (c.name) + ", set after " + (prepareAgain ? "prepare()" : "reset()")
                                    + " and before any audio: " + std::to_string (differ)
                                    + " samples differ from a fresh instance at that setting");
        }
}

//== 12. A preset recalled from any other ======================================
//
// A preset recall or a host snapshot changes COMPLEX and the knobs in the same
// block. The round-3 dip was decided on the new knobs alone, so COMPLEX on ->
// off with LOW and HIGH THRU going to their rails in the same block skipped it
// and the running sides left by the knobs' slow way: the voice +1.2 dB over an
// always-off instance for half a second, and Keep The Chest -> In Front with
// the limiter 5.76 dB over a fresh In Front (reviewer, 2026-10-03).
//
// Every ordered pair of the factory presets, 56 of them, recalled at 1 s into
// the suite's voice at -18 dBFS RMS, at 48 kHz in 512-sample blocks: against a
// fresh instance of the target preset, outside the dip --
//
//   - the level of every 10 ms window within 1 dB, and the limiter within
//     0.5 dB, beyond what the same recall with the split held out of both
//     presets (LOW and HIGH THRU at their rails: AMOUNT, MAKEUP and the
//     detector's settings change, nothing else) does;
//   - the largest sample step under 1.5x the larger of the two presets' own;
//   - and sample for sample the fresh instance within kRecallForgottenWithin.

Params presetParams (const bmo::FactoryPreset& preset)
{
    const auto& list = specs();
    std::vector<float> v;

    for (const auto& s : list)
        v.push_back (s.def);

    for (const auto& s : preset.settings)
        v[(size_t) bmo::indexOfParam (list, s.id)] = s.value;

    Params p;
    p.amountPercent = v[amount];
    p.gateDb        = v[gate];
    p.outputDb      = v[output];
    p.complex       = v[complex] > 0.5f;
    p.attackMs      = v[attack];
    p.releaseMs     = v[release];
    p.arc           = v[arc] > 0.5f;
    p.sidechainHz   = v[sidechain];
    p.lowThruHz     = v[lowThru];
    p.highThruHz    = v[highThru];
    return p;
}

/** What bounds this: ARC's slow branch. Two instances that heard different
    settings hold different slow-branch states, and the difference decays
    only at that branch's own rate -- its charge (1.2 x RELEASE) while a
    phrase is loud and its release (10 x RELEASE) in the gaps, 4 s at Smooth
    Lead's 400 ms -- until it is under a float's last bit and the two round
    alike. Nothing short of clearing the branch, which would be wrong for the
    reason section 1 gives, can hurry it. The reviewer saw 22.4 s on the base
    for one pair; here the worst is 45.6 s (In Front -> Smooth Lead), a
    recall with no split on either side and no dip: ARC, not the switch. */
constexpr double kRecallForgottenWithin = 50.0;

void presetRecalls()
{
    const double fs = 48000.0;
    const int block = 512;
    const auto shortN = (size_t) (4.0 * fs), longN = (size_t) (60.0 * fs);
    const auto sw = (size_t) (1.0 * fs) / (size_t) block * (size_t) block;
    const auto half = (size_t) std::lround (0.014 * fs);    // DspCore::kComplexDipMs
    const auto x = suiteVoice (fs, longN);
    const std::vector<float> xs (x.begin(), x.begin() + (long) shortN);

    const auto& presets = factory();

    double worstLevel = -1.0e9, worstLimit = -1.0e9, worstAbsLimit = -1.0e9, worstStep = 0.0, worstTime = 0.0;
    std::string levelWhere, limitWhere, stepWhere, timeWhere;

    auto railed = [] (Params p) { p.lowThruHz = kLowThruOffHz; p.highThruHz = kHighThruOffHz; return p; };
    auto quiet  = [] (Params p) { p.outputDb -= 24.0f; return p; };

    for (size_t ia = 0; ia < presets.size(); ++ia)
        for (size_t ib = 0; ib < presets.size(); ++ib)
        {
            if (ia == ib)
                continue;

            const auto a = presetParams (presets[ia]), b = presetParams (presets[ib]);
            const auto name = std::string (presets[ia].name) + " -> " + presets[ib].name;

            const auto sideA = a.complex && (a.lowThruHz > kLowThruOffHz || a.highThruHz < kHighThruOffHz);
            const auto sideB = b.complex && (b.lowThruHz > kLowThruOffHz || b.highThruHz < kHighThruOffHz);
            const auto lowA  = a.complex && a.lowThruHz > kLowThruOffHz,   lowB  = b.complex && b.lowThruHz > kLowThruOffHz;
            const auto highA = a.complex && a.highThruHz < kHighThruOffHz, highB = b.complex && b.highThruHz < kHighThruOffHz;
            const auto dipped = (a.complex != b.complex && (sideA || sideB)) || lowA != lowB || highA != highB;
            const auto from = dipped ? sw + 2 * half + 1 : sw;

            auto recall = [&] (const Params& p, const Params& q) { return renderBlocks (fs, xs, block, [&] (size_t s) { return s < sw ? p : q; }); };
            auto held   = [&] (const Params& p) { return renderBlocks (fs, xs, block, [&] (size_t) { return p; }); };

            const auto y  = recall (a, b),                         yq  = recall (quiet (a), quiet (b));
            const auto f  = held (b),                              fq  = held (quiet (b));
            const auto y2 = recall (railed (a), railed (b)),       y2q = recall (quiet (railed (a)), quiet (railed (b)));
            const auto f2 = held (railed (b)),                     f2q = held (quiet (railed (b)));
            const auto fa = held (a);

            const auto ly = limiterReduction (y, yq), lf = limiterReduction (f, fq);
            const auto l2 = limiterReduction (y2, y2q), lf2 = limiterReduction (f2, f2q);

            const auto w = (size_t) (0.01 * fs);
            double level = -1.0e9, limit = -1.0e9, absLimit = -1.0e9;

            for (auto s = sw; s + w <= shortN; s += w)
            {
                if (s < from)
                    continue;

                double ey = 0.0, ef = 0.0, e2 = 0.0, ef2 = 0.0;
                float my = 0.0f, mf = 0.0f, m2 = 0.0f, mf2 = 0.0f;

                for (size_t i = s; i < s + w; ++i)
                {
                    ey += (double) y[i] * y[i];   ef += (double) f[i] * f[i];
                    e2 += (double) y2[i] * y2[i]; ef2 += (double) f2[i] * f2[i];
                    my = std::max (my, ly[i]);    mf = std::max (mf, lf[i]);
                    m2 = std::max (m2, l2[i]);    mf2 = std::max (mf2, lf2[i]);
                }

                const auto excess  = (double) (my - mf);
                const auto excess2 = std::max (0.0, (double) (m2 - mf2));
                limit    = std::max (limit, excess - excess2);
                absLimit = std::max (absLimit, excess);

                if (10.0 * std::log10 (std::max (ef, 1.0e-300) / (double) w) < -60.0)
                    continue;

                const auto dy = 10.0 * std::log10 (std::max (ey, 1.0e-300) / ef);
                const auto d2 = 10.0 * std::log10 (std::max (e2, 1.0e-300) / std::max (ef2, 1.0e-300));
                level = std::max (level, std::abs (dy) - std::abs (d2));
            }

            const auto own  = std::max (largestStep (f, 1, shortN), largestStep (fa, 1, shortN));
            const auto step = largestStep (y, sw, shortN) / own;

            check (level <= 1.0, name + ": " + fixed (level) + " dB further from a fresh " + presets[ib].name
                                     + " than the recall with the split held out is, outside the dip");
            check (limit <= 0.5, name + ": the limiter " + fixed (limit) + " dB over a fresh " + presets[ib].name
                                     + " beyond the recall with the split held out");
            check (step < 1.5, name + " steps " + fixed (step) + "x the presets' own");

            // Sample for sample, over a long render.
            const auto yl = renderBlocks (fs, x, block, [&] (size_t s) { return s < sw ? a : b; });
            const auto fl = renderBlocks (fs, x, block, [&] (size_t) { return b; });

            size_t lastDifferent = sw;
            for (auto i = sw; i < longN; ++i)
                if (yl[i] != fl[i])
                    lastDifferent = i + 1;

            const auto after = (double) (lastDifferent - sw) / fs;
            check (after <= kRecallForgottenWithin && lastDifferent + (size_t) (2.0 * fs) < longN,
                   name + ": sample for sample a fresh " + presets[ib].name + " only " + fixed (after, 2) + " s after the recall");

            if (level > worstLevel)       { worstLevel = level;       levelWhere = name; }
            if (limit > worstLimit)       { worstLimit = limit;       limitWhere = name; }
            if (absLimit > worstAbsLimit) { worstAbsLimit = absLimit; }
            if (step > worstStep)         { worstStep = step;         stepWhere = name; }
            if (after > worstTime)        { worstTime = after;        timeWhere = name; }
        }

    std::cout << "Preset recalls, 56 pairs: level " << fixed (worstLevel) << " dB beyond the split-held-out recall (" << levelWhere
              << "); limiter " << fixed (worstLimit) << " dB beyond it (" << limitWhere << "), " << fixed (worstAbsLimit)
              << " dB over a fresh target at most; step " << fixed (worstStep) << "x (" << stepWhere << "); sample-exact after "
              << fixed (worstTime, 2) << " s at worst (" << timeWhere << ")\n";
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) == "--print-hashes")
    {
        heldIsUnchanged (true);
        return 0;
    }

    // Mono at every rate the suite runs at; then the step and silence
    // sections again in stereo, each channel judged, where every channel's
    // split has to move in step with the first's.
    sections();

    gChannels = 2;
    gHeavy = false;
    kRates = { 48000.0, 192000.0 };

    for (const auto right : { false, true })
    {
        gTakeRight = right;
        gMode = right ? " (stereo, right)" : " (stereo, left)";
        sections();
    }

    gChannels = 1;
    gTakeRight = false;
    gMode.clear();
    kRates = { 44100.0, 48000.0, 96000.0, 192000.0 };

    glideFlatness();
    complexReturn();
    gateReturn();
    complexSwitchLevels();
    complexSwitchSteps();
    complexToggled();
    complexSwitchLifecycle();
    sideFromRest();
    knobLevelsCompressing();
    unheardSettingsLand();
    presetRecalls();
    heldIsUnchanged (false);

    if (failures == 0)
        std::cout << "All LTV Comp switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
