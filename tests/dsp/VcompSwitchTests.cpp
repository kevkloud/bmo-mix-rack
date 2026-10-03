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

    if (failures == 0)
        std::cout << "All LTV Comp switch tests passed.\n";

    return failures == 0 ? 0 : 1;
}
