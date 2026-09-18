// measure_deq -- the zero-latency dynamic EQ's DSP, measured with no host.
//
//   measure_deq cramp [rate] [--json file]   accuracy grid vs the analogue prototype,
//                                            matched-Z against the cookbook bilinear
//   measure_deq gate                         the spec's comparative gate, config by config
//   measure_deq detector [attack] [release]  step response of detector + gain computer
//   measure_deq topology                     parallel vs serial on overlapping bands
//   measure_deq throughput                   CPU per block against band count (wall clock:
//                                            informative, never a test)
//   measure_deq curve <shape> <f0> <q> <gain> [rate]
//                                            one band against its prototype and the cookbook
//   measure_deq render <in.wav> <out> [--blind [seed]] [--case name] [--match]
//                                            the topology listening set, serial and parallel,
//                                            as 32-bit float WAVs (testing-notes/deq-topology-listening.md)
//
// The DSP tests (tests/dsp/DeqDspTests.cpp) say what the numbers must be; this
// says what they are. Its output is deterministic apart from `throughput`.

#include "modules/deq/dsp/DspCore.h"
#include "modules/deq/reference/Reference.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace bmo::deq;
namespace ref = bmo::deq::reference;

namespace
{
    const char* kShapeNames[] = { "bell", "lowshelf", "highshelf", "lowcut", "highcut" };
    constexpr double kGridF0[] = { 50, 200, 1000, 5000, 10000, 14000, 16000, 18000 };
    constexpr double kGridQ[]  = { 0.5, 0.707, 2, 4, 8, 16 };
    constexpr double kGridG[]  = { -24, -18, -12, -6, -3, 3, 6, 12, 18, 24 };

    bool isCut (Shape s) { return s == Shape::lowCut || s == Shape::highCut; }

    bool parseShape (const char* name, Shape& out)
    {
        for (int i = 0; i < 5; ++i)
            if (std::strcmp (name, kShapeNames[i]) == 0) { out = (Shape) i; return true; }
        return false;
    }

    //==========================================================================
    int cramp (double fs, const char* jsonPath)
    {
        std::FILE* json = jsonPath != nullptr ? std::fopen (jsonPath, "w") : nullptr;
        if (json) std::fprintf (json, "{\n  \"sampleRate\": %g,\n  \"configs\": [\n", fs);
        bool firstJson = true;

        std::printf ("Accuracy vs the analogue prototype at %g Hz. Worst |dB| per region:\n", fs);
        std::printf ("  r1 20 Hz-0.25 Fs (target 0.05)   r2 0.25-0.40 Fs (0.15)   r3 0.40-0.45 Fs (0.50)\n");
        std::printf ("Cuts skip points where both responses are below -60 dB.\n\n");

        for (int t = 0; t < 5; ++t)
        {
            const auto shape = (Shape) t;
            const auto floorDb = isCut (shape) ? -60.0 : -1.0e9;
            std::printf ("%s\n     f0   pass   matched r1/r2/r3            bilinear r1/r2/r3\n", kShapeNames[t]);

            double worstAll[3] = { 0, 0, 0 }, worstShelfQ[3] = { 0, 0, 0 };

            for (double f0 : kGridF0)
            {
                int pass = 0, total = 0;
                double wm[3] = { 0, 0, 0 }, wb[3] = { 0, 0, 0 };

                for (double q : kGridQ)
                    for (double g : kGridG)
                    {
                        if (isCut (shape) && g != kGridG[0]) continue;
                        const auto gg = isCut (shape) ? 0.0 : g;
                        const auto p = Prototype::make (shape, f0, q, gg);
                        const auto m = ref::regionErrors (designMatched (shape, f0, q, gg, fs), p, fs, floorDb);
                        const auto b = ref::regionErrors (ref::cookbook (shape, f0, q, gg, fs), p, fs, floorDb);

                        bool ok = true;
                        for (int r = 0; r < 3; ++r)
                        {
                            ok = ok && m.worst[r] <= ref::kRegionTargetDb[r];
                            wm[r] = std::max (wm[r], m.worst[r]);
                            wb[r] = std::max (wb[r], b.worst[r]);
                            worstAll[r] = std::max (worstAll[r], m.worst[r]);
                            if (q <= 2.0) worstShelfQ[r] = std::max (worstShelfQ[r], m.worst[r]);
                        }
                        pass += ok; ++total;

                        if (json)
                        {
                            std::fprintf (json, "%s    {\"shape\":\"%s\",\"f0\":%g,\"q\":%g,\"gain\":%g,"
                                                "\"matched\":[%.6f,%.6f,%.6f],\"bilinear\":[%.6f,%.6f,%.6f],\"pass\":%s}",
                                          firstJson ? "" : ",\n", kShapeNames[t], f0, q, gg,
                                          m.worst[0], m.worst[1], m.worst[2], b.worst[0], b.worst[1], b.worst[2],
                                          ok ? "true" : "false");
                            firstJson = false;
                        }
                    }

                std::printf ("  %6.0f  %3d/%-3d  %6.3f %6.3f %6.3f        %7.3f %7.3f %7.3f\n",
                             f0, pass, total, wm[0], wm[1], wm[2], wb[0], wb[1], wb[2]);
            }

            std::printf ("  worst, all Q:   %.4f %.4f %.4f\n", worstAll[0], worstAll[1], worstAll[2]);
            std::printf ("  worst, Q <= 2:  %.4f %.4f %.4f\n\n", worstShelfQ[0], worstShelfQ[1], worstShelfQ[2]);
        }

        if (json)
        {
            std::fprintf (json, "\n  ]\n}\n");
            std::fclose (json);
            std::printf ("wrote %s\n", jsonPath);
        }

        return 0;
    }

    //==========================================================================
    int gate()
    {
        const double fs = 44100.0;
        std::printf ("Comparative gate: region-3 (0.40-0.45 Fs) peak error, bells, f0 >= 10k, Q >= 4, |g| >= 12\n");
        std::printf ("     f0     Q    gain   bilinear  matched   ratio\n");
        double minRatio = 1.0e300;

        for (double f0 : { 10000.0, 14000.0, 16000.0, 18000.0 })
            for (double q : { 4.0, 8.0, 16.0 })
                for (double g : { -24.0, -18.0, -12.0, 12.0, 18.0, 24.0 })
                {
                    const auto p = Prototype::make (Shape::bell, f0, q, g);
                    const auto m = ref::regionErrors (designMatched (Shape::bell, f0, q, g, fs), p, fs).worst[2];
                    const auto b = ref::regionErrors (ref::cookbook (Shape::bell, f0, q, g, fs), p, fs).worst[2];
                    const auto ratio = b / std::max (m, 1.0e-12);
                    minRatio = std::min (minRatio, ratio);
                    std::printf ("  %6.0f  %4.0f  %+5.0f   %7.4f  %7.4f  %6.1fx\n", f0, q, g, b, m, ratio);
                }

        std::printf ("smallest ratio %.2fx (%.1f dB); the gate asks for 2x (6 dB)\n", minRatio, 20.0 * std::log10 (minRatio));
        return 0;
    }

    //==========================================================================
    int detector (double attackMs, double releaseMs)
    {
        const double fs = 48000.0;
        Detector d;
        d.configure (attackMs, releaseMs, false, fs);
        GainComputer c; c.thresholdDb = -20.0; c.ratio = 4.0; c.kneeDb = 0.0; c.rangeDb = -40.0;

        const auto lo = std::pow (10.0, -40.0 / 20.0), hi = std::pow (10.0, -6.0 / 20.0);
        for (int i = 0; i < (int) fs; ++i) d.process (lo);

        const auto target = c.offsetDb (-6.0);
        std::printf ("Step -40 -> -6 dBFS at t = 0, back to -40 at t = 1 s. T -20, R 4:1, attack %g ms, release %g ms\n", attackMs, releaseMs);
        std::printf ("static target %.2f dB\n      t ms    envelope dB   offset dB   error dB\n", target);

        const int total = (int) (2.0 * fs);
        for (int n = 0; n < total; ++n)
        {
            const auto env = d.process (n < (int) fs ? hi : lo);
            const auto db  = 20.0 * std::log10 (env);
            const auto off = c.offsetDb (db);
            const auto t   = (double) n / fs * 1000.0;
            const auto sinceEdge = n < (int) fs ? n : n - (int) fs;

            if (sinceEdge < 8 || sinceEdge % (int) (fs / 200) == 0)
                std::printf ("  %8.3f    %9.3f   %9.3f   %8.3f\n", t, db, off, n < (int) fs ? off - target : off);
        }

        return 0;
    }

    //==========================================================================
    /** One band of a topology comparison. */
    struct Band { Shape shape; double hz, q, gainDb; };

    struct Comparison
    {
        double worstDb = 0.0, atHz = 0.0, parallelDb = 0.0, serialDb = 0.0;
        bool parallelInverts = false;   // the parallel sum's real part goes negative
    };

    /** Parallel against serial for a centred source, over 20 Hz-20 kHz. Serial
        is also "what the band curves add up to": in series each band's dB
        response simply adds, which is what a user reading the curves expects. */
    Comparison compareTopologies (const std::vector<Band>& bands, double fs = 48000.0)
    {
        Settings s;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& b = s.bands[i];
            b.enabled = true; b.shape = bands[i].shape;
            b.frequencyHz = bands[i].hz; b.q = bands[i].q; b.gainDb = bands[i].gainDb;
        }

        DspCore par, ser;
        par.prepare (fs, 512, 2); ser.prepare (fs, 512, 2);
        s.topology = Topology::parallel; par.setSettings (s);
        s.topology = Topology::serial;   ser.setSettings (s);

        Comparison c;
        for (int i = 0; i < 2048; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, i / 2047.0);
            const auto hp = par.staticResponseAt (hz);
            const auto p  = 20.0 * std::log10 (std::max (std::abs (hp), 1.0e-12));
            const auto q  = 20.0 * std::log10 (std::max (std::abs (ser.staticResponseAt (hz)), 1.0e-12));
            c.parallelInverts = c.parallelInverts || hp.real() < 0.0;
            if (std::abs (p - q) > c.worstDb) { c.worstDb = std::abs (p - q); c.atHz = hz; c.parallelDb = p; c.serialDb = q; }
        }

        return c;
    }

    /** Is the parallel sum minimum phase? Its numerator is
        prod(D_k) + sum_k (N_k - D_k) prod_{j != k} D_j, degree 2N; its zeros
        are found by Durand-Kerner. A serial chain of minimum-phase bands is
        minimum phase by construction; a sum of them need not be. */
    bool parallelIsMinimumPhase (const std::vector<Band>& bands, double fs = 48000.0)
    {
        using Poly = std::vector<std::complex<double>>;   // coefficients of z^-0, z^-1, ...
        auto mul = [] (const Poly& a, const Poly& b)
        {
            Poly r (a.size() + b.size() - 1, 0.0);
            for (size_t i = 0; i < a.size(); ++i)
                for (size_t j = 0; j < b.size(); ++j)
                    r[i + j] += a[i] * b[j];
            return r;
        };
        auto add = [] (Poly a, const Poly& b)
        {
            if (b.size() > a.size()) a.resize (b.size(), 0.0);
            for (size_t i = 0; i < b.size(); ++i) a[i] += b[i];
            return a;
        };

        std::vector<Poly> num, den;
        for (const auto& b : bands)
        {
            const auto q = designMatched (b.shape, b.hz, b.q, b.gainDb, fs);
            num.push_back ({ q.b0, q.b1, q.b2 });
            den.push_back ({ 1.0, q.a1, q.a2 });
        }

        Poly total { 1.0 };
        for (const auto& d : den) total = mul (total, d);

        for (size_t k = 0; k < bands.size(); ++k)
        {
            Poly term { 1.0 };
            for (size_t j = 0; j < bands.size(); ++j)
                term = mul (term, j == k ? add (num[j], Poly { -den[j][0], -den[j][1], -den[j][2] }) : den[j]);
            total = add (total, term);
        }

        // In z: total[0] z^n + total[1] z^(n-1) + ... ; roots of that polynomial.
        const auto n = total.size() - 1;
        if (n == 0 || std::abs (total[0]) < 1.0e-300) return false;
        std::vector<std::complex<double>> roots (n);
        for (size_t i = 0; i < n; ++i) roots[i] = std::polar (0.9, 0.4 + 2.0 * kPi * (double) i / (double) n);

        for (int iter = 0; iter < 2000; ++iter)
        {
            double moved = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                std::complex<double> v = total[0];
                for (size_t k = 1; k <= n; ++k) v = v * roots[i] + total[k];
                std::complex<double> d = total[0];
                for (size_t j = 0; j < n; ++j) if (j != i) d *= roots[i] - roots[j];
                const auto step = v / d;
                roots[i] -= step;
                moved = std::max (moved, std::abs (step));
            }
            if (moved < 1.0e-14) break;
        }

        for (const auto& r : roots)
            if (std::abs (r) > 1.0 + 1.0e-9)
                return false;
        return true;
    }

    /** Cut filters in series, gain bands in parallel -- a third option, not in
        the engine; evaluated analytically here. Order does not matter for a
        static centred source: everything is linear and time-invariant. */
    double hybridResponseDbAt (const std::vector<Band>& bands, double hz, double fs = 48000.0)
    {
        std::complex<double> cuts = 1.0, sum = 1.0;
        const auto w = 2.0 * kPi * hz / fs;
        for (const auto& b : bands)
        {
            const auto h = designMatched (b.shape, b.hz, b.q, b.gainDb, fs).responseAt (w);
            if (hasGain (b.shape)) sum += h - 1.0;
            else                   cuts *= h;
        }
        return 20.0 * std::log10 (std::max (std::abs (cuts * sum), 1.0e-12));
    }

    double responseDbAt (const std::vector<Band>& bands, Topology t, double hz, double fs = 48000.0)
    {
        Settings s;
        s.topology = t;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& b = s.bands[i];
            b.enabled = true; b.shape = bands[i].shape;
            b.frequencyHz = bands[i].hz; b.q = bands[i].q; b.gainDb = bands[i].gainDb;
        }
        DspCore e; e.prepare (fs, 512, 2); e.setSettings (s);
        return 20.0 * std::log10 (std::max (std::abs (e.staticResponseAt (hz)), 1.0e-12));
    }

    int topology()
    {
        std::printf ("PARALLEL (spec C4: out = x + sum(H_k x - x)) vs SERIAL (out = ... H_2(H_1(x))).\n");
        std::printf ("Centred source, 48 kHz. Serial = the band curves added in dB.\n\n");

        //== A: bands stacked on one frequency ================================
        std::printf ("A. N identical bells stacked at 1 kHz, Q 1 -- level at 1 kHz\n");
        std::printf ("   INVERT = the parallel sum's polarity flips somewhere; NON-MIN = it is not minimum phase\n");
        std::printf ("   gain each   serial 2x   parallel 2x                 serial 3x   parallel 3x\n");
        for (double g : { -24.0, -18.0, -12.0, -9.0, -6.0, -3.0, 3.0, 6.0, 12.0, 18.0, 24.0 })
        {
            const Band b { Shape::bell, 1000.0, 1.0, g };
            const auto s2 = responseDbAt ({ b, b }, Topology::serial, 1000.0);
            const auto p2 = responseDbAt ({ b, b }, Topology::parallel, 1000.0);
            const auto s3 = responseDbAt ({ b, b, b }, Topology::serial, 1000.0);
            const auto p3 = responseDbAt ({ b, b, b }, Topology::parallel, 1000.0);
            const auto inv2 = compareTopologies ({ b, b }).parallelInverts;
            const auto inv3 = compareTopologies ({ b, b, b }).parallelInverts;
            const auto min2 = parallelIsMinimumPhase ({ b, b });
            const auto min3 = parallelIsMinimumPhase ({ b, b, b });
            std::printf ("   %+6.0f dB   %+8.2f   %+8.2f %-7s%-8s   %+8.2f   %+8.2f %-7s%s\n", g, s2, p2,
                         inv2 ? "INVERT" : "", min2 ? "" : "NON-MIN", s3, p3, inv3 ? "INVERT" : "", min3 ? "" : "NON-MIN");
        }

        //== B: how far apart before it stops mattering =======================
        std::printf ("\nB. Two bells 1 kHz and 1 kHz x 2^oct -- worst |parallel - serial| over the band, dB\n");
        std::printf ("   pair            Q    oct: 0      1/3    2/3    1      2      3      4\n");
        struct Pair { const char* name; double g1, g2; };
        for (const auto& pr : { Pair { "+6 / +6", 6, 6 }, Pair { "-6 / -6", -6, -6 }, Pair { "+6 / -6", 6, -6 },
                                Pair { "+12 / -12", 12, -12 }, Pair { "-12 / -12", -12, -12 } })
            for (double q : { 1.0, 4.0 })
            {
                std::printf ("   %-12s  %4.1f        ", pr.name, q);
                for (double oct : { 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0, 2.0, 3.0, 4.0 })
                {
                    const auto c = compareTopologies ({ { Shape::bell, 1000.0, q, pr.g1 },
                                                        { Shape::bell, 1000.0 * std::pow (2.0, oct), q, pr.g2 } });
                    std::printf ("%6.2f ", c.worstDb);
                }
                std::printf ("\n");
            }

        //== C: cut filters next to a band that boosts =========================
        std::printf ("\nC. A cut filter overlapping a boost -- does the cut still cut?\n");
        struct Filt { const char* name; std::vector<Band> bands; double probeHz; };
        const Filt filts[] =
        {
            { "low cut 80 Hz + low shelf +6 dB @ 100 Hz",
              { { Shape::lowCut, 80, 0.707, 0 }, { Shape::lowShelf, 100, 0.707, 6 } }, 20.0 },
            { "low cut 80 Hz + bell +6 dB @ 60 Hz Q 1",
              { { Shape::lowCut, 80, 0.707, 0 }, { Shape::bell, 60, 1.0, 6 } }, 25.0 },
            { "high cut 12 kHz + high shelf +4 dB @ 10 kHz",
              { { Shape::highCut, 12000, 0.707, 0 }, { Shape::highShelf, 10000, 0.707, 4 } }, 20000.0 },
            { "high cut 8 kHz + bell +3 dB @ 3 kHz Q 0.7",
              { { Shape::highCut, 8000, 0.707, 0 }, { Shape::bell, 3000, 0.7, 3 } }, 20000.0 },
        };
        for (const auto& f : filts)
        {
            const auto cutOnly = responseDbAt ({ f.bands[0] }, Topology::serial, f.probeHz);
            std::printf ("   %-44s at %5.0f Hz: cut alone %+7.2f, serial %+7.2f, parallel %+7.2f, hybrid %+7.2f\n", f.name, f.probeHz, cutOnly,
                         responseDbAt (f.bands, Topology::serial, f.probeHz), responseDbAt (f.bands, Topology::parallel, f.probeHz),
                         hybridResponseDbAt (f.bands, f.probeHz));
        }

        //== D: settings people actually use ==================================
        std::printf ("\nD. Typical settings -- worst |parallel - serial| over 20 Hz-20 kHz\n");
        struct Mix { const char* name; std::vector<Band> bands; };
        const Mix mixes[] =
        {
            { "vocal: LS -3@120, bell -2.5@350 Q1.2, bell +2@3k Q0.8, HS +3@10k",
              { { Shape::lowShelf, 120, 0.707, -3 }, { Shape::bell, 350, 1.2, -2.5 }, { Shape::bell, 3000, 0.8, 2 }, { Shape::highShelf, 10000, 0.707, 3 } } },
            { "kick: bell +4@60 Q1.5, bell -5@400 Q2, bell +3@4k Q1",
              { { Shape::bell, 60, 1.5, 4 }, { Shape::bell, 400, 2, -5 }, { Shape::bell, 4000, 1, 3 } } },
            { "bus: LS +1@80, HS +1.5@12k",
              { { Shape::lowShelf, 80, 0.707, 1 }, { Shape::highShelf, 12000, 0.707, 1.5 } } },
            { "surgical: bell -12@2.5k Q8, bell -9@3.1k Q8",
              { { Shape::bell, 2500, 8, -12 }, { Shape::bell, 3100, 8, -9 } } },
            { "broad tilt: bell +3@200 Q0.5, bell -3@2k Q0.5",
              { { Shape::bell, 200, 0.5, 3 }, { Shape::bell, 2000, 0.5, -3 } } },
        };
        for (const auto& m : mixes)
        {
            const auto c = compareTopologies (m.bands);
            std::printf ("   %-66s %5.2f dB at %6.0f Hz%s%s\n", m.name, c.worstDb, c.atHz,
                         c.parallelInverts ? "  INVERT" : "", parallelIsMinimumPhase (m.bands) ? "" : "  NON-MIN");
        }

        //== E: dynamics at full excursion =====================================
        std::printf ("\nE. A dynamic band at full excursion over a static band (the worst moment of the gesture)\n");
        const Mix dyn[] =
        {
            { "de-ess: dynamic bell -> -10@7k Q2 over static HS +4@8k",
              { { Shape::highShelf, 8000, 0.707, 4 }, { Shape::bell, 7000, 2, -10 } } },
            { "resonance tamer: dynamic bell -> -12@2.5k Q4 over static bell +3@2k Q1",
              { { Shape::bell, 2000, 1, 3 }, { Shape::bell, 2500, 4, -12 } } },
            { "low end: dynamic LS -> -6@100 over static bell +4@80 Q1",
              { { Shape::bell, 80, 1, 4 }, { Shape::lowShelf, 100, 0.707, -6 } } },
        };
        for (const auto& m : dyn)
        {
            const auto c = compareTopologies (m.bands);
            std::printf ("   %-66s %5.2f dB at %6.0f Hz%s%s\n", m.name, c.worstDb, c.atHz,
                         c.parallelInverts ? "  INVERT" : "", parallelIsMinimumPhase (m.bands) ? "" : "  NON-MIN");
        }

        //== F: cost ============================================================
        std::printf ("\nF. CPU, 24 bands, 48 kHz, 128-sample blocks, stereo (wall clock, informative)\n");
        for (int dynamic = 0; dynamic < 2; ++dynamic)
        {
            double us[2] = { 0, 0 };
            for (int t = 0; t < 2; ++t)
            {
                Settings s;
                s.topology = t == 0 ? Topology::parallel : Topology::serial;
                for (int i = 0; i < 24; ++i)
                {
                    auto& b = s.bands[(size_t) i];
                    b.enabled = true; b.frequencyHz = 30.0 * std::pow (1.3, i); b.q = 1.5; b.gainDb = i % 2 ? 3.0 : -3.0;
                    b.dynamics.enabled = dynamic == 1; b.dynamics.thresholdDb = -30.0;
                }
                DspCore e; e.prepare (48000.0, 128, 2); e.setSettings (s);
                std::vector<double> l (128), r (128);
                double seed = 0.3;
                const auto start = std::chrono::steady_clock::now();
                for (int k = 0; k < 3000; ++k)
                {
                    for (int i = 0; i < 128; ++i) { seed = std::fmod (seed * 3.7 + 0.31, 1.0); l[(size_t) i] = seed - 0.5; r[(size_t) i] = 0.5 - seed; }
                    double* ch[2] { l.data(), r.data() };
                    e.process (ch, 2, 128);
                }
                us[t] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count() / 3000.0;
            }
            std::printf ("   %-8s parallel %7.2f us/block   serial %7.2f us/block\n", dynamic ? "dynamic" : "static", us[0], us[1]);
        }

        return 0;
    }

    //==========================================================================
    // render: the topology listening test, before there is a plugin to load.
    //==========================================================================

    struct Wav
    {
        std::vector<float> l, r;
        int sampleRate = 0, channels = 0;
        bool ok = false;
    };

    /** 16/24/32-bit integer and 32-bit float, plain or WAVE_FORMAT_EXTENSIBLE
        (everything Live bounces). Same rules as measure_dim's reader: any
        other format is refused rather than guessed at. */
    Wav readWav (const std::string& path)
    {
        Wav w;
        std::FILE* f = std::fopen (path.c_str(), "rb");
        if (f == nullptr) { std::fprintf (stderr, "cannot open %s\n", path.c_str()); return w; }
        std::fseek (f, 0, SEEK_END);
        const long len = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        std::vector<uint8_t> b ((size_t) std::max (len, 0L));
        const bool read = ! b.empty() && std::fread (b.data(), 1, b.size(), f) == b.size();
        std::fclose (f);

        if (! read || b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        {
            std::fprintf (stderr, "not a RIFF/WAVE file: %s\n", path.c_str());
            return w;
        }

        size_t p = 12, dataOffset = 0, dataLength = 0;
        uint16_t formatTag = 1, bits = 0;

        while (p + 8 <= b.size())
        {
            char id[5] = {};
            std::memcpy (id, &b[p], 4);
            uint32_t size;
            std::memcpy (&size, &b[p + 4], 4);
            const size_t body = p + 8;

            if (std::strcmp (id, "fmt ") == 0 && body + 16 <= b.size())
            {
                uint16_t ch; uint32_t sr;
                std::memcpy (&formatTag, &b[body], 2);
                std::memcpy (&ch, &b[body + 2], 2);
                std::memcpy (&sr, &b[body + 4], 4);
                std::memcpy (&bits, &b[body + 14], 2);
                w.channels = ch; w.sampleRate = (int) sr;
                if (formatTag == 0xFFFE && size >= 26 && body + 26 <= b.size())
                    std::memcpy (&formatTag, &b[body + 24], 2);
            }
            else if (std::strcmp (id, "data") == 0)
            {
                dataOffset = body; dataLength = size;
            }

            if (size == 0) break;
            p = body + size + (size & 1);
        }

        if (dataOffset == 0 || w.channels < 1) { std::fprintf (stderr, "no audio in %s\n", path.c_str()); return w; }
        if (dataOffset + dataLength > b.size()) dataLength = b.size() - dataOffset;

        const int bytes = bits / 8;
        if ((formatTag != 1 && formatTag != 3) || bytes < 2 || bytes > 4 || (formatTag == 3 && bits != 32))
        {
            std::fprintf (stderr, "unsupported WAV (format 0x%04x, %d bit): %s\n", (unsigned) formatTag, (int) bits, path.c_str());
            return w;
        }

        const size_t frames = dataLength / (size_t) (bytes * w.channels);
        w.l.resize (frames); w.r.resize (frames);

        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const size_t o = dataOffset + (i * (size_t) w.channels + (size_t) std::min (c, w.channels - 1)) * (size_t) bytes;
                float v = 0.0f;
                if (bits == 16)      { int16_t x; std::memcpy (&x, &b[o], 2); v = (float) x / 32768.0f; }
                else if (bits == 24) { int32_t x = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16);
                                       if ((x & 0x800000) != 0) x -= 0x1000000;
                                       v = (float) x / 8388608.0f; }
                else if (formatTag == 3) std::memcpy (&v, &b[o], 4);
                else                 { int32_t x; std::memcpy (&x, &b[o], 4); v = (float) ((double) x / 2147483648.0); }
                (c == 0 ? w.l : w.r)[i] = v;
            }

        w.ok = true;
        return w;
    }

    /** 32-bit float, so nothing clips in the file however hot a boost gets. */
    bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int channels, int sampleRate)
    {
        std::FILE* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) { std::fprintf (stderr, "cannot write %s\n", path.c_str()); return false; }

        const uint32_t frames = (uint32_t) l.size(), dataBytes = frames * (uint32_t) channels * 4u;
        auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

        std::fwrite ("RIFF", 1, 4, f); u32 (36u + dataBytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3); u16 ((uint16_t) channels);
        u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * (uint32_t) channels * 4u);
        u16 ((uint16_t) (channels * 4)); u16 (32);
        std::fwrite ("data", 1, 4, f); u32 (dataBytes);

        for (uint32_t i = 0; i < frames; ++i)
        {
            std::fwrite (&l[i], 4, 1, f);
            if (channels > 1) std::fwrite (&r[i], 4, 1, f);
        }

        std::fclose (f);
        return true;
    }

    /** The listening set: the cases from spec/topology-options.md where the
        topologies measurably differ, plus one where they should not. */
    struct ListeningCase
    {
        const char* name;
        const char* listenFor;
        std::vector<BandSettings> bands;
    };

    BandSettings bell (double hz, double q, double g)  { BandSettings b; b.enabled = true; b.shape = Shape::bell; b.frequencyHz = hz; b.q = q; b.gainDb = g; return b; }
    BandSettings shelf (Shape s, double hz, double g) { BandSettings b; b.enabled = true; b.shape = s; b.frequencyHz = hz; b.q = 0.707; b.gainDb = g; return b; }
    BandSettings cut (Shape s, double hz)             { BandSettings b; b.enabled = true; b.shape = s; b.frequencyHz = hz; b.q = 0.707; return b; }

    BandSettings dynamic (BandSettings b, double rangeDb, double thresholdDb, double attackMs, double releaseMs)
    {
        b.dynamics.enabled = true; b.dynamics.rangeDb = rangeDb; b.dynamics.thresholdDb = thresholdDb;
        b.dynamics.ratio = 4.0; b.dynamics.kneeDb = 6.0; b.dynamics.attackMs = attackMs; b.dynamics.releaseMs = releaseMs;
        return b;
    }

    std::vector<ListeningCase> listeningCases()
    {
        return {
            { "lowcut-under-shelf", "rumble below 40 Hz: does the low cut still remove it?",
              { cut (Shape::lowCut, 80), shelf (Shape::lowShelf, 100, 6) } },
            { "stacked-cuts-12", "two -12 dB cuts at 1 kHz: -24 dB (serial) or -6 dB and inverted (parallel)",
              { bell (1000, 1, -12), bell (1000, 1, -12) } },
            { "stacked-cuts-6", "two -6 dB cuts at 1 kHz: -12 dB (serial) or a -52 dB notch (parallel)",
              { bell (1000, 1, -6), bell (1000, 1, -6) } },
            { "stacked-boosts", "two +6 dB boosts at 3 kHz: +12 dB (serial) or +9.5 dB (parallel)",
              { bell (3000, 1, 6), bell (3000, 1, 6) } },
            { "surgical", "two narrow cuts a third apart: up to 6.8 dB different between them",
              { bell (2500, 8, -12), bell (3100, 8, -9) } },
            { "vocal-control", "CONTROL: separated, moderate bands -- should be indistinguishable (0.25 dB)",
              { shelf (Shape::lowShelf, 120, -3), bell (350, 1.2, -2.5), bell (3000, 0.8, 2), shelf (Shape::highShelf, 10000, 3) } },
            { "deess-dynamic", "dynamic cut at 7 kHz under a static high shelf: how hard the sibilance is caught",
              { shelf (Shape::highShelf, 8000, 4), dynamic (bell (7000, 2, 0), -10, -30, 2, 80) } },
            { "tamer-dynamic", "dynamic cut at 2.5 kHz beside a static boost at 2 kHz: depth of the dip on hits",
              { bell (2000, 1, 3), dynamic (bell (2500, 4, 0), -12, -30, 5, 100) } },
        };
    }

    struct Rendered { std::vector<float> l, r; double maxGrDb = 0.0; };

    Rendered renderCase (const Wav& in, const std::vector<BandSettings>& bands, Topology topology, bool reverse = false)
    {
        Settings s;
        s.topology = topology;
        for (size_t i = 0; i < bands.size(); ++i)
            s.bands[reverse ? bands.size() - 1 - i : i] = bands[i];

        DspCore e;
        const int channels = std::min (in.channels, 2);
        e.prepare ((double) in.sampleRate, 512, channels);
        e.setSettings (s);

        Rendered out { in.l, in.r, 0.0 };
        for (size_t pos = 0; pos < out.l.size(); pos += 512)
        {
            const auto n = (int) std::min ((size_t) 512, out.l.size() - pos);
            float* ch[2] { out.l.data() + pos, out.r.data() + pos };
            e.process (ch, channels, n);
            out.maxGrDb = std::max (out.maxGrDb, e.currentGainReductionDb());
        }
        return out;
    }

    double rmsDb (const std::vector<float>& a, const std::vector<float>* minus = nullptr)
    {
        double acc = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double v = minus ? (double) a[i] - (double) (*minus)[i] : (double) a[i];
            acc += v * v;
        }
        return 10.0 * std::log10 (std::max (acc / (double) std::max<size_t> (a.size(), 1), 1.0e-30));
    }

    double peakDb (const std::vector<float>& a)
    {
        double p = 0.0;
        for (float v : a) p = std::max (p, (double) std::abs (v));
        return 20.0 * std::log10 (std::max (p, 1.0e-15));
    }

    /** Energy in an octave either side of `f0`, in dB: Welch over 4096-sample
        Hann frames. Whole-file RMS cannot see a narrow dynamic band -- a Q 4
        bell at 2.5 kHz moves it by hundredths of a dB while moving the band
        itself by ten -- so a level match has to be made where the band works.
    */
    double bandRmsDb (const std::vector<float>& x, double f0, double fs)
    {
        constexpr size_t kFrame = 4096;
        if (x.size() < kFrame) return -300.0;

        const auto lo = (size_t) std::max (1.0, std::floor (f0 / 2.0 / fs * (double) kFrame));
        const auto hi = (size_t) std::min ((double) kFrame / 2.0 - 1.0, std::ceil (f0 * 2.0 / fs * (double) kFrame));

        double acc = 0.0;
        size_t frames = 0;

        for (size_t pos = 0; pos + kFrame <= x.size(); pos += kFrame / 2)
        {
            std::vector<std::complex<double>> a (kFrame);
            for (size_t i = 0; i < kFrame; ++i)
            {
                const auto w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (kFrame - 1));
                a[i] = { (double) x[pos + i] * w, 0.0 };
            }

            ref::fft (a);
            for (size_t i = lo; i <= hi; ++i) acc += std::norm (a[i]);
            ++frames;
        }

        return 10.0 * std::log10 (std::max (acc / (double) std::max<size_t> (frames, 1), 1.0e-30));
    }

    /** The serial range that puts serial's output at parallel's level on this
        material, for a dynamic case.

        Parallel catches less than it is asked to, because the static band's
        path carries the signal past the dynamic one. So comparing the two at
        the same `range` compares two different amounts of gain reduction, and
        a listener is answering "how much" rather than "which behaviour".
        Matching the level first leaves only the behaviour: how the catch
        moves, which is the part no knob reproduces.

        Output level falls monotonically as range deepens, so a bisection
        finds it in a dozen renders rather than a sweep's hundreds. */
    double matchedSerialRange (const Wav& in, const std::vector<BandSettings>& bands, double asked,
                               double parallelBandDb, double f0)
    {
        auto levelAt = [&] (double range)
        {
            auto b = bands;
            for (auto& band : b)
                if (band.dynamics.enabled)
                    band.dynamics.rangeDb = range;
            return bandRmsDb (renderCase (in, b, Topology::serial).l, f0, in.sampleRate);
        };

        double deep = asked, shallow = 0.0;

        for (int i = 0; i < 14; ++i)
        {
            const auto mid = 0.5 * (deep + shallow);
            (levelAt (mid) < parallelBandDb ? deep : shallow) = mid;
        }

        return 0.5 * (deep + shallow);
    }

    int render (const std::string& inPath, const std::string& outDir, bool blind, unsigned seed, const std::string& only,
                bool matchLevel)
    {
        const auto in = readWav (inPath);
        if (! in.ok) return 1;

        std::printf ("%s: %d Hz, %d channel%s, %.1f s, input RMS %.1f dBFS, peak %.1f dBFS\n", inPath.c_str(), in.sampleRate,
                     in.channels, in.channels == 1 ? "" : "s", (double) in.l.size() / in.sampleRate, rmsDb (in.l), peakDb (in.l));
        if (in.channels > 2) std::printf ("  more than two channels: only the first two are rendered\n");

        std::FILE* key = nullptr;
        if (blind)
        {
            key = std::fopen ((outDir + "/key.txt").c_str(), "w");
            if (key == nullptr) { std::fprintf (stderr, "cannot write %s/key.txt -- does the folder exist?\n", outDir.c_str()); return 1; }
            std::fprintf (key, "Blind key for %s (seed %u). Open after listening.\n\n", inPath.c_str(), seed);
        }

        std::printf ("\n  case                  serial-vs-parallel   serial peak   parallel peak   max GR   order residual\n");

        for (const auto& c : listeningCases())
        {
            if (! only.empty() && only != c.name) continue;

            auto serialBands = c.bands;
            const auto par = renderCase (in, c.bands, Topology::parallel);
            double matched = 0.0;
            bool wasMatched = false;

            if (matchLevel)
            {
                double asked = 0.0, f0 = 1000.0;
                for (const auto& b : c.bands)
                    if (b.dynamics.enabled)
                        { asked = b.dynamics.rangeDb; f0 = b.frequencyHz; }

                if (asked < 0.0)
                {
                    matched = matchedSerialRange (in, c.bands, asked, bandRmsDb (par.l, f0, in.sampleRate), f0);
                    wasMatched = true;
                    for (auto& b : serialBands)
                        if (b.dynamics.enabled)
                            b.dynamics.rangeDb = matched;
                }
            }

            const auto ser = renderCase (in, serialBands, Topology::serial);
            const auto diff = rmsDb (ser.l, &par.l) - rmsDb (in.l);   // relative to the input's level
            const auto channels = std::min (in.channels, 2);

            if (matchLevel && ! wasMatched)
                continue;   // --match is about the dynamic cases; a static one has nothing to match

            // Serial in the reverse band order: only a dynamic case can differ.
            std::string order = "--";
            if (ser.maxGrDb > 0.0)
            {
                const auto rev = renderCase (in, c.bands, Topology::serial, true);
                char buf[32]; std::snprintf (buf, sizeof buf, "%.1f dB", rmsDb (ser.l, &rev.l) - rmsDb (in.l));
                order = buf;
            }

            std::printf ("  %-20s  %8.1f dB rel. in   %8.1f dBFS   %8.1f dBFS   %5.1f dB   %s\n", c.name, diff,
                         peakDb (ser.l), peakDb (par.l), ser.maxGrDb, order.c_str());

            if (wasMatched)
            {
                double f0 = 1000.0, asked = 0.0;
                for (const auto& b : c.bands)
                    if (b.dynamics.enabled) { f0 = b.frequencyHz; asked = b.dynamics.rangeDb; }

                const auto pb = bandRmsDb (par.l, f0, in.sampleRate);
                const auto sb = bandRmsDb (ser.l, f0, in.sampleRate);

                std::printf ("      level-matched: serial ran at %.2f dB of range rather than the case's %.0f, which puts\n"
                             "      the band an octave either side of %.0f Hz at %.2f dB in both (%.3f dB apart).\n"
                             "      What is left to hear is the shape of the catch.\n",
                             matched, asked, f0, pb, std::abs (sb - pb));

                if (matched <= asked + 0.01)
                    std::printf ("      (Nothing had to be given up: serial at full range already sat at parallel's\n"
                                 "      level on this material.)\n");
            }

            if (blind)
            {
                // Deterministic from the seed and the case, so a rerun with the
                // same seed reproduces the same assignment.
                unsigned h = seed * 2654435761u;
                for (const char* p = c.name; *p != 0; ++p) h = (h ^ (unsigned char) *p) * 16777619u;
                const bool serialIsA = (h >> 7) % 2 == 0;
                writeWav (outDir + "/" + c.name + "-A.wav", serialIsA ? ser.l : par.l, serialIsA ? ser.r : par.r, channels, in.sampleRate);
                writeWav (outDir + "/" + c.name + "-B.wav", serialIsA ? par.l : ser.l, serialIsA ? par.r : ser.r, channels, in.sampleRate);
                std::fprintf (key, "%-20s A = %s, B = %s\n    listen for: %s\n", c.name, serialIsA ? "serial" : "parallel",
                              serialIsA ? "parallel" : "serial", c.listenFor);
            }
            else
            {
                writeWav (outDir + "/" + c.name + "-serial.wav", ser.l, ser.r, channels, in.sampleRate);
                writeWav (outDir + "/" + c.name + "-parallel.wav", par.l, par.r, channels, in.sampleRate);

                std::vector<float> dl (ser.l.size()), dr (ser.r.size());
                for (size_t i = 0; i < dl.size(); ++i) { dl[i] = ser.l[i] - par.l[i]; dr[i] = ser.r[i] - par.r[i]; }
                writeWav (outDir + "/" + c.name + "-difference.wav", dl, dr, channels, in.sampleRate);
            }
        }

        if (key) std::fclose (key);

        std::printf ("\nwrote 32-bit float WAVs to %s%s\n", outDir.c_str(),
                     blind ? " as <case>-A/-B; the key is in key.txt -- open it after listening" : " (serial, parallel, and their difference)");
        std::printf ("A dynamic case with max GR 0.0 never engaged on this material: its threshold is -30 dB on the band's sidechain.\n");
        return 0;
    }

    //==========================================================================
    int throughput()
    {
        const double fs = 48000.0;
        const int block = 128, blocks = 3000;
        std::printf ("CPU per 128-sample block at 48 kHz, stereo, %d blocks (wall clock; varies run to run)\n", blocks);
        std::printf ("  bands   static us/block   dynamic us/block   dynamic us per band\n");

        std::vector<double> l (block), r (block);
        double seed = 0.1;

        for (int count : { 1, 4, 12, 24, 48 })
        {
            double us[2] = { 0, 0 };

            for (int dyn = 0; dyn < 2; ++dyn)
            {
                Settings s;
                for (int i = 0; i < count; ++i)
                {
                    auto& b = s.bands[(size_t) i];
                    b.enabled = true;
                    b.shape = i % 3 == 1 ? Shape::lowShelf : Shape::bell;
                    b.frequencyHz = 30.0 * std::pow (1.15, i);
                    b.q = 1.0 + (i % 3);
                    b.gainDb = i % 2 ? 4.0 : -4.0;
                    b.dynamics.enabled = dyn == 1;
                    b.dynamics.thresholdDb = -30.0;
                }

                DspCore e; e.prepare (fs, block, 2); e.setSettings (s);
                const auto start = std::chrono::steady_clock::now();

                for (int k = 0; k < blocks; ++k)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        seed = std::fmod (seed * 3.7 + 0.31, 1.0);
                        l[(size_t) i] = seed - 0.5; r[(size_t) i] = 0.5 - seed;
                    }
                    double* ch[2] { l.data(), r.data() };
                    e.process (ch, 2, block);
                }

                const auto end = std::chrono::steady_clock::now();
                us[dyn] = std::chrono::duration<double, std::micro> (end - start).count() / blocks;
            }

            std::printf ("  %5d   %13.2f    %14.2f    %14.3f\n", count, us[0], us[1], us[1] / count);
        }

        std::printf ("real time is %.0f us per block\n", 1.0e6 * block / fs);

        // What one redesign costs, per shape. A dynamic band pays this once per
        // control interval while its gain is moving.
        std::printf ("\ndesignMatched cost with the engine's prebuilt grid (ns per call):\n");
        const auto grid = DesignGrid::make (fs);
        for (int t = 0; t < 5; ++t)
        {
            constexpr int calls = 200000;
            double sink = 0.0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < calls; ++i)
                sink += designMatched ((Shape) t, 1000.0 + (i % 97), 1.3, -12.0 + (i % 25), grid).b1;
            const auto ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / calls;
            std::printf ("  %-10s %8.1f   (%g)\n", kShapeNames[t], ns, sink * 0.0);
        }

        return 0;
    }

    //==========================================================================
    int curve (Shape shape, double f0, double q, double g, double fs)
    {
        const auto p = Prototype::make (shape, f0, q, g);
        const auto m = designMatched (shape, f0, q, g, fs);
        const auto b = ref::cookbook (shape, f0, q, g, fs);
        std::printf ("%s f0 %g Q %g gain %g at %g Hz\n        Hz    analogue    matched   bilinear\n", kShapeNames[(int) shape], f0, q, g, fs);

        for (int i = 0; i < 64; ++i)
        {
            const auto hz = 20.0 * std::pow (0.499 * fs / 20.0, i / 63.0);
            std::printf ("  %8.1f   %8.3f   %8.3f   %8.3f\n", hz, p.magnitudeDbAt (hz), m.magnitudeDbAt (hz, fs), b.magnitudeDbAt (hz, fs));
        }

        return 0;
    }
}

int main (int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";

    if (cmd == "cramp")
    {
        double fs = 44100.0; const char* json = nullptr;
        for (int i = 2; i < argc; ++i)
        {
            if (std::strcmp (argv[i], "--json") == 0 && i + 1 < argc) json = argv[++i];
            else fs = std::atof (argv[i]);
        }
        return cramp (fs, json);
    }

    if (cmd == "render" && argc >= 4)
    {
        bool blind = false, match = false; unsigned seed = 1; std::string only;
        for (int i = 4; i < argc; ++i)
        {
            if (std::strcmp (argv[i], "--blind") == 0) { blind = true; if (i + 1 < argc && std::isdigit ((unsigned char) argv[i + 1][0])) seed = (unsigned) std::strtoul (argv[++i], nullptr, 10); }
            else if (std::strcmp (argv[i], "--case") == 0 && i + 1 < argc) only = argv[++i];
            else if (std::strcmp (argv[i], "--match") == 0) match = true;
        }
        std::error_code ec;
        std::filesystem::create_directories (argv[3], ec);
        if (ec || ! std::filesystem::is_directory (argv[3]))
        {
            // Most often Windows' 260-character path limit: the folder plus
            // "<case>-difference.wav" has to fit. Say so rather than write nothing.
            std::fprintf (stderr, "cannot create output folder %s (%s)%s\n", argv[3], ec.message().c_str(),
                          std::strlen (argv[3]) > 200 ? " -- the path is long; Windows limits paths to 260 characters" : "");
            return 1;
        }
        return render (argv[2], argv[3], blind, seed, only, match);
    }

    if (cmd == "gate")       return gate();
    if (cmd == "topology")   return topology();
    if (cmd == "throughput") return throughput();
    if (cmd == "detector")   return detector (argc > 2 ? std::atof (argv[2]) : 0.1, argc > 3 ? std::atof (argv[3]) : 100.0);

    if (cmd == "curve" && argc >= 6)
    {
        Shape s;
        if (! parseShape (argv[2], s)) { std::fprintf (stderr, "unknown shape %s\n", argv[2]); return 2; }
        return curve (s, std::atof (argv[3]), std::atof (argv[4]), std::atof (argv[5]), argc > 6 ? std::atof (argv[6]) : 44100.0);
    }

    std::fprintf (stderr,
        "usage: measure_deq cramp [rate] [--json file] | gate | detector [attack_ms] [release_ms]\n"
        "                   | topology | throughput | curve <shape> <f0> <q> <gain> [rate]\n"
        "                   | render <in.wav> <out-folder> [--blind [seed]] [--case name]\n"
        "shapes: bell lowshelf highshelf lowcut highcut\n");
    return 2;
}
