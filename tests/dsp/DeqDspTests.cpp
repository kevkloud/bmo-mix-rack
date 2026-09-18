// BMO DEQ -- the zero-latency dynamic EQ's DSP, held to the
// spec's T-suites. Each group says which T it is and, where the spec's own
// wording could not be met or measured as written, what it asserts instead
// and why. modules/deq/AGENTS.md has the reasoning in full.

#include "modules/deq/dsp/DspCore.h"
#include "modules/deq/reference/Reference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace bmo::deq;
namespace ref = bmo::deq::reference;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    void checkClose (double actual, double expected, double tol, const std::string& what)
    {
        if (! (std::abs (actual - expected) <= tol))
        {
            std::cerr << "FAIL: " << what << " -- expected " << expected
                      << " +/- " << tol << ", got " << actual << '\n';
            ++failures;
        }
    }

    void checkAtMost (double actual, double limit, const std::string& what)
    {
        if (! (actual <= limit))
        {
            std::cerr << "FAIL: " << what << " -- limit " << limit << ", got " << actual << '\n';
            ++failures;
        }
    }

    std::string cfg (Shape s, double f0, double q, double g)
    {
        static const char* names[] = { "bell", "lowShelf", "highShelf", "lowCut", "highCut" };
        return std::string (names[(int) s]) + " f0=" + std::to_string (f0) + " Q=" + std::to_string (q)
             + " g=" + std::to_string (g);
    }

    constexpr double kRates[]  = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    constexpr double kGridF0[] = { 50, 200, 1000, 5000, 10000, 14000, 16000, 18000 };
    constexpr double kGridQ[]  = { 0.5, 0.707, 2, 4, 8, 16 };
    constexpr double kGridG[]  = { -24, -18, -12, -6, -3, 3, 6, 12, 18, 24 };
    constexpr Shape  kGainShapes[] = { Shape::bell, Shape::lowShelf, Shape::highShelf };

    /** Reversing the order of bandsConfig(3) -- 24 overlapping dynamic bands,
        0.5 ms attack, 12 dB of range, driven hard -- measured -20.4 dBFS on
        2026-09-10. Realistic settings measure -37 to -51 (see AGENTS.md). */
    constexpr double kDynamicOrderResidualDb = -18.0;
    constexpr Shape  kCutShapes[]  = { Shape::lowCut, Shape::highCut };

    //== Signals: deterministic, no wall clock, no library RNG ================
    struct Lcg
    {
        uint32_t s = 0x2545F491u;
        double next() { s = s * 1664525u + 1013904223u; return (double) s / 4294967296.0 * 2.0 - 1.0; }
    };

    /** Paul Kellet's economy pink filter over a fixed-seed LCG. */
    std::vector<double> pinkNoise (size_t n, double peakDb, uint32_t seed = 0x2545F491u)
    {
        Lcg rng; rng.s = seed;
        std::vector<double> out (n);
        double b0 = 0, b1 = 0, b2 = 0, peak = 0;

        for (auto& v : out)
        {
            const auto w = rng.next();
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            v = b0 + b1 + b2 + w * 0.1848;
            peak = std::max (peak, std::abs (v));
        }

        const auto scale = std::pow (10.0, peakDb / 20.0) / peak;
        for (auto& v : out) v *= scale;
        return out;
    }

    struct Stereo { std::vector<double> l, r; };

    /** Run a stereo buffer through an engine in blocks of `block`. */
    template <typename Sample = double>
    Stereo render (DspCore& dsp, const Stereo& in, int block)
    {
        std::vector<Sample> l (in.l.size()), r (in.r.size());
        for (size_t i = 0; i < l.size(); ++i) { l[i] = (Sample) in.l[i]; r[i] = (Sample) in.r[i]; }

        for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, l.size() - pos);
            Sample* ch[2] { l.data() + pos, r.data() + pos };
            dsp.process (ch, 2, n);
        }

        return { { l.begin(), l.end() }, { r.begin(), r.end() } };
    }

    DspCore makeEngine (double rate, const Settings& s)
    {
        DspCore d;
        d.prepare (rate, 4096, 2);
        d.setSettings (s);
        return d;
    }

    /** The configurations T1 runs: bypassed, one static band, 24 static
        bands, 24 bands with dynamics that are reducing gain. */
    Settings bandsConfig (int which)
    {
        Settings s;
        if (which == 0) return s;

        const int count = which == 1 ? 1 : 24;
        for (int i = 0; i < count; ++i)
        {
            auto& b = s.bands[(size_t) i];
            b.enabled = true;
            b.shape = i % 5 == 3 ? Shape::lowShelf : (i % 5 == 4 ? Shape::highShelf : Shape::bell);
            b.frequencyHz = 40.0 * std::pow (2.0, i * 0.37);
            b.q = 0.5 + (i % 4) * 1.5;
            b.gainDb = (i % 2 ? 6.0 : -6.0);
            b.placement = i % 3 == 0 ? Placement::stereo : (i % 3 == 1 ? Placement::mid : Placement::side);
            b.msAmount = 0.25 + 0.25 * (i % 4);

            if (which == 3)
            {
                b.dynamics.enabled = true;
                b.dynamics.thresholdDb = -40.0;
                b.dynamics.ratio = 4.0;
                b.dynamics.rangeDb = -12.0;
                b.dynamics.attackMs = 0.5;
                b.dynamics.releaseMs = 50.0;
            }
        }

        return s;
    }

    double peakAbs (const std::vector<double>& v, size_t from = 0, size_t to = (size_t) -1)
    {
        double p = 0;
        for (size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, std::abs (v[i]));
        return p;
    }

    double toDb (double linear) { return 20.0 * std::log10 (std::max (linear, 1.0e-300)); }

    //==========================================================================
    // T1 -- latency. The product claim; runs first.
    //==========================================================================
    /** T-new: solo and the analyser tap, the two paths that are not
        parameters. Both were added on 2026-09-12 (spec/decisions.md); the
        claims worth protecting are that neither can change the sound and
        neither can move the latency. */
    void testSoloAndTap()
    {
        const Stereo in { pinkNoise (8000, -6.0), pinkNoise (8000, -6.0, 4242u) };

        // -- Solo ------------------------------------------------------------
        //
        // Soloing does not take the band out of the chain: every band still
        // runs, and solo only decides what leaves. So the exact claim is that
        // the contributions add up -- the sum of every band soloed in turn is
        // what the EQ did to the signal, whatever the topology.
        //
        // (An earlier version of this test asserted that solo equals the whole
        // output minus the same EQ with that band switched off. That is only
        // true in parallel: in serial, removing a band changes what every band
        // after it sees, and the test failed by 0.12 -- correctly.)
        for (auto topology : { Topology::serial, Topology::parallel })
        {
            Settings s;
            s.topology = topology;
            const auto label = topology == Topology::serial ? std::string ("serial") : std::string ("parallel");

            const int used = 4;
            for (int i = 0; i < used; ++i)
            {
                auto& b = s.bands[(size_t) i];
                b.enabled = true;
                b.shape = i == 0 ? Shape::lowShelf : (i == 3 ? Shape::highShelf : Shape::bell);
                b.frequencyHz = 120.0 * std::pow (3.0, i * 0.8);
                b.q = 0.7 + 0.6 * i;
                b.gainDb = i % 2 ? 5.0 : -4.0;
            }

            // One of them dynamic, so the moving case is covered too.
            s.bands[2].dynamics.enabled = true;
            s.bands[2].dynamics.thresholdDb = -34.0;
            s.bands[2].dynamics.ratio = 3.0;
            s.bands[2].dynamics.rangeDb = -9.0;
            s.bands[2].dynamics.attackMs = 2.0;
            s.bands[2].dynamics.releaseMs = 60.0;

            auto full = makeEngine (48000.0, s);
            const auto whole = render (full, in, 256);

            std::vector<double> sum (whole.l.size(), 0.0);

            for (int band = 0; band < used; ++band)
            {
                auto one = makeEngine (48000.0, s);
                one.setSolo (band);
                const auto only = render (one, in, 256);
                for (size_t i = 0; i < sum.size(); ++i) sum[i] += only.l[i];
            }

            double worst = 0.0;
            for (size_t i = 0; i < sum.size(); ++i)
                worst = std::max (worst, std::abs (sum[i] - (whole.l[i] - in.l[i])));

            checkAtMost (worst, 1.0e-12, "T9: the soloed bands add up to what the EQ did, " + label);

            // A band that is off contributes nothing, so soloing it is silence.
            auto offSolo = s;
            offSolo.bands[1].enabled = false;
            auto quiet = makeEngine (48000.0, offSolo);
            quiet.setSolo (1);
            const auto nothing = render (quiet, in, 256);
            checkAtMost (peakAbs (nothing.l), 1.0e-12, "T9: soloing a band that is off is silence, " + label);

            // A band nobody configured, likewise.
            auto never = makeEngine (48000.0, s);
            never.setSolo (30);
            const auto empty = render (never, in, 256);
            checkAtMost (peakAbs (empty.l), 1.0e-12, "T9: soloing a band that was never set up is silence, " + label);

            // -1 puts it back, bit for bit.
            auto cleared = makeEngine (48000.0, s);
            cleared.setSolo (2);
            cleared.setSolo (-1);
            const auto restored = render (cleared, in, 256);
            double diff = 0.0;
            for (size_t i = 0; i < whole.l.size(); ++i) diff = std::max (diff, std::abs (restored.l[i] - whole.l[i]));
            checkAtMost (diff, 0.0, "T9: clearing solo restores the output exactly, " + label);
        }

        // Solo does not move the latency: the reported figure is a constant,
        // and the first sample out is still the first sample in.
        check (DspCore::latencySamples() == 0, "T9: latency is still 0 with solo available");

        // Block-size invariance holds with a solo held down, which is what
        // makes reading it once per block rather than per sample necessary.
        {
            auto s = bandsConfig (3);
            auto ref = makeEngine (48000.0, s);
            ref.setSolo (1);
            const auto y0 = render (ref, in, 4096);

            for (int block : { 1, 37, 512 })
            {
                auto e = makeEngine (48000.0, s);
                e.setSolo (1);
                const auto y = render (e, in, block);
                double worst = 0.0;
                for (size_t i = 0; i < y0.l.size(); ++i) worst = std::max (worst, std::abs (y.l[i] - y0.l[i]));
                checkAtMost (worst, 0.0, "T9: block-size invariance holds under solo, block " + std::to_string (block));
            }
        }

        // -- The analyser tap -------------------------------------------------
        //
        // The claim is that it cannot change the sound. Same engine, same
        // input, tap off and tap on: the output has to be bit-identical.
        {
            auto s = bandsConfig (3);
            auto silentTap = makeEngine (48000.0, s);
            const auto without = render (silentTap, in, 512);

            auto watched = makeEngine (48000.0, s);
            watched.postTap().setEnabled (true);
            watched.preTap().setEnabled (true);
            const auto with = render (watched, in, 512);

            double worst = 0.0;
            for (size_t i = 0; i < without.l.size(); ++i) worst = std::max (worst, std::abs (with.l[i] - without.l[i]));
            checkAtMost (worst, 0.0, "T9: a tap that is being read does not change the output");

            // And it read something: the newest samples are the output's.
            std::vector<float> window (1024, 0.0f);
            const auto got = watched.postTap().read (window.data(), (int) window.size());
            check (got == (int) window.size(), "T9: the post tap fills the window it is asked for");

            double tapError = 0.0;
            for (size_t i = 0; i < window.size(); ++i)
            {
                const auto at = with.l.size() - window.size() + i;
                const auto expected = 0.5 * (with.l[at] + with.r[at]);
                tapError = std::max (tapError, std::abs ((double) window[i] - expected));
            }
            checkAtMost (tapError, 1.0e-6, "T9: the post tap holds the mono sum of what was output");

            // Nothing is written while no panel is looking.
            auto closed = makeEngine (48000.0, s);
            const auto ignored = render (closed, in, 512);
            (void) ignored;
            std::vector<float> empty (16, 1.0f);
            check (closed.postTap().read (empty.data(), (int) empty.size()) == 0,
                   "T9: a tap nobody enabled stays empty");
        }
    }

    void testLatency()
    {
        check (DspCore::latencySamples() == 0, "T1: reported latency is 0");

        const int blocks[] = { 1, 32, 64, 111, 512, 4096 };

        for (double rate : kRates)
            for (int block : blocks)
                for (int which = 0; which < 4; ++which)
                {
                    // Dynamics first have to be reducing, so a pre-roll drives
                    // them; latency is then the first sample at which an impulse
                    // makes any difference to the output.
                    const size_t preroll = which == 3 ? (size_t) (0.03 * rate) : 0;
                    const size_t n = preroll + 256;

                    Stereo base { pinkNoise (n, -6.0), pinkNoise (n, -6.0, 77u) };
                    for (size_t i = preroll; i < n; ++i) base.l[i] = base.r[i] = 0.0;
                    auto withImpulse = base;
                    withImpulse.l[preroll] = withImpulse.r[preroll] = 1.0;

                    auto a = makeEngine (rate, bandsConfig (which));
                    auto b = makeEngine (rate, bandsConfig (which));
                    const auto ya = render (a, base, block);
                    const auto yb = render (b, withImpulse, block);

                    size_t first = n;
                    for (size_t i = 0; i < n && first == n; ++i)
                        if (std::abs (yb.l[i] - ya.l[i]) > 1.0e-12 || std::abs (yb.r[i] - ya.r[i]) > 1.0e-12)
                            first = i;

                    check (first == preroll, "T1: first responding sample is the impulse's own, rate "
                           + std::to_string ((int) rate) + " block " + std::to_string (block)
                           + " config " + std::to_string (which) + " (got " + std::to_string ((long long) first - (long long) preroll) + ")");

                    if (which == 3)
                        check (b.currentGainReductionDb() > 1.0, "T1: the dynamic config is actually reducing gain");
                }
    }

    /** No hidden block-level state: one 4096-sample block and 4096 blocks of
        one sample give the same bits, with dynamics, M/S and both topologies. */
    void testBlockSizeInvariance()
    {
        for (auto topology : { Topology::parallel, Topology::serial })
            for (double rate : { 48000.0, 96000.0 })
            {
                auto s = bandsConfig (3);
                s.topology = topology;
                const Stereo in { pinkNoise (12000, -3.0), pinkNoise (12000, -3.0, 991u) };

                auto ref = makeEngine (rate, s);
                const auto y0 = render (ref, in, 4096);

                for (int block : { 1, 37, 111, 512 })
                {
                    auto e = makeEngine (rate, s);
                    const auto y = render (e, in, block);
                    const auto same = std::memcmp (y.l.data(), y0.l.data(), y.l.size() * sizeof (double)) == 0
                                   && std::memcmp (y.r.data(), y0.r.data(), y.r.size() * sizeof (double)) == 0;
                    check (same, "T1: bit-identical output at block " + std::to_string (block)
                           + (topology == Topology::serial ? " (serial)" : " (parallel)"));
                }
            }
    }

    //==========================================================================
    // T2 -- accuracy against the analogue prototype.
    //==========================================================================
    void testAccuracy()
    {
        const double fs = 44100.0;

        // The spec's absolute targets, where a second-order filter can meet
        // them. Above a few hundred Hz a wide, loud bell or shelf has skirt
        // past Nyquist that no biquad can follow (see AGENTS.md); those
        // configurations are held by the comparative gate and by the ceilings
        // below instead.
        for (Shape shape : kGainShapes)
            for (double f0 : { 50.0, 200.0 })
                for (double q : kGridQ)
                    for (double g : kGridG)
                    {
                        const auto e = ref::regionErrors (designMatched (shape, f0, q, g, fs), Prototype::make (shape, f0, q, g), fs);
                        for (int r = 0; r < 3; ++r)
                            checkAtMost (e.worst[r], ref::kRegionTargetDb[r], "T2: spec target, region " + std::to_string (r + 1) + ", " + cfg (shape, f0, q, g));
                    }

        // The comparative gate, as the spec states it: where the design choice
        // matters most (f0 >= 10 kHz, Q >= 4, |gain| >= 12 dB), matched-Z must
        // halve the bilinear design's peak error near Nyquist.
        for (double f0 : { 10000.0, 14000.0, 16000.0, 18000.0 })
            for (double q : { 4.0, 8.0, 16.0 })
                for (double g : { -24.0, -18.0, -12.0, 12.0, 18.0, 24.0 })
                {
                    const auto p = Prototype::make (Shape::bell, f0, q, g);
                    const auto mz = ref::regionErrors (designMatched (Shape::bell, f0, q, g, fs), p, fs).worst[2];
                    const auto bl = ref::regionErrors (ref::cookbook (Shape::bell, f0, q, g, fs), p, fs).worst[2];
                    check (bl >= 2.0 * mz, "T2: matched-Z halves bilinear's region-3 error, " + cfg (Shape::bell, f0, q, g)
                           + " (bilinear " + std::to_string (bl) + ", matched " + std::to_string (mz) + ")");
                }

        // A bell's peak is where the knob says, at the height it says, and DC
        // is untouched: the three conditions the bell's zeros are fitted to.
        // Exact in exact arithmetic; 1e-5 dB is the rounding left when f0 is
        // under ~3e-4 of the sample rate (50 Hz at 176.4 kHz measures 2e-6),
        // where the fit subtracts two nearly equal numbers. See Design.cpp.
        for (double rate : kRates)
            for (double f0 : kGridF0)
                for (double q : kGridQ)
                    for (double g : kGridG)
                    {
                        const auto b = designMatched (Shape::bell, f0, q, g, rate);
                        checkClose (b.magnitudeDbAt (f0, rate), g, 1.0e-5, "T2: bell gain at f0, " + cfg (Shape::bell, f0, q, g));
                        checkClose (std::abs (b.responseAt (0.0)), 1.0, 1.0e-9, "T2: bell unity at DC, " + cfg (Shape::bell, f0, q, g));
                    }

        // Symmetry: a boost and its exact inverse cut null. Exact by
        // construction, because the cut *is* the boost's reciprocal.
        double worstSym = 0.0;
        for (Shape shape : kGainShapes)
            for (double f0 : kGridF0)
                for (double q : kGridQ)
                    for (double g : { 3.0, 12.0, 24.0 })
                    {
                        const auto up = designMatched (shape, f0, q, g, fs), dn = designMatched (shape, f0, q, -g, fs);
                        for (int i = 0; i < 512; ++i)
                        {
                            const auto hz = 10.0 * std::pow (0.499 * fs / 10.0, i / 511.0);
                            worstSym = std::max (worstSym, std::abs (up.magnitudeDbAt (hz, fs) + dn.magnitudeDbAt (hz, fs)));
                        }
                    }
        checkAtMost (worstSym, 1.0e-6, "T2: boost then cut is flat (spec: 0.01 dB)");
    }

    /** Regression ceilings over the whole T2 grid: the worst error measured
        when this suite was written, rounded up. Tighten them when the design
        improves; loosening one is a decision, made in the commit that does it.
        `tools/measure/deq cramp` prints the live figures. */
    void testAccuracyCeilings()
    {
        // Measured 2026-09-10 at 44.1 kHz, +5 %. The high shelf equals the low
        // shelf exactly because it is built from it (Design.cpp).
        struct Ceiling { Shape shape; double r1, r2, r3; };
        const Ceiling ceilings[] =
        {
            { Shape::bell,      1.40, 0.98, 1.29 },   // measured 1.331 0.933 1.225
            { Shape::lowShelf,  0.32, 0.54, 0.92 },   //          0.299 0.508 0.870 (Q <= 2)
            { Shape::highShelf, 0.32, 0.54, 0.92 },   //          0.299 0.508 0.870 (Q <= 2)
            { Shape::lowCut,    1.89, 1.69, 3.03 },   //          1.793 1.607 2.880
            { Shape::highCut,   0.25, 0.66, 1.32 },   //          0.234 0.626 1.254
        };

        const double fs = 44100.0;

        for (const auto& c : ceilings)
        {
            const auto cut = c.shape == Shape::lowCut || c.shape == Shape::highCut;
            double worst[3] = { 0, 0, 0 };

            for (double f0 : kGridF0)
                for (double q : kGridQ)
                    for (double g : kGridG)
                    {
                        if (cut && g != kGridG[0]) continue;
                        // Shelves are held at shelf-sized Q; a Q 16 shelf is a
                        // resonance with a shelf attached, and is reported by
                        // the measure tool rather than gated.
                        if ((c.shape == Shape::lowShelf || c.shape == Shape::highShelf) && q > 2.0) continue;
                        const auto gg = cut ? 0.0 : g;
                        const auto e = ref::regionErrors (designMatched (c.shape, f0, q, gg, fs), Prototype::make (c.shape, f0, q, gg), fs, cut ? -60.0 : -1.0e9);
                        for (int r = 0; r < 3; ++r) worst[r] = std::max (worst[r], e.worst[r]);
                    }

            const double lim[3] = { c.r1, c.r2, c.r3 };
            for (int r = 0; r < 3; ++r)
                checkAtMost (worst[r], lim[r], "T2 ceiling: " + cfg (c.shape, 0, 0, 0) + " region " + std::to_string (r + 1));
        }
    }

    //==========================================================================
    // T3 -- coefficient integrity.
    //==========================================================================
    void testCoefficientIntegrity()
    {
        // A low cut's zeros are a double zero at DC -- two coincident roots on
        // the unit circle -- and that is the case a root-radius minimum-phase
        // test cannot decide. The discriminant of a double root is rounding
        // noise, and its square root magnifies that noise to about 1e-8 in the
        // radius. On macOS the noise came out non-zero (clang contracts
        // b1*b1 - 4*b0*b2 into an fma; MSVC does not), the low cut's own fit
        // was rejected as not minimum phase, Design.cpp fell back to mapped
        // zeros, and T2's low-cut ceilings went from 1.793/1.607 to
        // 3.313/2.404 -- on macOS alone. Fork run 34657686217.
        for (double k : { 1.0e-3, 0.25, 1.0, 1.0e3 })
        {
            const auto scale = "k=" + std::to_string (k);
            check (Biquad { k, -2.0 * k, k, 0.0, 0.0 }.isMinimumPhase(),
                   "T3: a double zero on the unit circle is minimum phase, " + scale);

            // Perturbed so the two roots are genuinely inside, by a margin far
            // smaller than the noise a root test would read as being outside.
            for (const auto& e : { std::pair { 1.0e-16, "1e-16" }, { 1.0e-15, "1e-15" },
                                    { 1.0e-13, "1e-13" }, { 1.0e-12, "1e-12" } })
                check (Biquad { k * (1.0 + e.first), -2.0 * k, k * (1.0 - e.first), 0.0, 0.0 }.isMinimumPhase(),
                       "T3: zeros just inside the circle are minimum phase, " + scale
                       + " eps=" + e.second);

            // Still rejected when a zero is really outside: the product of the
            // roots is 1 + 1e-3 here, which no tolerance should swallow.
            check (! (Biquad { k, -2.0 * k, k * 1.001, 0.0, 0.0 }.isMinimumPhase()),
                   "T3: a zero outside the circle is not minimum phase, " + scale);
        }

        // The pole invariant, stated against the prototype's own poles:
        // a2 = e^(-(d1/d2)/Fs), the product of the two mapped poles. The spec
        // writes it as e^(-w0/Q) with the knob values, which is only true of
        // the cut filters (a bell's pole Q is A*Q). It holds wherever poles are
        // mapped rather than fitted: every boost except the high shelf, whose
        // poles are the low shelf's fitted zeros (Design.cpp says why), and
        // both cut filters. A negative-gain band's poles are its boost's zeros.
        for (double rate : { 44100.0, 192000.0 })
            for (Shape shape : { Shape::bell, Shape::lowShelf, Shape::highShelf, Shape::lowCut, Shape::highCut })
                for (double f0 : kGridF0)
                    for (double q : kGridQ)
                        for (double g : { 0.0, 3.0, 12.0, 24.0 })
                        {
                            if (shape == Shape::highShelf && g > 0.0)
                                continue;

                            const auto gg = hasGain (shape) ? g : 0.0;
                            const auto p = Prototype::make (shape, f0, q, gg);
                            const auto b = designMatched (shape, f0, q, gg, rate);
                            checkClose (b.a2, std::exp (-(p.d1 / p.d2) / rate), 1.0e-12, "T3: a2 invariant, " + cfg (shape, f0, q, gg));
                        }

        // Every combination, degenerate ones included, gives a finite, stable
        // filter, and so does its SVF form.
        const double qs[]  = { 0.0, 0.05, 0.1, 0.5, 16.0, 40.0, 1000.0 };
        const double gs[]  = { -1000.0, -60.0, -30.0, -24.0, -0.001, 0.0, 0.001, 24.0, 30.0, 60.0 };

        for (double rate : kRates)
            for (Shape shape : { Shape::bell, Shape::lowShelf, Shape::highShelf, Shape::lowCut, Shape::highCut })
                for (double f0 : { 0.0, 1.0, 5.0, 50.0, 1000.0, 18000.0, 0.499 * rate, 0.5 * rate, rate, 2.0 * rate })
                    for (double q : qs)
                        for (double g : gs)
                        {
                            const auto b = designMatched (shape, f0, q, g, rate);
                            const auto c = SvfCoeffs::fromBiquad (b);
                            const auto ok = b.isFinite() && b.isStable() && c.isStable();
                            check (ok, "T3: finite and stable at rate " + std::to_string ((int) rate) + ", " + cfg (shape, f0, q, g));
                        }

        // The coefficients and the running filter agree: the analytic response
        // of the design against the FFT of the *engine's* impulse response --
        // the SVF that actually runs, not a direct-form stand-in. The FFT is as
        // long as the filter needs (Reference.h: irLengthFor).
        for (Shape shape : kGainShapes)
            for (double f0 : { 50.0, 1000.0, 18000.0 })
                for (double q : { 0.707, 16.0 })
                    for (double g : { -24.0, -6.0, 6.0, 24.0 })
                    {
                        const double fs = 44100.0;
                        Settings s;
                        s.bands[0].enabled = true;
                        s.bands[0].shape = shape;
                        s.bands[0].frequencyHz = f0;
                        s.bands[0].q = q;
                        s.bands[0].gainDb = g;

                        const auto design = designMatched (shape, f0, q, g, fs);
                        const auto n = ref::irLengthFor (design);
                        auto e = makeEngine (fs, s);

                        std::vector<double> l (n, 0.0);
                        l[0] = 1.0;
                        double* ch[1] { l.data() };
                        e.process (ch, 1, (int) n);

                        std::vector<std::complex<double>> ir (l.begin(), l.end());
                        checkAtMost (ref::analyticVsMeasuredDb (design, ir, fs), 0.001, "T3: analytic vs engine IR, " + cfg (shape, f0, q, g));
                    }
    }

    //==========================================================================
    // T4 -- parameter modulation.
    //==========================================================================
    void testModulation()
    {
        const double fs = 48000.0;
        const int block = 64;

        // f0 swept 20 Hz -> 20 kHz over 0.5 s under pink noise, for a boost, a
        // cut and a narrow band. Stable at every step, finite, bounded.
        for (double g : { 12.0, -12.0 })
            for (double q : { 0.707, 16.0 })
            {
                Settings s;
                s.bands[0].enabled = true;
                s.bands[0].shape = Shape::bell;
                s.bands[0].q = q;
                s.bands[0].gainDb = g;
                s.bands[0].frequencyHz = 20.0;

                auto e = makeEngine (fs, s);
                const auto n = (size_t) (0.5 * fs);
                auto l = pinkNoise (n, -6.0), r = pinkNoise (n, -6.0, 5u);
                bool stable = true, finite = true;

                for (size_t pos = 0; pos < n; pos += block)
                {
                    s.bands[0].frequencyHz = 20.0 * std::pow (1000.0, (double) pos / (double) n);
                    e.setSettings (s);
                    double* ch[2] { l.data() + pos, r.data() + pos };
                    e.process (ch, 2, block);
                    stable = stable && e.allCoefficientsStable();
                    for (int i = 0; i < block; ++i) finite = finite && std::isfinite (l[pos + (size_t) i]);
                }

                const auto what = " (g " + std::to_string (g) + ", Q " + std::to_string (q) + ")";
                check (stable, "T4: stable at every step of a full-range sweep" + what);
                check (finite, "T4: finite through a full-range sweep" + what);
                checkAtMost (peakAbs (l), std::pow (10.0, (g > 0 ? g : 0.0) / 20.0) * 2.0, "T4: bounded through the sweep" + what);
            }

        // Gain stepped 0 -> +12 dB instantly on a sine at the band's centre:
        // the output settles on +12 dB and never overshoots it by 0.5 dB.
        {
            Settings s;
            s.bands[0].enabled = true;
            s.bands[0].shape = Shape::bell;
            s.bands[0].frequencyHz = 1000.0;
            s.bands[0].q = 1.0;
            auto e = makeEngine (fs, s);

            const auto n = (size_t) fs;
            std::vector<double> x (n);
            for (size_t i = 0; i < n; ++i) x[i] = 0.1 * std::sin (2.0 * kPi * 1000.0 * (double) i / fs);
            auto y = x;

            const size_t stepAt = (size_t) (0.25 * fs);
            for (size_t pos = 0; pos < n; pos += block)
            {
                s.bands[0].gainDb = pos >= stepAt ? 12.0 : 0.0;
                e.setSettings (s);
                double* ch[1] { y.data() + pos };
                e.process (ch, 1, block);
            }

            const auto settled = peakAbs (y, n - 4800, n);
            const auto during  = peakAbs (y, stepAt, n);
            checkClose (toDb (settled / 0.1), 12.0, 0.05, "T4: gain step settles on the new gain");
            checkAtMost (toDb (during / settled), 0.5, "T4: gain step overshoots by at most 0.5 dB");
        }

        // Q across its whole range inside one block, both ways: stable, finite.
        {
            Settings s;
            s.bands[0].enabled = true;
            s.bands[0].shape = Shape::bell;
            s.bands[0].frequencyHz = 3000.0;
            s.bands[0].gainDb = 18.0;
            s.bands[0].q = 0.1;
            auto e = makeEngine (fs, s);
            auto l = pinkNoise (4096, -6.0), r = pinkNoise (4096, -6.0, 9u);
            bool stable = true;

            for (size_t pos = 0; pos < 4096; pos += 512)
            {
                s.bands[0].q = (pos / 512) % 2 ? 40.0 : 0.1;
                e.setSettings (s);
                double* ch[2] { l.data() + pos, r.data() + pos };
                e.process (ch, 2, 512);
                stable = stable && e.allCoefficientsStable();
            }

            check (stable, "T4: stable while Q jumps across its whole range");
            check (std::isfinite (peakAbs (l)) && peakAbs (l) < 100.0, "T4: finite and bounded while Q jumps");
        }
    }

    //==========================================================================
    // T5 -- detector and gain computer.
    //==========================================================================

    /** Continuous-time step-down response of the release stage feeding the
        attack stage, from 1 to 0: the reference the release is held to. */
    double cascadeRemaining (double t, double tauR, double tauA)
    {
        if (std::abs (tauR - tauA) < 1.0e-12 * tauR)
            return (1.0 + t / tauR) * std::exp (-t / tauR);

        return (tauR * std::exp (-t / tauR) - tauA * std::exp (-t / tauA)) / (tauR - tauA);
    }

    double cascade63 (double tauR, double tauA)
    {
        double lo = 0.0, hi = 20.0 * (tauR + tauA);
        for (int i = 0; i < 200; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (cascadeRemaining (mid, tauR, tauA) > std::exp (-1.0) ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    void testDetector()
    {
        const double fs = 44100.0;
        const double lo = std::pow (10.0, -40.0 / 20.0), hi = std::pow (10.0, -6.0 / 20.0);
        const auto target63 = 1.0 - std::exp (-1.0);

        // Attack: a pure one-pole, 63.2 % at tau. The clock starts one sample
        // before the step, because y[0] already contains it (Dynamics.h).
        for (double attack : { 0.1, 1.0, 10.0, 100.0 })
        {
            Detector d;
            d.configure (attack, 1000.0, false, fs);
            for (int i = 0; i < 20000; ++i) d.process (lo);

            // y[n] is n + 1 samples after the origin, so a crossing a fraction
            // `frac` of the way from y[n-1] to y[n] is at n + frac samples.
            double prev = (d.envelope() - lo) / (hi - lo), t63 = -1.0;
            for (int n = 0; n < (int) (fs * 2) && t63 < 0; ++n)
            {
                const auto p = (d.process (hi) - lo) / (hi - lo);
                if (p >= target63)
                    t63 = ((double) n + (target63 - prev) / (p - prev)) / fs * 1000.0;
                prev = p;
            }

            checkClose (t63, attack, 0.05 * attack, "T5: attack time is tau, attack " + std::to_string (attack) + " ms");
        }

        // Release: the cascade of the release and attack poles.
        for (double attack : { 0.1, 1.0, 10.0, 100.0 })
            for (double release : { 10.0, 100.0, 1000.0 })
            {
                Detector d;
                d.configure (attack, release, false, fs);
                for (int i = 0; i < (int) (fs * 3); ++i) d.process (hi);

                double t63 = -1.0;
                for (int n = 0; n < (int) (fs * 12) && t63 < 0; ++n)
                    if ((hi - d.process (lo)) / (hi - lo) >= target63)
                        t63 = (double) (n + 1) / fs * 1000.0;

                const auto expected = cascade63 (release, attack);
                checkClose (t63, expected, 0.05 * expected + 1000.0 / fs, "T5: release follows the two-stage cascade, attack "
                            + std::to_string (attack) + " release " + std::to_string (release));
            }

        // The static curve, against the spec's formula written out longhand.
        for (double knee : { 0.0, 6.0, 12.0 })
            for (double ratio : { 1.5, 4.0, 20.0 })
                for (double x = -80.0; x <= 6.0; x += 0.25)
                {
                    GainComputer c;
                    c.thresholdDb = -20.0; c.ratio = ratio; c.kneeDb = knee; c.rangeDb = -100.0;
                    double y;
                    if (2 * (x + 20.0) < -knee) y = x;
                    else if (knee > 0 && std::abs (2 * (x + 20.0)) <= knee) y = x + (1 / ratio - 1) * std::pow (x + 20.0 + knee / 2, 2) / (2 * knee);
                    else y = -20.0 + (x + 20.0) / ratio;
                    checkClose (c.offsetDb (x), y - x, 1.0e-9, "T5: static curve at " + std::to_string (x) + " dB, knee " + std::to_string (knee));
                }

        // All four cases have the sign they should, and stop at the range.
        {
            GainComputer c; c.thresholdDb = -20; c.ratio = 1000; c.kneeDb = 0;
            c.direction = Direction::above; c.rangeDb = -6; checkClose (c.offsetDb (0.0),   -6.0, 1e-9, "T5: cut above threshold, stops at range");
            c.rangeDb = 6;                                  checkClose (c.offsetDb (0.0),    6.0, 1e-9, "T5: boost above threshold");
            c.direction = Direction::below; c.rangeDb = -6; checkClose (c.offsetDb (-60.0), -6.0, 1e-9, "T5: cut below threshold");
            c.rangeDb = 6;                                  checkClose (c.offsetDb (-60.0),  6.0, 1e-9, "T5: boost below threshold");
            checkClose (c.offsetDb (0.0), 0.0, 1e-9, "T5: below-threshold band idle above threshold");
        }

        // No overshoot past the target. Lag is a causal detector's price
        // (the first sample's error at 0.1 ms is ~10 dB whatever the
        // implementation); overshooting the static target is not, and never
        // happens.
        for (double attack : { 0.1, 1.0, 10.0 })
        {
            Detector d;
            d.configure (attack, 100.0, false, fs);
            GainComputer c; c.thresholdDb = -20; c.ratio = 4; c.kneeDb = 0; c.rangeDb = -40;
            for (int i = 0; i < 20000; ++i) d.process (lo);

            const auto targetDb = c.offsetDb (-6.0);
            double prev = 0.0; bool monotone = true, beyond = false;
            for (int n = 0; n < (int) fs; ++n)
            {
                const auto off = c.offsetDb (20.0 * std::log10 (d.process (hi)));
                monotone = monotone && off <= prev + 1e-12;
                beyond = beyond || off < targetDb - 1e-9;
                prev = off;
            }
            check (monotone && ! beyond, "T5: gain approaches its target monotonically, never past it, attack " + std::to_string (attack));
        }

        // End to end: a dynamic bell settles on the static curve, measured on
        // the band's reported offset and on the audio.
        {
            const double rate = 48000.0;
            Settings s;
            auto& b = s.bands[0];
            b.enabled = true; b.shape = Shape::bell; b.frequencyHz = 1000.0; b.q = 2.0; b.gainDb = 0.0;
            b.dynamics.enabled = true; b.dynamics.thresholdDb = -20.0; b.dynamics.ratio = 4.0;
            b.dynamics.kneeDb = 0.0; b.dynamics.rangeDb = -24.0;
            b.dynamics.attackMs = 5.0; b.dynamics.releaseMs = 1000.0;

            auto e = makeEngine (rate, s);
            const auto n = (size_t) (2.0 * rate);
            std::vector<double> x (n);
            for (size_t i = 0; i < n; ++i) x[i] = hi * std::sin (2.0 * kPi * 1000.0 * (double) i / rate);
            double* ch[1] { x.data() };
            e.process (ch, 1, (int) n);

            const auto expected = -10.5;   // (1/4 - 1)(-6 + 20)
            checkClose (e.bandOffsetDb (0), expected, 0.05, "T5: steady-state offset on the static curve");
            checkClose (toDb (peakAbs (x, n - 4800, n) / hi), expected, 0.05, "T5: steady-state level in the audio");
            checkClose (e.currentGainReductionDb(), 10.5, 0.05, "T5: gain reduction reported for the meter");
        }

        // The same band pointed the other way, because the meter's figure is
        // signed and the sign is the only thing that tells an upward band from
        // a band doing nothing.
        //
        // `currentGainReductionDb` was `max (0, -offsetDb)` until 2026-09-15,
        // so this case reported a flat zero and the panel drew an empty bar
        // while the band was adding 10.5 dB. Nothing failed, because nothing
        // asked. The mirror of the block above, to the same tolerances: only
        // `rangeDb` changes, and every figure comes back with its sign turned
        // over.
        {
            const double rate = 48000.0;
            Settings s;
            auto& b = s.bands[0];
            b.enabled = true; b.shape = Shape::bell; b.frequencyHz = 1000.0; b.q = 2.0; b.gainDb = 0.0;
            b.dynamics.enabled = true; b.dynamics.thresholdDb = -20.0; b.dynamics.ratio = 4.0;
            b.dynamics.kneeDb = 0.0; b.dynamics.rangeDb = 24.0;
            b.dynamics.attackMs = 5.0; b.dynamics.releaseMs = 1000.0;

            auto e = makeEngine (rate, s);
            const auto n = (size_t) (2.0 * rate);
            std::vector<double> x (n);
            for (size_t i = 0; i < n; ++i) x[i] = hi * std::sin (2.0 * kPi * 1000.0 * (double) i / rate);
            double* ch[1] { x.data() };
            e.process (ch, 1, (int) n);

            const auto expected = 10.5;    // the same amount, added rather than taken
            checkClose (e.bandOffsetDb (0), expected, 0.05, "T5: an upward band settles on the static curve");
            checkClose (toDb (peakAbs (x, n - 4800, n) / hi), expected, 0.05, "T5: an upward band's level in the audio");

            // The absolute, not "is negative": a sign test would pass on any
            // wrong magnitude, which is the trap OptoDspTests was written
            // against.
            checkClose (e.currentGainReductionDb(), -10.5, 0.05,
                        "T5: the meter reports gain added as a negative figure");
        }
    }

    //==========================================================================
    // T6 -- mid/side.
    //==========================================================================
    void testMidSide()
    {
        const double fs = 48000.0;
        const Stereo in { pinkNoise (8192, -1.0), pinkNoise (8192, -1.0, 4242u) };

        auto nullDb = [] (const Stereo& a, const Stereo& b)
        {
            double worst = 0;
            for (size_t i = 0; i < a.l.size(); ++i)
                worst = std::max ({ worst, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]) });
            return toDb (worst);
        };

        // Encode -> decode through a band at 0 dB (its filter is the identity
        // with real poles) nulls: double path, then the float boundary.
        {
            Settings s;
            s.bands[0].enabled = true; s.bands[0].frequencyHz = 2000.0; s.bands[0].gainDb = 0.0;
            s.bands[0].placement = Placement::mid; s.bands[0].msAmount = 0.6;
            auto e = makeEngine (fs, s);
            checkAtMost (nullDb (render (e, in, 256), in), -140.0, "T6: M/S round trip nulls, double path");
            auto f = makeEngine (fs, s);
            checkAtMost (nullDb (render<float> (f, in, 256), in), -120.0, "T6: M/S round trip nulls, float boundary");
        }

        // A stereo band equals the same filter run on L and R independently,
        // in a plain direct form -- the identity that says the M/S internals
        // are right.
        {
            Settings s;
            auto& b = s.bands[0];
            b.enabled = true; b.shape = Shape::bell; b.frequencyHz = 700.0; b.q = 3.0; b.gainDb = 9.0;
            auto e = makeEngine (fs, s);
            const auto y = render (e, in, 256);

            const auto q = designMatched (Shape::bell, 700.0, 3.0, 9.0, fs);
            Stereo expect = in;
            for (auto* ch : { &expect.l, &expect.r })
            {
                double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                for (auto& v : *ch)
                {
                    const auto out = q.b0 * v + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
                    x2 = x1; x1 = v; y2 = y1; y1 = out; v = out;
                }
            }
            checkAtMost (nullDb (y, expect), -140.0, "T6: stereo band equals per-channel filtering");
        }

        // A side-only band does nothing to a perfectly correlated source.
        {
            Settings s;
            s.bands[0].enabled = true; s.bands[0].frequencyHz = 3000.0; s.bands[0].gainDb = 12.0;
            s.bands[0].placement = Placement::side; s.bands[0].msAmount = 1.0;
            auto e = makeEngine (fs, s);
            const Stereo mono { in.l, in.l };
            checkAtMost (nullDb (render (e, mono, 256), mono), -140.0, "T6: side band leaves a mono source alone");
        }

        // The blend sweep. Output is linear in the blend at every sample, so
        // with the two ends rendered separately the sweep's own blend value
        // can be read back, and it must be continuous, monotone and inside
        // [0, 1]. This is the check that catches interpolating the matrix
        // instead of the contributions (singular at 0.586).
        {
            Settings s;
            auto& b = s.bands[0];
            b.enabled = true; b.shape = Shape::bell; b.frequencyHz = 1500.0; b.q = 1.0; b.gainDb = 10.0;
            b.placement = Placement::mid;

            const auto n = (size_t) fs;
            const Stereo src { pinkNoise (n, -3.0, 11u), pinkNoise (n, -3.0, 12u) };

            b.msAmount = 0.0; auto e0 = makeEngine (fs, s); const auto y0 = render (e0, src, 256);
            b.msAmount = 1.0; auto e1 = makeEngine (fs, s); const auto y1 = render (e1, src, 256);

            b.msAmount = 0.0;
            auto es = makeEngine (fs, s);
            Stereo ys = src;
            for (size_t pos = 0; pos < n; pos += 256)
            {
                s.bands[0].msAmount = std::min (1.0, (double) pos / (double) n * 1.25);
                es.setSettings (s);
                double* ch[2] { ys.l.data() + pos, ys.r.data() + pos };
                es.process (ch, 2, (int) std::min ((size_t) 256, n - pos));
            }

            double prevBeta = 0.0, worstJump = 0.0, lowest = 0.0, highest = 0.0;
            bool monotone = true;
            for (size_t i = 0; i < n; ++i)
            {
                const auto d = y1.l[i] - y0.l[i];
                if (std::abs (d) < 1.0e-4) continue;
                const auto beta = (ys.l[i] - y0.l[i]) / d;
                worstJump = std::max (worstJump, std::abs (beta - prevBeta));
                monotone = monotone && beta >= prevBeta - 1.0e-9;
                lowest = std::min (lowest, beta); highest = std::max (highest, beta);
                prevBeta = beta;
            }

            checkAtMost (worstJump, 0.01, "T6: blend sweep is continuous");
            check (monotone, "T6: blend sweep is monotone");
            check (lowest >= -1.0e-9 && highest <= 1.0 + 1.0e-9, "T6: blend stays in [0, 1]");
            checkClose (prevBeta, 1.0, 1.0e-6, "T6: blend sweep arrives at the M/S end");
        }
    }

    //==========================================================================
    // T7 -- numerical robustness.
    //==========================================================================
    void testRobustness()
    {
        // reset() is a true reset: the same input after it gives the same bits.
        {
            const auto s = bandsConfig (3);
            const Stereo in { pinkNoise (6000, -3.0), pinkNoise (6000, -3.0, 3u) };
            auto e = makeEngine (48000.0, s);
            const auto first = render (e, in, 128);
            e.reset();
            const auto second = render (e, in, 128);
            check (std::memcmp (first.l.data(), second.l.data(), first.l.size() * sizeof (double)) == 0
                && std::memcmp (first.r.data(), second.r.data(), first.r.size() * sizeof (double)) == 0,
                   "T7: reset returns to a bit-identical initial state");
        }

        // Denormals, checked deterministically: after an impulse and seconds of
        // silence no state is subnormal. The spec times blocks instead, which
        // is a wall-clock test the harness is not allowed to contain.
        {
            auto s = bandsConfig (3);
            s.bands[0].frequencyHz = 20.0; s.bands[0].q = 16.0; s.bands[0].gainDb = 24.0;
            auto e = makeEngine (48000.0, s);
            std::vector<double> l (4096, 0.0), r (4096, 0.0);
            l[0] = r[0] = 1.0e-3;
            bool normal = true;
            for (int blockNo = 0; blockNo < 60; ++blockNo)
            {
                double* ch[2] { l.data(), r.data() };
                e.process (ch, 2, 4096);
                normal = normal && e.allStateNormal();
                std::fill (l.begin(), l.end(), 0.0); std::fill (r.begin(), r.end(), 0.0);
            }
            check (normal, "T7: no subnormal state through 5 s of silence");

            // And from the smallest thing a host can send: subnormal floats.
            // They arrive as normal doubles (double's range is far wider), so
            // the only way into a subnormal double is a long decay, which the
            // flush on the control cadence stops at 1e-30.
            auto t = makeEngine (48000.0, s);
            std::vector<float> fl (4096, 1.0e-40f), fr (4096, -1.0e-40f);
            float* fch[2] { fl.data(), fr.data() };
            t.process (fch, 2, 4096);
            check (t.allStateNormal(), "T7: no subnormal state from a subnormal float input");
        }

        // Low-frequency precision: a 20 Hz bell at 192 kHz meets the spec's
        // targets in every region.
        for (double q : kGridQ)
            for (double g : kGridG)
            {
                const auto e = ref::regionErrors (designMatched (Shape::bell, 20.0, q, g, 192000.0), Prototype::make (Shape::bell, 20.0, q, g), 192000.0);
                for (int r = 0; r < 3; ++r)
                    checkAtMost (e.worst[r], ref::kRegionTargetDb[r], "T7: 20 Hz @ 192 kHz, region " + std::to_string (r + 1) + ", " + cfg (Shape::bell, 20, q, g));
            }

        // A Nyquist square at full scale through 24 bands at extreme settings.
        {
            Settings s;
            for (int i = 0; i < 24; ++i)
            {
                auto& b = s.bands[(size_t) i];
                b.enabled = true;
                b.shape = (Shape) (i % 5);
                b.frequencyHz = i % 2 ? 0.499 * 96000.0 : 20.0 + 900.0 * i;
                b.q = i % 3 ? 40.0 : 0.1;
                b.gainDb = i % 2 ? 30.0 : -30.0;
                b.dynamics.enabled = i % 4 == 0;
                b.dynamics.rangeDb = 30.0; b.dynamics.direction = Direction::below;
            }
            auto e = makeEngine (96000.0, s);
            std::vector<double> l (96000), r (96000);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (i % 2 ? -1.0 : 1.0);
            double* ch[2] { l.data(), r.data() };
            e.process (ch, 2, (int) l.size());
            check (std::isfinite (peakAbs (l)) && std::isfinite (peakAbs (r)), "T7: Nyquist square stays finite");
            check (e.allCoefficientsStable(), "T7: Nyquist square leaves every coefficient set stable");
        }
    }

    //==========================================================================
    // Serial -- the chosen topology, held to the claims that chose it
    // (modules/deq/spec/topology-options.md).
    //==========================================================================
    void testSerial()
    {
        const double fs = 48000.0;
        check (Settings {}.topology == Topology::serial, "Serial: the default topology is serial");

        // What the curves show is what the audio does: the engine's measured
        // response is the product of its bands' designs -- their dB curves
        // added -- including a low cut under a low-shelf boost and two cuts
        // stacked a tone apart.
        {
            struct B { Shape shape; double hz, q, g; };
            const B chain[] = {
                { Shape::lowCut,    80.0,    0.707, 0.0 },
                { Shape::lowShelf,  100.0,   0.707, 6.0 },
                { Shape::bell,      1000.0,  2.0,  -9.0 },
                { Shape::bell,      1120.0,  2.0,  -9.0 },
                { Shape::highShelf, 10000.0, 0.707, 3.0 },
                { Shape::highCut,   18000.0, 0.707, 0.0 },
            };

            Settings s;
            std::vector<Biquad> designs;
            size_t n = (size_t) 1 << 16;

            for (size_t i = 0; i < std::size (chain); ++i)
            {
                auto& b = s.bands[i];
                b.enabled = true; b.shape = chain[i].shape; b.frequencyHz = chain[i].hz;
                b.q = chain[i].q; b.gainDb = chain[i].g;
                designs.push_back (designMatched (chain[i].shape, chain[i].hz, chain[i].q, chain[i].g, fs));
                n = std::max (n, ref::irLengthFor (designs.back()));
            }

            auto e = makeEngine (fs, s);
            std::vector<double> l (n, 0.0);
            l[0] = 1.0;
            double* ch[1] { l.data() };
            e.process (ch, 1, (int) n);

            std::vector<std::complex<double>> ir (l.begin(), l.end());
            ref::fft (ir);

            double worst = 0.0;
            for (size_t k = 1; k < n / 2; ++k)
            {
                const auto hz = (double) k * fs / (double) n;
                if (hz < 10.0 || hz > 0.499 * fs) continue;
                std::complex<double> h = 1.0;
                for (const auto& d : designs) h *= d.responseAt (2.0 * kPi * (double) k / (double) n);
                const auto expected = toDb (std::abs (h));
                if (expected < -120.0) continue;
                worst = std::max (worst, std::abs (toDb (std::abs (ir[k])) - expected));
            }
            checkAtMost (worst, 0.001, "Serial: the engine's response is the band curves added in dB");

            // And the two behaviours that decided it, read off the same render.
            const auto at = [&] (double hz)
            {
                const auto k = (size_t) std::lround (hz * (double) n / fs);
                return toDb (std::abs (ir[k]));
            };
            checkAtMost (at (20.0), -15.0, "Serial: the low cut still cuts under a +6 dB low shelf");
            checkClose (at (1060.0), toDb (std::abs (designs[2].responseAt (2 * kPi * 1060.0 / fs)))
                                   + toDb (std::abs (designs[3].responseAt (2 * kPi * 1060.0 / fs)))
                                   + toDb (std::abs (designs[1].responseAt (2 * kPi * 1060.0 / fs)))
                                   + toDb (std::abs (designs[4].responseAt (2 * kPi * 1060.0 / fs)))
                                   + toDb (std::abs (designs[0].responseAt (2 * kPi * 1060.0 / fs)))
                                   + toDb (std::abs (designs[5].responseAt (2 * kPi * 1060.0 / fs))),
                        0.01, "Serial: stacked cuts add");
        }

        // Static bands commute: the same bands in reverse order give the same
        // audio, to rounding. There is no band order for a user to get wrong.
        const Stereo in { pinkNoise (16384, -3.0, 21u), pinkNoise (16384, -3.0, 22u) };
        auto reversed = [] (Settings s, int count)
        {
            std::reverse (s.bands.begin(), s.bands.begin() + count);
            return s;
        };
        auto nullDb = [] (const Stereo& a, const Stereo& b)
        {
            double worst = 0;
            for (size_t i = 0; i < a.l.size(); ++i)
                worst = std::max ({ worst, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]) });
            return toDb (worst);
        };

        {
            auto s = bandsConfig (2);
            auto a = makeEngine (fs, s);
            auto b = makeEngine (fs, reversed (s, 24));
            checkAtMost (nullDb (render (a, in, 256), render (b, in, 256)), -200.0, "Serial: static bands are order-independent");
        }

        // Dynamic bands commute exactly while their gains are still -- every
        // detector hears the dry input, so the trajectories are identical in
        // either order -- which dynamics enabled with zero range proves...
        {
            auto s = bandsConfig (3);
            for (int i = 0; i < 24; ++i) s.bands[(size_t) i].dynamics.rangeDb = 0.0;
            auto a = makeEngine (fs, s);
            auto b = makeEngine (fs, reversed (s, 24));
            checkAtMost (nullDb (render (a, in, 256), render (b, in, 256)), -200.0, "Serial: dynamic bands with no movement are order-independent");
        }

        // ...but not while they move: two filters whose coefficients are
        // changing do not commute, in this EQ or any serial dynamic EQ. The
        // residual is only there during gain movement, and is held here.
        {
            auto s = bandsConfig (3);
            auto a = makeEngine (fs, s);
            auto b = makeEngine (fs, reversed (s, 24));
            const auto residual = nullDb (render (a, in, 256), render (b, in, 256));
            std::cout << "  serial, 24 dynamic bands, reversed order: residual " << residual << " dBFS\n";
            checkAtMost (residual, kDynamicOrderResidualDb, "Serial: dynamic bands are order-independent to within the measured residual");
        }
    }

    //==========================================================================
    // Topology: what each one does, written down so it cannot change quietly.
    //==========================================================================
    void testTopologyBehaviour()
    {
        const double fs = 48000.0;
        Settings s;
        for (int i = 0; i < 2; ++i)
        {
            s.bands[(size_t) i].enabled = true;
            s.bands[(size_t) i].frequencyHz = 1000.0;
            s.bands[(size_t) i].q = 2.0;
            s.bands[(size_t) i].gainDb = -24.0;
        }

        DspCore e;
        e.prepare (fs, 512, 2);

        s.topology = Topology::serial;
        e.setSettings (s);
        checkClose (toDb (std::abs (e.staticResponseAt (1000.0))), -48.0, 1.0e-6, "Topology: serial cuts stack");

        s.topology = Topology::parallel;
        e.setSettings (s);
        const auto h = e.staticResponseAt (1000.0);
        // Against the analogue figure: the digital bell carries a sliver of
        // phase at f0 that the formula does not, hence 0.01 dB rather than
        // exact. The point is the behaviour, not the fourth decimal.
        checkClose (toDb (std::abs (h)), toDb (std::abs (2.0 * std::pow (10.0, -24.0 / 20.0) - 1.0)), 0.01,
                    "Topology: parallel coincident cuts give 1 + 2(G - 1)");
        check (h.real() < 0.0, "Topology: ... and invert polarity there");
    }
}

int main()
{
    testLatency();
    testBlockSizeInvariance();
    testAccuracy();
    testAccuracyCeilings();
    testCoefficientIntegrity();
    testModulation();
    testDetector();
    testMidSide();
    testRobustness();
    testSerial();
    testTopologyBehaviour();
    testSoloAndTap();

    if (failures == 0)
        std::cout << "deq_dsp: all passed\n";

    return failures == 0 ? 0 : 1;
}
