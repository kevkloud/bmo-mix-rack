// Offline measurement harness.
//
// Links the DSP core directly -- no plugin host, no GUI, no JUCE -- so a sweep
// runs in milliseconds and is trivial to debug. This is the ground truth the
// EQ is developed against; see docs/plan.md, Part 7.
//
//   measure thd    [--freq f] [--level dBFS] [--in dB] [--os n]
//   measure profile  -- distortion against frequency and level
//   measure alias    -- folded-image rejection
//   measure curve  [--lf i:dB] [--mid i:dB] [--hf i:dB]
//                  [--hpf i] [--lpf i] [--hiq]
//   measure bands  -- band-interaction table
//   measure q      -- realised bell width vs gain

#include "modules/eq/dsp/EqNetwork.h"
#include "modules/eq/dsp/DspCore.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <vector>

using namespace bmo::eq;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kPi = 3.14159265358979323846;   // kPi is not portable to MSVC

    EqNetwork make (const EqSettings& s)
    {
        EqNetwork n;
        n.prepare (kSampleRate);
        n.setSettings (s);
        return n;
    }

    /** Parse "index:gainDb", e.g. "2:-6.5". */
    bool parseBand (const char* arg, int& index, float& gainDb)
    {
        const auto* colon = std::strchr (arg, ':');

        if (colon == nullptr)
            return false;

        index  = std::atoi (std::string (arg, colon).c_str());
        gainDb = (float) std::atof (colon + 1);
        return true;
    }

    void printCurve (const EqNetwork& net)
    {
        std::printf ("%9s  %9s  %9s\n", "Hz", "dB", "phase deg");

        for (int i = 0; i < 61; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, (double) i / 60.0);
            const auto h  = net.responseAt (hz);

            std::printf ("%9.1f  %+9.2f  %+9.1f\n",
                         hz, net.magnitudeDbAt (hz), std::arg (h) * 180.0 / kPi);
        }

        std::printf ("\nbroadband gain: %+.2f dB\n", 20.0 * std::log10 (net.broadbandGain()));
    }

    void printInteraction()
    {
        std::printf ("Band interaction: LF 220 Hz +12 dB against mid 360 Hz +12 dB.\n"
                     "A cascade of independent biquads would sum the two dB columns.\n\n");

        EqSettings a; a.lfFreqHz  = 220.0f; a.lfGainDb  = 12.0f;
        EqSettings b; b.midFreqHz = 360.0f; b.midGainDb = 12.0f;
        EqSettings c = a; c.midFreqHz = 360.0f; c.midGainDb = 12.0f;

        auto A = make (a), B = make (b), C = make (c);

        std::printf ("%7s %8s %8s %9s %10s %8s\n", "Hz", "LF", "mid", "naive sum", "combined", "delta");

        for (double hz : { 50.0, 110.0, 220.0, 360.0, 700.0, 1600.0, 5000.0 })
        {
            const auto da = A.magnitudeDbAt (hz), db = B.magnitudeDbAt (hz), dc = C.magnitudeDbAt (hz);
            std::printf ("%7.0f %+8.2f %+8.2f %+9.2f %+10.2f %+8.2f\n", hz, da, db, da + db, dc, dc - (da + db));
        }
    }

    //==========================================================================
    // Distortion analysis.
    //
    // The fundamental is placed exactly on a DFT bin so a rectangular window
    // leaks nothing, and the harmonics then land exactly on bins too. No
    // windowing, no correction factors, no ambiguity about what is signal and
    // what is skirt.
    //==========================================================================

    constexpr int kAnalysisSize = 1 << 15;

    struct Harmonics
    {
        double fundamentalHz = 0.0;
        std::array<double, 9> level {};   // linear magnitude, index 1..8
        double thdPercent = 0.0;
    };

    std::vector<float> render (double binFrequency, double levelDb,
                               float inputGainDb, int oversampling, int samples)
    {
        DspCore core;
        core.prepare (kSampleRate, 512, 1, oversampling);

        DspCore::Params p;
        p.inputGainDb  = inputGainDb;
        p.oversampling = oversampling;
        core.setParams (p);

        const auto amplitude = std::pow (10.0, levelDb / 20.0);

        std::vector<float> buffer ((size_t) samples);
        float* channels[1] { buffer.data() };

        // Two passes: the first lets the filters and the dry delay settle, the
        // second is what gets analysed.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < samples; ++i)
                buffer[(size_t) i] = (float) (amplitude
                    * std::sin (2.0 * kPi * binFrequency * (double) i / kSampleRate));

            core.process (channels, 1, samples);
        }

        return buffer;
    }

    /** Nearest frequency that is an exact whole number of cycles in the window. */
    double snapToBin (double hz)
    {
        const auto bin = std::max (1.0, std::round (hz * (double) kAnalysisSize / kSampleRate));
        return bin * kSampleRate / (double) kAnalysisSize;
    }

    double magnitudeAt (const std::vector<float>& x, double hz)
    {
        const auto w = -2.0 * kPi * hz / kSampleRate;
        const std::complex<double> step { std::cos (w), std::sin (w) };
        std::complex<double> rot { 1.0, 0.0 }, acc { 0.0, 0.0 };

        for (float v : x) { acc += (double) v * rot; rot *= step; }

        return 2.0 * std::abs (acc) / (double) x.size();
    }

    Harmonics analyse (double requestedHz, double levelDb, float inputGainDb, int oversampling)
    {
        Harmonics h;
        h.fundamentalHz = snapToBin (requestedHz);

        const auto x = render (h.fundamentalHz, levelDb, inputGainDb, oversampling, kAnalysisSize);

        double harmonicPower = 0.0;

        for (int n = 1; n <= 8; ++n)
        {
            const auto f = h.fundamentalHz * n;

            if (f >= kSampleRate * 0.5)
                break;

            h.level[(size_t) n] = magnitudeAt (x, f);

            if (n > 1)
                harmonicPower += h.level[(size_t) n] * h.level[(size_t) n];
        }

        h.thdPercent = h.level[1] > 0.0 ? 100.0 * std::sqrt (harmonicPower) / h.level[1] : 0.0;
        return h;
    }

    void printThd (double hz, double levelDb, float inputGainDb, int oversampling)
    {
        const auto h = analyse (hz, levelDb, inputGainDb, oversampling);

        std::printf ("%.1f Hz at %.1f dBFS, input %+.1f dB, %dx oversampling\n\n",
                     h.fundamentalHz, levelDb, inputGainDb, oversampling);
        std::printf ("  THD  %.4f %%\n\n%10s %12s\n", h.thdPercent, "harmonic", "dB rel H1");

        for (int n = 2; n <= 8; ++n)
            if (h.level[(size_t) n] > 0.0)
                std::printf ("%10d %12.1f\n", n,
                             20.0 * std::log10 (h.level[(size_t) n] / h.level[1]));
    }

    void printProfile (int oversampling)
    {
        std::printf ("Distortion against frequency and level, input gain at unity.\n"
                     "A transformer's core flux is the integral of applied voltage, so at\n"
                     "a fixed level the bottom of the band works it far harder than the\n"
                     "top. Vendor 15 measured 0.1 %% at 40 Hz against 0.01 %% at 1 kHz and\n"
                     "10 kHz for the line transformer in these units.\n\n");

        const double levels[] { -24.0, -12.0, -6.0, 0.0 };
        const double freqs[]  { 40.0, 100.0, 400.0, 1000.0, 5000.0, 10000.0 };

        std::printf ("%8s", "Hz");
        for (double l : levels) std::printf ("%12.0f dBFS", l);
        std::printf ("\n");

        for (double f : freqs)
        {
            std::printf ("%8.0f", f);

            for (double l : levels)
                std::printf ("%14.4f%%", analyse (f, l, 0.0f, oversampling).thdPercent);

            std::printf ("\n");
        }

        std::printf ("\nSecond against third harmonic, 0 dBFS:\n%8s %10s %10s %10s\n",
                     "Hz", "H2 dB", "H3 dB", "H2-H3");

        for (double f : freqs)
        {
            const auto h = analyse (f, 0.0, 0.0f, oversampling);

            if (h.level[2] <= 0.0 || h.level[3] <= 0.0)
                continue;

            const auto h2 = 20.0 * std::log10 (h.level[2] / h.level[1]);
            const auto h3 = 20.0 * std::log10 (h.level[3] / h.level[1]);
            std::printf ("%8.0f %10.1f %10.1f %10.1f\n", f, h2, h3, h2 - h3);
        }
    }

    void printAliasing()
    {
        std::printf ("Folded-image rejection. A tone near the top of the band is driven\n"
                     "hard; its harmonics land above Nyquist and fold back as inharmonic\n"
                     "content. Reported is the worst non-harmonic peak below the\n"
                     "fundamental.\n\n%6s %14s %14s\n", "factor", "worst alias", "THD");

        for (int os : { 1, 2, 4, 8 })
        {
            const auto f0 = snapToBin (7500.0);
            const auto x  = render (f0, -3.0, 18.0f, os, kAnalysisSize);

            const auto fundamental = magnitudeAt (x, f0);
            double worst = 0.0;

            // Scan bins, skipping those at or adjacent to a harmonic of f0.
            for (int bin = 4; bin < kAnalysisSize / 2; ++bin)
            {
                const auto f = bin * kSampleRate / (double) kAnalysisSize;
                bool harmonic = false;

                for (int n = 1; n <= 12; ++n)
                    if (std::abs (f - n * f0) < 3.0 * kSampleRate / (double) kAnalysisSize)
                        harmonic = true;

                if (! harmonic)
                    worst = std::max (worst, magnitudeAt (x, f));
            }

            std::printf ("%5dx %13.1f dB %13.4f%%\n", os,
                         20.0 * std::log10 (worst / fundamental),
                         analyse (7500.0, -3.0, 18.0f, os).thdPercent);
        }
    }

    /** Realised centre and width for each mid detent -- what the band actually
        does, as opposed to what its branch Q was set to. */
    void printBell()
    {
        std::printf ("Mid band, as realised. Centre is where the response actually peaks\n"
                     "(band interaction can pull it off the nominal frequency); width is\n"
                     "measured 3 dB below that peak.\n\n"
                     "%9s %10s %8s %9s %9s %9s\n",
                     "nominal", "centre", "err %", "+6 oct", "+12 oct", "+18 oct");

        for (int position = 0; position < 6; ++position)
        {
            const auto nominal = kMidFreqs[(size_t) position];
            std::printf ("%9.0f", nominal);

            // (centre reported from the +6 dB pass)

            for (double gain : { 6.0, 12.0, 18.0 })
            {
                EqSettings s;
                s.midFreqHz = nominal;
                s.midGainDb = (float) gain;
                auto net = make (s);

                // Locate the actual peak.
                double best = -1.0e9, bestHz = nominal;
                for (int i = 0; i < 3000; ++i)
                {
                    const auto hz = nominal * std::pow (2.0, -2.0 + 4.0 * i / 2999.0);
                    const auto db = net.magnitudeDbAt (hz);
                    if (db > best) { best = db; bestHz = hz; }
                }

                const auto threshold = best - 3.0;
                const auto edge = [&] (double dir)
                {
                    double hz = bestHz;
                    for (int i = 0; i < 8000; ++i)
                    {
                        hz *= std::pow (2.0, dir * 0.001);
                        if (hz < 10.0 || hz > 23000.0 || net.magnitudeDbAt (hz) < threshold) break;
                    }
                    return hz;
                };

                if (gain == 6.0)
                    std::printf ("%10.0f %7.1f%%", bestHz, 100.0 * (bestHz - nominal) / nominal);

                std::printf ("%9.2f", std::log2 (edge (1.0) / edge (-1.0)));
            }

            std::printf ("\n");
        }
    }

    /** Solve for the branch Q that reproduces a measured bell width.

        The realised width is not the branch Q: the shared feedback path and
        the gain setting both act on it. So rather than converting by hand,
        bisect on the branch value until the model's own realised width matches
        the target. Targets are taken from measurements of a built unit. */
    void printFitQ()
    {
        // Realised -3 dB width in octaves at +18 dB, traced from the response
        // plots published with the Nyan-1073-EQ hardware project (CC BY-SA
        // 4.0), which measured an assembled board.
        const double target[6] { 1.13, 1.00, 1.06, 1.15, 0.74, 0.52 };

        std::printf ("Branch Q solved against measured bell widths (+18 dB, -3 dB points).\n\n"
                     "%8s %10s %10s %10s\n", "Hz", "target oct", "solved Q", "got oct");

        for (int position = 0; position < 6; ++position)
        {
            const auto hz = (double) kMidFreqs[(size_t) position];

            const auto widthFor = [&] (double q)
            {
                EqSettings s;
                s.midFreqHz = (float) hz;
                s.midGainDb = 18.0f;
                s.midQ      = (float) q;
                auto net = make (s);

                double best = -1.0e9, peak = hz;
                for (int i = 0; i < 4000; ++i)
                {
                    const auto f = hz * std::pow (2.0, -2.0 + 4.0 * i / 3999.0);
                    const auto db = net.magnitudeDbAt (f);
                    if (db > best) { best = db; peak = f; }
                }

                const auto thr = best - 3.0;
                const auto edge = [&] (double dir)
                {
                    double f = peak;
                    for (int i = 0; i < 9000; ++i)
                    {
                        f *= std::pow (2.0, dir * 0.0005);
                        if (f < 10.0 || f > 23000.0 || net.magnitudeDbAt (f) < thr) break;
                    }
                    return f;
                };

                return std::log2 (edge (1.0) / edge (-1.0));
            };

            // Width falls as Q rises, so bisect on that.
            double lo = 0.3, hi = 8.0;
            for (int i = 0; i < 40; ++i)
            {
                const auto mid = 0.5 * (lo + hi);
                if (widthFor (mid) > target[position]) lo = mid; else hi = mid;
            }

            const auto q = 0.5 * (lo + hi);
            std::printf ("%8.0f %10.2f %10.2f %10.2f\n", hz, target[position], q, widthFor (q));
        }
    }

    /** Shelf shape, reported the same way the measured plots were read, so the
        two are directly comparable. */
    void printShelf()
    {
        std::printf ("Shelf shape at +20 dB, matching the level the reference plots were taken at. Measured figures traced from response plots of\n"
                     "an assembled board (Nyan-1073-EQ, CC BY-SA 4.0): slope near 7 dB/oct,\n"
                     "and a dip past the corner of roughly 6 %% of the boost.\n\n"
                     "%14s %8s %9s %11s %9s %10s\n",
                     "shelf", "boost", "corner", "slope dB/oct", "dip dB", "dip/boost");

        struct Case { const char* name; bool high; float hz; };
        const Case cases[] {
            { "low 35",   false, 35.0f  }, { "low 60",  false, 60.0f  },
            { "low 110",  false, 110.0f }, { "low 220", false, 220.0f },
            { "high 12k", true,  12000.0f },
        };

        for (const auto& c : cases)
        {
            EqSettings s;
            if (c.high) { s.hfFreqHz = c.hz; s.hfGainDb = 20.0f; }
            else        { s.lfFreqHz = c.hz; s.lfGainDb = 20.0f; }

            auto net = make (s);

            // Sample the whole band once.
            constexpr int kPoints = 2000;
            std::vector<double> f (kPoints), db (kPoints);
            for (int i = 0; i < kPoints; ++i)
            {
                f[(size_t) i]  = 20.0 * std::pow (1000.0, (double) i / (kPoints - 1));
                db[(size_t) i] = net.magnitudeDbAt (f[(size_t) i]);
            }

            const auto boost = c.high ? db.back() : db.front();

            // Corner and slope, walking away from the shelf plateau.
            const auto step = c.high ? -1 : 1;
            const auto start = c.high ? kPoints - 1 : 0;

            double corner = 0.0, at15 = 0.0, dip = 0.0, dipAt = 0.0;

            for (int i = start; i >= 0 && i < kPoints; i += step)
            {
                if (corner == 0.0 && db[(size_t) i] < boost - 3.0)  corner = f[(size_t) i];
                if (at15   == 0.0 && db[(size_t) i] < boost - 15.0) at15   = f[(size_t) i];
                if (db[(size_t) i] < dip) { dip = db[(size_t) i]; dipAt = f[(size_t) i]; }
            }

            const auto slope = (corner > 0.0 && at15 > 0.0)
                                 ? 12.0 / std::abs (std::log2 (at15 / corner)) : 0.0;

            std::printf ("%14s %+8.1f %9.0f %11.1f %9.2f %9.1f%%  (at %.0f Hz)\n",
                         c.name, boost, corner, slope, dip, 100.0 * std::abs (dip) / boost, dipAt);
        }
    }

    /** High-pass shape, reported as the measured plots were read. */
    void printHpf()
    {
        std::printf ("High-pass. Measured from an assembled board: no peak at all (worst\n"
                     "+0.0 dB), and -3 dB corners about 10 %% below the marked frequency\n"
                     "-- 45, 71, 139 and 270 Hz for the 50, 80, 160 and 300 settings.\n\n"
                     "%9s %9s %8s %10s %10s\n", "marked", "-3 dB", "err %", "peak dB", "dB/oct");

        for (int position = 1; position <= 4; ++position)
        {
            const auto marked = hpfFreqHz (position);

            EqSettings s;
            s.hpfFreqHz = marked;
            auto net = make (s);

            double peak = -100.0, corner = 0.0, at6 = 0.0, at24 = 0.0;

            for (int i = 3000; i >= 0; --i)
            {
                const auto f = 20.0 * std::pow (1000.0, (double) i / 3000.0);
                const auto db = net.magnitudeDbAt (f);

                if (db > peak) peak = db;
                if (corner == 0.0 && db < -3.0)  corner = f;
                if (at6    == 0.0 && db < -6.0)  at6 = f;
                if (at24   == 0.0 && db < -24.0) at24 = f;
            }

            const auto slope = (at6 > 0.0 && at24 > 0.0 && at6 > at24)
                                 ? 18.0 / std::log2 (at6 / at24) : 0.0;

            std::printf ("%9.0f %9.0f %8.1f %10.2f %10.1f\n",
                         marked, corner, 100.0 * (corner - marked) / marked, peak, slope);
        }
    }

    void printQ()
    {
        std::printf ("Proportional Q: realised mid-bell width at 1.6 kHz, measured 3 dB\n"
                     "below the peak. Nothing models this explicitly -- it falls out of\n"
                     "the shared feedback structure.\n\n%8s %10s\n", "gain dB", "octaves");

        for (double gain : { 3.0, 6.0, 9.0, 12.0, 15.0, 18.0 })
        {
            EqSettings s; s.midFreqHz = 1600.0f; s.midGainDb = (float) gain;
            auto n = make (s);

            const auto threshold = n.magnitudeDbAt (1600.0) - 3.0;

            const auto edge = [&] (double dir)
            {
                double hz = 1600.0;

                for (int i = 0; i < 6000; ++i)
                {
                    hz *= std::pow (2.0, dir * 0.001);

                    if (hz < 10.0 || hz > 22000.0 || n.magnitudeDbAt (hz) < threshold)
                        break;
                }

                return hz;
            };

            std::printf ("%+8.1f %10.2f\n", gain, std::log2 (edge (1.0) / edge (-1.0)));
        }
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    const std::string command = argc > 1 ? argv[1] : "curve";

    if (command == "bands")   { printInteraction(); return 0; }
    if (command == "q")       { printQ();           return 0; }
    if (command == "alias")   { printAliasing();    return 0; }
    if (command == "bell")    { printBell();        return 0; }
    if (command == "fitq")    { printFitQ();        return 0; }
    if (command == "shelf")   { printShelf();       return 0; }
    if (command == "hpf")     { printHpf();         return 0; }

    if (command == "thd" || command == "profile")
    {
        double hz = 1000.0, level = -6.0;
        float inputGain = 0.0f;
        int os = 2;

        for (int i = 2; i + 1 < argc; ++i)
        {
            const std::string a = argv[i];
            if      (a == "--freq")  hz        = std::atof (argv[i + 1]);
            else if (a == "--level") level     = std::atof (argv[i + 1]);
            else if (a == "--in")    inputGain = (float) std::atof (argv[i + 1]);
            else if (a == "--os")    os        = std::atoi (argv[i + 1]);
        }

        if (command == "profile") printProfile (os);
        else                      printThd (hz, level, inputGain, os);

        return 0;
    }

    EqSettings s;
    int index = 0;
    float gainDb = 0.0f;

    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto* next = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if      (a == "--hiq")                    s.midHiQ = true;
        else if (a == "--lf"  && next && parseBand (next, index, gainDb)) { s.lfFreqHz  = lowShelfFreqHz (index);        s.lfGainDb  = gainDb; }
        else if (a == "--mid" && next && parseBand (next, index, gainDb)) { s.midFreqHz = midFreqHz (index);             s.midGainDb = gainDb; }
        else if (a == "--hf"  && next && parseBand (next, index, gainDb)) { s.hfFreqHz  = highShelfFreqHz (index); s.hfGainDb = gainDb; }
        else if (a == "--hpf" && next)            s.hpfFreqHz = hpfFreqHz (std::atoi (next));
        else if (a == "--lpf" && next)            s.lpfFreqHz = lpfFreqHz (std::atoi (next));
    }

    auto net = make (s);
    printCurve (net);
    return 0;
}
