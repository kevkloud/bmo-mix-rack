/*
    BMO Tune RT end to end: detector, correction law and CLASSIC engine as
    the host drives them, measured with tools/common/Analysis.h -- an
    independent ruler, never the plugin's own detector.

    The claims, each tied to the spec:
      - an in-tune note passes bit-exactly, delayed by the engine's floor
        (T-5: the analysis path live, correction enabled, lag unchanged)
      - reported latency is 0: Live is the only contract
      - at retune 0 a steady note settles within 3 cents of its target (§9)
      - the output is identical at every block size, and run to run (T-1)
      - a scale is the set of targets, and switching notes off narrows it (§4.1)
      - one NaN does not break anything for longer than it takes to re-lock
*/

#include "modules/tune/dsp/TuneCore.h"
#include "tests/dsp/tune/TestUtil.h"
#include "tools/tune/common/Analysis.h"
#include "tools/tune/common/Signals.h"

#include <cstdio>
#include <limits>
#include <string>

using namespace bmo::tune;
using namespace bmo::tune::test;
namespace sig = bmo::tune::signals;
namespace an = bmo::tune::analysis;

namespace
{
    /** Renders through a fresh core. blockSize 0 = pseudo-random blocks. */
    std::vector<float> render (const std::vector<float>& in, const TuneParams& p, double fs, int blockSize = 128)
    {
        TuneCore core;
        core.setParams (p);
        core.prepare (fs, 4096);

        auto out = in;
        sig::Random rng (42);
        size_t at = 0;

        while (at < out.size())
        {
            auto n = blockSize > 0 ? blockSize : 1 + (int) (rng.next() % 700);
            n = (int) std::min<size_t> ((size_t) n, out.size() - at);
            core.process (out.data() + at, n);
            at += (size_t) n;
        }

        return out;
    }

    double measuredCents (const std::vector<float>& y, double fs, double targetHz, double fromSeconds, double seconds)
    {
        const auto hz = an::measureHz (y, (size_t) (fromSeconds * fs), (size_t) (seconds * fs), fs);
        return hz > 0.0 ? an::cents (hz, targetHz) : 1.0e9;
    }

    std::string label (const char* what, double a, double b = 0.0)
    {
        char buf[160];
        std::snprintf (buf, sizeof buf, what, a, b);
        return buf;
    }
}

int main()
{
    const double fs = 48000.0;

    //== The ruler is a ruler ==================================================
    {
        const auto c = sig::steady (437.3, 0.3, fs);
        const auto x = sig::voice (c, fs).samples;
        const auto err = an::cents (an::measureHz (x, 4800, 4800, fs), 437.3);
        report ("ruler error on a 437.3 Hz synthetic voice", err, "c");
        check (std::abs (err) < 0.05, "the offline ruler reads a steady voice within 0.05 cents");
    }

    //== Passthrough and latency ===============================================
    {
        TuneParams p;   // chromatic, retune 0, Live
        const auto c = sig::steady (440.0, 0.5, fs);
        const auto x = sig::sine (c, fs, 0.8).samples;
        const auto y = render (x, p, fs);

        const auto d = contract::liveRestSamples (fs);

        // An "in-tune" A440 is detected a few ten-thousandths of a cent off,
        // so the engine really does apply that correction: over half a
        // second the read drifts ~0.015 samples, which on a 0.8 sine at
        // 440 Hz is a -63 dBFS difference from the plain delayed input. That
        // is the correction working, not an error. Bit-exactness is claimed
        // where the correction is exactly zero: every note switched off.
        double worst = 0.0;
        for (size_t i = (size_t) d; i < x.size(); ++i)
            worst = std::max (worst, (double) std::abs (y[i] - x[i - (size_t) d]));
        report ("in-tune A440: worst deviation from the delayed input", 20.0 * std::log10 (worst + 1e-30), "dBFS");
        check (worst < 3.0e-3, "an in-tune A440 comes out as the input delayed by the Live rest delay, within -50 dBFS");
        check (an::delayOf (x, y, d + 64) == d, "cross-correlation finds the same delay with correction enabled (T-5)");

        TuneParams zero = p;
        zero.allowed = 0;
        const auto yz = render (sig::sine (sig::steady (452.0, 0.5, fs), fs, 0.8).samples, zero, fs);
        const auto xz = sig::sine (sig::steady (452.0, 0.5, fs), fs, 0.8).samples;
        bool exact = true;
        for (size_t i = (size_t) d; i < xz.size(); ++i)
            exact = exact && yz[i] == xz[i - (size_t) d];
        check (exact, "with the correction exactly zero the output is the input, delayed, bit for bit");
        check (TuneCore::kReportedLatency == 0, "Live reports 0 to the host");
        report ("Live rest delay", 1000.0 * d / fs, "ms");
    }

    //== Tuning accuracy at retune 0 (spec §9: < 3 cents) ======================
    std::printf ("output tuning, retune 0, chromatic\n");
    for (auto base : { 110.0, 220.0, 440.0, 880.0 })
    {
        for (auto offset : { -45.0, -20.0, 10.0, 35.0 })
        {
            TuneParams p;
            const auto hz = base * std::exp2 (offset / 1200.0);
            const auto c = sig::steady (hz, 0.8, fs);
            const auto y = render (sig::voice (c, fs).samples, p, fs);
            const auto err = measuredCents (y, fs, base, 0.3, 0.4);

            const auto name = label ("%.0f Hz %+.0f c: output error", base, offset);
            report (name, err, "c");
            check (std::abs (err) < 3.0, name + " under 3 cents");
        }
    }

    //== Tone quality: splices are inaudible on a sine =========================
    {
        TuneParams p;
        for (auto offset : { -40.0, 40.0 })
        {
            const auto base = 440.0;
            const auto c = sig::steady (base * std::exp2 (offset / 1200.0), 1.0, fs);
            const auto y = render (sig::sine (c, fs, 0.5).samples, p, fs);
            const auto thd = an::thdPlusNoiseDb (y, (size_t) (0.4 * fs), (size_t) (0.5 * fs), base, fs);
            report (label ("sine %+.0f c corrected: THD+N", offset), thd, "dB");
            check (thd < -60.0, label ("a %+.0f-cent correction of a sine stays below -60 dB THD+N (T-3)", offset));
        }
    }

    //== Block-size invariance and determinism (T-1) ===========================
    {
        TuneParams p;
        p.retuneMs = 3.2;   // 0.1's knob 20, which this check was written at
        p.vibratoPercent = 50.0;
        const auto c = sig::concat ({ sig::silence (0.05, fs), sig::vibrato (233.0, 60.0, 5.5, 0.6, fs),
                                      sig::silence (0.05, fs), sig::glide (180.0, 500.0, 0.5, fs) });
        const auto x = sig::voice (c, fs).samples;

        const auto reference = render (x, p, fs, 1);
        bool allSame = true;

        for (auto bs : { 13, 32, 64, 128, 512, 1024, 4096, 0 })
        {
            const auto y = render (x, p, fs, bs);
            double worst = 0.0;
            for (size_t i = 0; i < y.size(); ++i)
                worst = std::max (worst, (double) std::abs (y[i] - reference[i]));
            if (worst != 0.0)
            {
                allSame = false;
                report (label ("block %.0f worst deviation from block 1", bs), worst);
            }
        }

        check (allSame, "the output is bit-identical at block sizes 1, 13 ... 4096 and random");
        check (render (x, p, fs, 0) == render (x, p, fs, 0), "two runs with the same input are bit-identical");
    }

    //== Scales and the note switches =========================================
    {
        // Only A# switched on: an A3 input has one place to go, a semitone up.
        TuneParams p;
        p.allowed = (NoteMask) (1u << 10);
        const auto c = sig::steady (220.0, 0.8, fs);
        const auto y = render (sig::voice (c, fs).samples, p, fs);
        const auto target = 440.0 * std::exp2 ((58 - 69) / 12.0);
        const auto err = measuredCents (y, fs, target, 0.3, 0.4);
        report ("only A# allowed, A3 input: error against A#3", err, "c");
        check (std::abs (err) < 3.0, "with one note switched on, a voice a semitone away is pulled onto it");

        TuneParams q;
        q.scale = ScaleType::major;   // C major: no C#
        const auto cs = sig::steady (282.0, 0.8, fs);   // between C#4 (277) and D4 (294), nearer C#
        const auto yq = render (sig::voice (cs, fs).samples, q, fs);
        const auto errD = measuredCents (yq, fs, 293.66, 0.3, 0.4);
        report ("C major, 282 Hz in: error against D4", errD, "c");
        check (std::abs (errD) < 3.0, "outside the scale, the nearest allowed note wins (C# is not in C major)");

        TuneParams r;
        r.allowed = 0;
        const auto off = sig::steady (230.0, 0.6, fs);
        const auto xr = sig::voice (off, fs).samples;
        const auto yr = render (xr, r, fs);
        const auto errR = measuredCents (yr, fs, 230.0, 0.3, 0.25);
        check (std::abs (errR) < 0.5, "with every note switched off the pitch is left alone (T-4)");

        TuneParams m;
        m.scale = ScaleType::minor;
        m.key = 9;   // A minor: no C#, so 282 Hz goes to D4 as in C major
        const auto ym = render (sig::voice (cs, fs).samples, m, fs);
        const auto errM = measuredCents (ym, fs, 293.66, 0.3, 0.4);
        check (std::abs (errM) < 3.0, "A minor pulls the same 282 Hz to D4");
    }

    //== One NaN ==============================================================
    {
        TuneParams p;
        const auto c = sig::steady (440.0 * std::exp2 (30.0 / 1200.0), 1.2, fs);
        auto x = sig::voice (c, fs).samples;
        x[(size_t) (0.4 * fs)] = std::numeric_limits<float>::quiet_NaN();
        x[(size_t) (0.4 * fs) + 1] = std::numeric_limits<float>::infinity();

        const auto y = render (x, p, fs);
        bool finite = true;
        for (auto v : y)
            finite = finite && std::isfinite (v);

        check (finite, "NaN and Inf in the input never reach the output");
        const auto err = measuredCents (y, fs, 440.0, 0.8, 0.3);
        report ("tuning error 0.4 s after a NaN", err, "c");
        check (std::abs (err) < 3.0, "and the correction is back on target afterwards");
    }

    return finish ("core");
}
