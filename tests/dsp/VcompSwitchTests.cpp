// LTV Comp: what its controls do to the audio at the moment they move, and
// what a stage that was out of use brings back with it when it returns.
//
// VcompDspTests holds what the module does once it has settled; this file
// holds the transitions -- a control changing while a signal passes, or after
// one has stopped -- which none of those tests can see, because each of them
// builds a fresh core, sets it once and measures the result.

#include "modules/vcomp/dsp/DspCore.h"

#include <algorithm>
#include <cmath>
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
    constexpr double kRates[] { 44100.0, 48000.0, 96000.0 };

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
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
        core.prepare (fs, block, 1);

        for (size_t start = 0; start < signal.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, signal.size() - start);
            core.setParams (paramsAt (start));
            auto* p = signal.data() + start;
            core.process (&p, 1, n);
        }

        return signal;
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
                      const Params* widest = nullptr, double seconds = 1.2, double firstMove = 0.4)
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
            const auto during = largestStep (y, sw, last + (size_t) (0.25 * fs));

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
}

int main()
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
                    const auto ratio = stepRatio (fs, hz, m.moves, nullptr, 1.6);
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

    if (failures == 0)
        std::cout << "All LTV Comp switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
