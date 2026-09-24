/*
    BMO Linger's DSP, JUCE-free.

    **The early reflections are real since M2; the tail is not yet.** The
    first half of this file is everything that is true of the *frame* -- the
    adapter's unpacking, the latency contract, the tail arithmetic, the Size
    law, the tap table's shape and the Reverb EQ's design. The second half,
    `erEngineTests`, is 11 section 6's ER block as it applies to the engine:
    tap times and gains against the table in play, the density bridge, ER
    HI-CUT, termination, the level laws and the phasing trap, and the
    invariance, fuzz, click, allocation and tail-report items. Every ER item
    runs with the tail at -40.

    **No assertion reads a number out of the stand-in table.** `ErTable.cpp`
    is replaced wholesale by the table generator; every expected tap here is
    computed from `erTableFor (type)` at run time through 10 section 3's laws,
    so the tests follow whatever table is linked. The table's own audits --
    comb, flamming, mono gamma, lateral fraction -- are the generator's, and
    are not written here.

    Still to come with M3: T60, damping ratios, echo density and mixing time,
    modal density (which 10 section 4 expects Plate to *fail*), modulation,
    pre-delay and the tail's own phasing null.
*/

#include "modules/reverb/dsp/ErAudit.h"
#include "modules/reverb/dsp/ErTable.h"
#include "modules/reverb/dsp/ImageSource.h"
#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>

using namespace bmo::reverb;

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    bool near (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }

    /** Every parameter at its schema default, in `Index` order -- the array a
        host hands the adapter. Built from `specs()` rather than written out,
        so it cannot drift from the schema it is supposed to be. */
    std::vector<float> defaults()
    {
        std::vector<float> v;

        for (const auto& s : specs())
            v.push_back (s.def);

        return v;
    }
}

//==============================================================================
// Counting allocations. `process()` must allocate nothing, ever, and the only
// honest way to say so is to count: every global `operator new` in this
// executable goes through here, and the count is only armed around the calls
// under test.
namespace
{
    std::atomic<long> allocations { 0 };
    std::atomic<bool> countingAllocations { false };
}

void* operator new (std::size_t n)
{
    if (countingAllocations.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (n > 0 ? n : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    //== Driving the core ======================================================

    /** The ER on its own: the tail at -40 (off), the ER fader at 0 dB, MIX
        fully wet, OUTPUT at 0 dB, ER HI-CUT open -- and the type's own row for
        everything a type sets, so each type is heard as itself. 11 section 6
        runs every ER item with `verblevel` at -40, and this is that. */
    DspCore::Params erOnly (int type = room)
    {
        const auto& c = constantsFor (type);

        DspCore::Params p;
        p.type        = typeFor (type);
        p.sizeM       = c.sizeM;
        p.erDensity   = c.erDensity * 0.01f;
        p.erShape     = c.erShape;
        p.erSpreadMs  = c.erSpreadMs;
        p.erHiCutHz   = ErEngine::kHiCutOpenHz;
        p.erMode      = ErMode::taps;
        p.erVariation = 2;
        p.erLevelDb   = 0.0f;
        p.verbLevelDb = -40.0f;
        p.mix         = 1.0f;
        p.outputDb    = 0.0f;
        return p;
    }

    struct Stereo
    {
        std::vector<float> l, r;
    };

    void run (DspCore& core, Stereo& io, int block)
    {
        const auto n = io.l.size();

        for (size_t i = 0; i < n; i += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, n - i);
            float* ch[] { io.l.data() + i, io.r.data() + i };
            core.process (ch, 2, len);
        }
    }

    /** An impulse response, captured into vectors and never written anywhere
        (11 section 6). A unit impulse in both channels is a unit impulse in
        the mono sum the ER engine is fed. */
    Stereo impulse (const DspCore::Params& p, double rate, double seconds, int block = 256)
    {
        DspCore core;
        core.prepare (rate, block, 2);
        core.setParams (p);

        Stereo io;
        io.l.assign ((size_t) (rate * seconds), 0.0f);
        io.r = io.l;
        io.l[0] = io.r[0] = 1.0f;

        run (core, io, block);
        return io;
    }

    double energyOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        double e = 0.0;

        for (size_t i = from; i < std::min (to, x.size()); ++i)
            e += (double) x[i] * (double) x[i];

        return e;
    }

    double db (double ratio) { return 10.0 * std::log10 (std::max (ratio, 1.0e-300)); }

    /** The Size law as 10 section 3 states it, re-derived here from the table's
        own fields rather than read from the engine: t scales by S / S_ref and
        a by S_ref / S, and the window is held inside [5 ms, windowClampMs]. */
    float scaleFor (const ErTable& t, float sizeM)
    {
        const auto lo = 5.0f / t.windowMs;
        const auto hi = std::max (lo, t.windowClampMs / t.windowMs);
        return std::clamp (sizeM / kReferenceSizeM, lo, hi);
    }

    struct Expected
    {
        int   sample;
        float gain;
        float pole;    ///< the one-pole coefficient of the tap's band at this size and rate
    };

    /** The core taps of one channel of the table in play, where they should
        land and at what gain, at DENSITY 0 -- where only the core taps sound
        and the renormalisation is exactly unity, because the set it
        renormalises to is the set that is playing. The gain is the table's,
        divided by the Size factor, and faded by the end-of-cluster ramp,
        which is the engine's own law (`ErEngine::endTaper`) and the one piece
        of this that is not in the table. The pole is the tap's band filter,
        from the table's own cutoff law (`erBandCutoffHzAt`) through the
        engine's public one-pole design -- needed only to take a neighbour's
        tail out of a tap's reading. */
    std::vector<Expected> coreTaps (const ErChannel& c, const ErTable& t, float sizeM,
                                    double rate, int shift = 0, float gainScale = 1.0f)
    {
        const auto k = scaleFor (t, sizeM);
        std::vector<Expected> out;

        for (int i = 0; i < c.numTaps; ++i)
            if (c.taps[i].theta <= 0.0f)
            {
                const auto ms = c.taps[i].timeMs * k;
                const auto band = std::clamp (c.taps[i].band, 0, kErBands - 1);
                out.push_back ({ (int) std::lround ((double) ms * rate * 0.001) + shift,
                                 gainScale * c.taps[i].gain / k * ErEngine::endTaper (ms, t.windowMs * k),
                                 ErEngine::onePoleCoefficient (erBandCutoffHzAt (t, band, kReferenceSizeM * k), rate) });
            }

        return out;
    }

    struct TapScore
    {
        int checked = 0, timeMisses = 0, gainMisses = 0, skipped = 0, gains = 0;
        double worstDb = 0.0;
        int worstSamples = 0;
    };

    /** Peak-pick each expected tap in `ir` for its time, and read its gain.

        **How a tap's gain is isolated from its band filter and its
        neighbours.** Every filter between a tap and the output is a one-pole
        low-pass `y = (1 - a) x + a y`, unity at DC, so a tap of gain g at
        sample n puts g (1 - a) a^(m - n) into sample m >= n, and the sum of
        that from n to the end of the IR is g (1 - a^(N - n)). So the sum of
        the IR from a tap's own sample to the end, less what every *other*
        core tap still has to put out from there, is that tap's gain -- exact
        at any spacing and through any band, however dark. Plate's bands are
        dark enough that a tail still carries half a pulse 25 samples on,
        which is why a window between neighbours is not enough here. The
        other taps' gains in that subtraction are the table's, so an engine
        playing any tap wrong shows up in that tap and in its neighbours;
        nothing is fitted.

        Times are per tap, peak-picked between the midpoints to the
        neighbours, since a peak does not leak. */
    void scoreTaps (const std::vector<float>& ir, std::vector<Expected> taps, TapScore& score)
    {
        std::sort (taps.begin(), taps.end(), [] (auto& a, auto& b) { return a.sample < b.sample; });

        const auto end = (int) ir.size();

        // Suffix sums of the IR, in double.
        std::vector<double> after ((size_t) end + 1, 0.0);
        for (int j = end - 1; j >= 0; --j)
            after[(size_t) j] = after[(size_t) j + 1] + (double) ir[(size_t) j];

        // What tap j puts into samples [from, end).
        const auto remaining = [end] (const Expected& t, int from)
        {
            const auto start = std::max (from, t.sample);
            return (double) t.gain * (std::pow ((double) t.pole, (double) (start - t.sample))
                                    - std::pow ((double) t.pole, (double) (end - t.sample)));
        };

        for (size_t i = 0; i < taps.size(); ++i)
        {
            const auto n = taps[i].sample;

            if (n < 0 || n >= end)
            {
                ++score.skipped;
                continue;
            }

            // The time.
            const auto prev = i > 0 ? taps[i - 1].sample : n - 64;
            const auto next = i + 1 < taps.size() ? taps[i + 1].sample : n + 256;

            if (n - prev >= 4 && next - n >= 4)
            {
                const auto lo = std::max (0, (prev + n) / 2 + 1);
                const auto hi = std::min (end, (next + n) / 2 + 1);
                int peak = lo;

                for (int j = lo; j < hi; ++j)
                    if (std::abs (ir[(size_t) j]) > std::abs (ir[(size_t) peak]))
                        peak = j;

                ++score.checked;

                if (std::abs (peak - n) > 1)
                    ++score.timeMisses;

                score.worstSamples = std::max (score.worstSamples, std::abs (peak - n));
            }
            else
            {
                ++score.skipped;   // two peaks a few samples apart cannot be told apart
            }

            // The gain.
            if (std::abs (taps[i].gain) < 1.0e-4f)
                continue;

            double others = 0.0;
            for (size_t j = 0; j < taps.size(); ++j)
                if (j != i)
                    others += remaining (taps[j], n);

            const auto own = after[(size_t) n] - others;
            const auto expected = remaining (taps[i], n);
            const auto errDb = 20.0 * std::log10 (std::max (std::abs (own), 1.0e-30) / std::abs (expected));

            ++score.gains;

            if (std::abs (errDb) > 0.2)
                ++score.gainMisses;

            score.worstDb = std::max (score.worstDb, std::abs (errDb));
        }
    }

    /** |H(f)| of a real sequence, directly. */
    std::complex<double> dft (const std::vector<float>& x, double hz, double rate)
    {
        std::complex<double> sum {};
        const auto w = -2.0 * 3.14159265358979323846 * hz / rate;

        for (size_t n = 0; n < x.size(); ++n)
            sum += (double) x[n] * std::polar (1.0, w * (double) n);

        return sum;
    }

    /** A 1 ms window's energy, summed over an ensemble of fixed-seed noise
        runs. One noise run's 48-sample windows wander by a decibel from
        window to window on their own, which would swamp a 3 dB criterion; the
        sum over sixteen independent seeds is steady to a few tenths, and a
        real click -- a step in level or pattern that a crossfade or a dip
        should have spread over 30 ms -- still stands out of it by the whole
        step. `change` is applied at window `at`. */
    template <typename Change>
    std::vector<double> ensembleWindows (const DspCore::Params& start, Change change,
                                         int windows, int at, int seeds = 16)
    {
        constexpr double rate = 48000.0;
        constexpr int win = 48;

        std::vector<double> energy ((size_t) windows, 0.0);

        for (int s = 0; s < seeds; ++s)
        {
            DspCore core;
            core.prepare (rate, win, 2);
            core.setParams (start);

            unsigned int seed = 977u + 7919u * (unsigned int) s;
            Stereo io;
            io.l.resize ((size_t) win);
            io.r.resize ((size_t) win);

            for (int w = 0; w < windows; ++w)
            {
                if (w == at)
                {
                    auto p = start;
                    change (p);
                    core.setParams (p);
                }

                for (int i = 0; i < win; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    io.l[(size_t) i] = io.r[(size_t) i] = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                }

                run (core, io, win);
                energy[(size_t) w] += energyOf (io.l) + energyOf (io.r);
            }
        }

        return energy;
    }

    /** The largest 1 ms step, in dB, between neighbouring windows from `from`
        on, counting only windows above `floor`. The floor is there for the
        TYPE dip, which goes to silence on purpose: a raised cosine's rise
        out of zero is steep in decibels and smooth in the waveform, and the
        one thing a dip must not do is click at level -- which is what the
        floor leaves in view. */
    double worstStepDb (const std::vector<double>& e, int from, double floor)
    {
        double worst = 0.0;

        for (size_t w = (size_t) std::max (from, 1); w < e.size(); ++w)
            if (e[w] > floor && e[w - 1] > floor)
                worst = std::max (worst, std::abs (db (e[w] / e[w - 1])));

        return worst;
    }

    /** The end of the ER in samples: the last sample at which the Schroeder
        backward integral is still within 60 dB of the whole. */
    int minus60 (const std::vector<float>& l, const std::vector<float>& r)
    {
        double total = energyOf (l) + energyOf (r);
        double remaining = total;
        int last = 0;

        for (size_t i = 0; i < l.size(); ++i)
        {
            if (remaining >= total * 1.0e-6)
                last = (int) i;

            remaining -= (double) l[i] * l[i] + (double) r[i] * r[i];
        }

        return last;
    }

    //==========================================================================
    void erEngineTests()
    {
        constexpr double rate = 48000.0;

        //== ER taps: times to the sample, gains to 0.2 dB, against the table ==
        //
        // DENSITY minimum, hi-cut open, every type, every per-channel
        // VARIATION set, both channels, and three sizes -- the reference, half
        // of it, and twice it, where the table's own window clamp may bind.
        // Expected values are the table in play read through the Size law, so
        // nothing here pins a number from the stand-in table.
        {
            TapScore score;

            for (int type = 0; type < numTypes; ++type)
            {
                const auto& table = erTableFor (type);

                for (int v = 0; v < kErCombVariation; ++v)
                    for (const auto size : { kReferenceSizeM, kReferenceSizeM * 0.5f, kReferenceSizeM * 2.0f })
                    {
                        auto p = erOnly (type);
                        p.erDensity = 0.0f;
                        p.erVariation = v;
                        p.sizeM = size;

                        const auto ir = impulse (p, rate, (double) (table.windowClampMs + 20.0f) * 0.001);

                        scoreTaps (ir.l, coreTaps (table.variation[v].left,  table, size, rate), score);
                        scoreTaps (ir.r, coreTaps (table.variation[v].right, table, size, rate), score);
                    }
            }

            check (score.checked >= numTypes * 6 * 3 * 2 * 10,
                   "the tap check reached at least ten core taps per channel and setting");
            check (score.timeMisses == 0, "every core tap lands within 1 sample of the table's time under the Size law");
            check (score.gainMisses == 0, "every core tap's gain is the table's under the Size law, within 0.2 dB");

            std::cout << "  er taps: " << score.checked << " checked, " << score.skipped
                      << " skipped, " << score.gains << " gains read, worst " << score.worstDb << " dB, " << score.worstSamples << " samples\n";
        }

        //== Variation 6 is "mono null": the ER on the side and nowhere else ==
        //
        // The owner's reading (2026-09-23), and the value string's: with E the
        // table's variation-6 set, the ER bus is L = +E and R = -E, BMO
        // Dimension's side convention, so the mono sum of the module is
        // exactly twice the dry. `combDelayMs` and `combGain` are not used.
        {
            // An impulse and then noise; fully wet, the output is the ER bus.
            const auto signal = [&]
            {
                std::vector<float> x ((size_t) (0.3 * rate), 0.0f);
                x[0] = 1.0f;
                unsigned int seed = 5150u;

                for (size_t i = x.size() / 2; i < x.size(); ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    x[i] = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                }

                return x;
            };

            const auto drive = [&] (const DspCore::Params& p, int channels)
            {
                DspCore core;
                core.prepare (rate, 256, channels);
                core.setParams (p);

                Stereo io { signal(), signal() };

                if (channels == 2)
                {
                    run (core, io, 256);
                }
                else
                {
                    for (size_t i = 0; i < io.l.size(); i += 256)
                    {
                        float* ch[] { io.l.data() + i };
                        core.process (ch, 1, (int) std::min ((size_t) 256, io.l.size() - i));
                    }
                }

                return io;
            };

            // **L + R is exactly zero**: bit-exact, every sample, every type,
            // four densities (the bridge and every diffuser stage), all three
            // modes. And L is not silent, or this proves nothing.
            bool nulls = true, sounds = true;
            bool levelsMatch = true;
            double worstLevelDb = 0.0;

            for (int type = 0; type < numTypes; ++type)
                for (const auto mode : { ErMode::taps, ErMode::energy, ErMode::blend })
                    for (const auto density : { 0.0f, 0.3f, 0.7f, 1.0f })
                    {
                        auto p = erOnly (type);
                        p.erMode = mode;
                        p.erDensity = density;
                        p.erVariation = kErCombVariation;

                        const auto out = drive (p, 2);

                        for (size_t i = 0; i < out.l.size(); ++i)
                            nulls = nulls && out.l[i] + out.r[i] == 0.0f;

                        const auto energy = energyOf (out.l);
                        sounds = sounds && energy > 0.0;

                        // **Each side carries the ER at the level the other
                        // positions do**: against Variation 5 at the same
                        // settings, 0.2 dB, on the impulse response -- the
                        // noise half of the drive is a random quantity
                        // through two different velvet sequences. Both sets
                        // are renormalised to their own core energy, so this
                        // holds while the two carry the same energy, which the
                        // table half's sets are built to; the tap check below
                        // is the absolute.
                        //
                        // **Up to DENSITY 0.6 only.** Above it the diffuser
                        // is in, and what it does to a level depends on how a
                        // set's own pattern meets its 64 paths -- in Energy
                        // mode Var 5 and Var 6 are different velvet sequences
                        // and differ by a quarter of a decibel there. That is
                        // the same for every position, and the density sweep
                        // is where it is held.
                        if (density > ErEngine::kDiffuserStartDensity)
                            continue;

                        auto five = p;
                        five.erVariation = 5;
                        const auto six  = impulse (p, rate, 0.2);
                        const auto ref5 = impulse (five, rate, 0.2);
                        const auto levelDb = db (energyOf (six.l) / energyOf (ref5.l));

                        worstLevelDb = std::max (worstLevelDb, std::abs (levelDb));
                        levelsMatch = levelsMatch && std::abs (levelDb) <= 0.2;
                    }

            check (nulls, "Variation 6: L + R is exactly 0.0 at every sample, type, density and mode");
            check (sounds, "Variation 6: the side is not silent, so the null is not vacuous");
            check (levelsMatch, "Variation 6: each side carries the ER within 0.2 dB of Variation 5 at the same settings, DENSITY 0-60 %");
            std::cout << "  var 6: worst level against Var 5 " << worstLevelDb << " dB\n";

            // **And L is E itself**, tap for tap: the absolute behind the
            // level check. At DENSITY 0 with the hi-cut open, L's taps are the
            // variation-6 set's core taps under the Size law, and R = -L by
            // the null above.
            TapScore side;

            for (int type = 0; type < numTypes; ++type)
            {
                const auto& table = erTableFor (type);
                auto p = erOnly (type);
                p.erDensity = 0.0f;
                p.erVariation = kErCombVariation;

                const auto ir = impulse (p, rate, (double) (table.windowClampMs + 20.0f) * 0.001);
                scoreTaps (ir.l, coreTaps (table.variation[kErCombVariation].left, table, p.sizeM, rate), side);
            }

            check (side.checked > 0 && side.timeMisses == 0 && side.gainMisses == 0,
                   "Variation 6: L is the mono set E, tap for tap, times to a sample and gains to 0.2 dB");

            // **A mono instance puts out no ER at all at Variation 6**, and
            // what it does put out is finite: at MIX 100 % exactly silence,
            // at MIX 50 % exactly half the dry, sample for sample.
            bool monoSilent = true, monoDry = true, monoFinite = true;

            for (int type = 0; type < numTypes; ++type)
                for (const auto mode : { ErMode::taps, ErMode::energy, ErMode::blend })
                {
                    auto p = erOnly (type);
                    p.erMode = mode;
                    p.erDensity = 0.8f;
                    p.erVariation = kErCombVariation;

                    const auto wet = drive (p, 1);

                    for (const auto x : wet.l)
                    {
                        monoFinite = monoFinite && std::isfinite (x);
                        monoSilent = monoSilent && x == 0.0f;
                    }

                    p.mix = 0.5f;
                    const auto half = drive (p, 1);
                    const auto input = signal();

                    for (size_t i = 0; i < half.l.size(); ++i)
                    {
                        monoFinite = monoFinite && std::isfinite (half.l[i]);
                        monoDry = monoDry && half.l[i] == input[i] * 0.5f;
                    }
                }

            check (monoFinite, "Variation 6, mono instance: every sample is finite");
            check (monoSilent, "Variation 6, mono instance: at MIX 100 % the output is exactly silence -- no ER at all");
            check (monoDry, "Variation 6, mono instance: at MIX 50 % the output is exactly half the dry");
        }

        //== Density sweep: constant energy, no tap appearing, no click =======
        //
        // **Two ranges, and only the first meets 11 section 6's 0.2 dB.**
        // Up to DENSITY 0.6 the bridge is the tap weights alone, renormalised
        // on the energy the band filters actually put out, and it holds to a
        // millionth of a decibel on any table. Above 0.6 the diffuser fades
        // in, and a per-channel feed-forward diffuser can only hold the level
        // on average (ErEngine.cpp, kDiffuserMs): what it does to one table's
        // IR depends on how that table's taps interfere with its 64 paths.
        // A gain for the diffuser's own share, tabulated from its impulse
        // response in prepare(), was built and measured on 2026-09-23 and moved
        // no type by more than 0.01 dB: the drift is the table's pattern
        // meeting the paths, not the diffuser alone (testing note).
        //
        // Swept at 48, 96 and 192 kHz, every type. That range is held to
        // 0.3 dB, which is a guard against a gross error -- the DC-gain bug
        // this file caught ran to 0.39 -- and not the spec's figure.
        {
            double bridgeDb = 0.0, diffuserDb = 0.0;
            double perType[numTypes][3] {};
            const double rates[] { 48000.0, 96000.0, 192000.0 };

            for (int ri = 0; ri < 3; ++ri)
            for (int type = 0; type < numTypes; ++type)
            {
                const auto sweepRate = rates[ri];
                auto p = erOnly (type);
                p.erDensity = 0.0f;

                const auto seconds = (double) (erTableFor (type).windowClampMs + ErEngine::diffuserSpreadMs() + 30.0f) * 0.001;
                const auto ref = impulse (p, sweepRate, seconds);
                const auto e0l = energyOf (ref.l), e0r = energyOf (ref.r);

                for (int step = 1; step <= 20; ++step)
                {
                    p.erDensity = (float) step / 20.0f;
                    const auto ir = impulse (p, sweepRate, seconds);

                    const auto error = std::max (std::abs (db (energyOf (ir.l) / e0l)),
                                                 std::abs (db (energyOf (ir.r) / e0r)));

                    auto& worst = p.erDensity <= ErEngine::kDiffuserStartDensity ? bridgeDb : diffuserDb;
                    worst = std::max (worst, error);

                    if (p.erDensity > ErEngine::kDiffuserStartDensity)
                        perType[type][ri] = std::max (perType[type][ri], error);
                }
            }

            check (bridgeDb <= 0.2, "ER energy is constant across DENSITY 0..60 %, the tap bridge, within 0.2 dB per channel");
            check (diffuserDb <= 0.3, "ER energy stays within 0.3 dB across DENSITY 60..100 %, the diffuser's range");
            std::cout << "  density sweep: worst energy error " << bridgeDb << " dB over the bridge, "
                      << diffuserDb << " dB over the diffuser\n";

            for (int type = 0; type < numTypes; ++type)
                std::cout << "    " << kTypeNames[type] << ": worst over the diffuser " << perType[type][0]
                          << " dB at 48 kHz, " << perType[type][1] << " at 96, " << perType[type][2] << " at 192\n";
        }

        // **No tap appears at a non-zero level.** On the weights themselves,
        // because an IR smears them: DENSITY is driven along a slow ramp one
        // sample at a time, and every tap that goes from silent to sounding
        // must do so at a small fraction of where it ends up. A switch rather
        // than a ramp would bring a tap in at its whole level.
        {
            auto p = erOnly (room);
            p.erDensity = 0.0f;

            DspCore core;
            core.prepare (rate, 1, 2);
            core.setParams (p);

            const auto& er = core.erEngine();
            constexpr int steps = 24000;
            float previous[2][kErMaxTaps] {};
            float appeared[2][kErMaxTaps] {};

            Stereo io { { 0.0f }, { 0.0f } };

            for (int n = 0; n <= steps + 4800; ++n)
            {
                p.erDensity = std::min (1.0f, (float) n / (float) steps);
                core.setParams (p);
                run (core, io, 1);

                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < er.currentTapCount (ch); ++k)
                    {
                        const auto g = er.currentTapGain (ch, k);

                        if (n > 0 && previous[ch][k] == 0.0f && g != 0.0f && appeared[ch][k] == 0.0f)
                            appeared[ch][k] = std::abs (g);

                        previous[ch][k] = g;
                    }
            }

            double worstFraction = 0.0;
            int appearedCount = 0;

            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < er.currentTapCount (ch); ++k)
                    if (appeared[ch][k] > 0.0f)
                    {
                        ++appearedCount;
                        worstFraction = std::max (worstFraction,
                                                  (double) appeared[ch][k] / std::abs ((double) er.currentTapGain (ch, k)));
                    }

            check (appearedCount > 0, "the density ramp brought taps in, so the next check is not vacuous");
            check (worstFraction <= 0.01, "no tap appears at more than 1 % of its level: the bridge is a ramp, not a switch");
            std::cout << "  density: " << appearedCount << " taps appeared, the loudest at "
                      << 100.0 * worstFraction << " % of its final level\n";
        }

        // **No click while DENSITY sweeps the whole range in one second.**
        //
        // Not by the energy per millisecond, which the renormalisation holds
        // steady whether a tap ramps in or switches in -- a switched tap is
        // still a step in the waveform, and that is what a click is. So the
        // input is a 100 Hz sine, whose ER is a sum of 100 Hz sines and so
        // itself smooth, and the detector is the second difference: for a
        // smooth signal of amplitude A it is (2 pi 100 / 48000)^2 A, under
        // 2e-4 A, while a tap of gain g arriving in one sample puts a step of
        // g A into it. Held to 1 % of the ER's own peak.
        {
            auto p = erOnly (room);
            p.erDensity = 0.0f;

            DspCore core;
            core.prepare (rate, 48, 2);
            core.setParams (p);

            constexpr int windows = 1600;
            Stereo io { std::vector<float> (48), std::vector<float> (48) };
            std::vector<float> out;
            size_t n = 0;

            for (int w = 0; w < windows; ++w)
            {
                auto q = p;
                q.erDensity = std::clamp ((float) (w - 400) / 1000.0f, 0.0f, 1.0f);
                core.setParams (q);

                for (int i = 0; i < 48; ++i, ++n)
                    io.l[(size_t) i] = io.r[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * 100.0 * (double) n / rate);

                run (core, io, 48);
                out.insert (out.end(), io.l.begin(), io.l.end());
            }

            // The sine starts abruptly at sample 0, and its onset through the
            // taps is a legitimate edge; the ER has settled by 300 ms.
            const auto from = (size_t) (0.3 * rate);
            float peak = 0.0f, worst = 0.0f;

            for (size_t i = from; i < out.size(); ++i)
            {
                peak  = std::max (peak, std::abs (out[i]));
                worst = std::max (worst, std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]));
            }

            check (worst <= 0.01f * peak, "sweeping DENSITY does not click: the second difference stays under 1 % of the ER's peak");
            std::cout << "  density sweep: worst second difference " << worst / peak * 100.0f << " % of peak\n";
        }

        // At the top, the pulse rate clears 2000/s: local maxima of |h| over
        // the stretch of the IR that is within 60 dB of its peak.
        {
            double worstRate = 1.0e30;

            for (int type = 0; type < numTypes; ++type)
            {
                auto p = erOnly (type);
                p.erDensity = 1.0f;

                const auto ir = impulse (p, rate, (double) (erTableFor (type).windowClampMs + 40.0f) * 0.001);

                float peak = 0.0f;
                for (const auto x : ir.l)
                    peak = std::max (peak, std::abs (x));

                const auto floor = peak * 1.0e-3f;
                int first = -1, last = -1, pulses = 0;

                for (size_t i = 1; i + 1 < ir.l.size(); ++i)
                {
                    const auto a = std::abs (ir.l[i]);

                    if (a < floor)
                        continue;

                    if (first < 0)
                        first = (int) i;

                    last = (int) i;

                    if (a > std::abs (ir.l[i - 1]) && a >= std::abs (ir.l[i + 1]))
                        ++pulses;
                }

                const auto seconds = (double) std::max (1, last - first) / rate;
                worstRate = std::min (worstRate, (double) pulses / seconds);
            }

            check (worstRate >= 2000.0, "at DENSITY 100 % the ER clears 2000 pulses per second, every type");
            std::cout << "  density top: at least " << worstRate << " pulses/s\n";
        }

        //== ER HI-CUT: -3 dB where the knob says, and no tap moves ============
        //
        // The hi-cut is the last filter on the bus and the path is linear, so
        // the ratio of the ER's spectrum with the hi-cut at X to its spectrum
        // with the hi-cut open *is* the hi-cut's response, whatever the taps
        // and bands in front of it do. At the top of its range the hi-cut is
        // exactly a wire, which is what makes "open" a reference.
        {
            auto p = erOnly (room);
            const auto open = impulse (p, rate, 0.15);

            for (const auto corner : { 2000.0f, 4000.0f, 8000.0f, 16000.0f })
            {
                p.erHiCutHz = corner;
                const auto cut = impulse (p, rate, 0.15);

                const auto refMax = std::abs (dft (open.l, 1000.0, rate));
                double previousF = 0.0, previousR = 1.0, found = -1.0;

                for (double f = 0.4 * corner; f < std::min (2.0 * (double) corner, 0.49 * rate); f *= 1.004)
                {
                    const auto h0 = dft (open.l, f, rate);

                    if (std::abs (h0) < 1.0e-3 * refMax)
                        continue;

                    const auto ratio = std::norm (dft (cut.l, f, rate) / h0);

                    if (ratio <= 0.5 && previousF > 0.0 && previousR > 0.5)
                    {
                        found = previousF + (f - previousF) * (previousR - 0.5) / (previousR - ratio);
                        break;
                    }

                    previousF = f;
                    previousR = ratio;
                }

                check (found > 0.0 && std::abs (found - corner) <= 0.1 * corner,
                       "ER HI-CUT is -3 dB within 10 % of its setting");
                std::cout << "  er hi-cut " << corner << " Hz: -3 dB at " << found << " Hz\n";

                // No tap moves: the cross-correlation of the two IRs peaks at
                // lag 0, +-1.
                int bestLag = 0;
                double best = -1.0e30;

                for (int lag = -8; lag <= 8; ++lag)
                {
                    double c = 0.0;

                    for (size_t i = 8; i + 8 < open.l.size(); ++i)
                        c += (double) open.l[i] * cut.l[(size_t) ((int) i + lag)];

                    if (c > best)
                    {
                        best = c;
                        bestLag = lag;
                    }
                }

                check (std::abs (bestLag) <= 1, "ER HI-CUT moves no tap: cross-correlation peaks at lag 0 +-1");
            }
        }

        //== ER-only: the cluster terminates ====================================
        //
        // Energy after (span + 5 ms) is at least 60 dB below the whole. The
        // span is the window at this size -- the end-of-cluster ramp reaches
        // zero there, so nothing is played later -- plus the diffuser's spread
        // once any of it is in.
        {
            double worst = -1.0e300;

            for (int type = 0; type < numTypes; ++type)
                for (const auto density : { 0.5f, 1.0f })
                    for (const auto mode : { ErMode::taps, ErMode::energy, ErMode::blend })
                        for (const auto size : { 0.5f, kReferenceSizeM, 80.0f })
                        {
                            const auto& table = erTableFor (type);
                            auto p = erOnly (type);
                            p.erDensity = density;
                            p.erMode = mode;
                            p.sizeM = size;

                            const auto spanMs = table.windowMs * scaleFor (table, size)
                                              + (density > ErEngine::kDiffuserStartDensity ? ErEngine::diffuserSpreadMs() : 0.0f);

                            const auto ir = impulse (p, rate, (double) (spanMs + 60.0f) * 0.001);
                            const auto cut = (size_t) std::lround ((double) (spanMs + 5.0f) * rate * 0.001);

                            const auto total = energyOf (ir.l) + energyOf (ir.r);
                            const auto after = energyOf (ir.l, cut) + energyOf (ir.r, cut);

                            worst = std::max (worst, db (after / total));
                        }

            check (worst <= -60.0, "ER-only: energy after the span plus 5 ms is at least 60 dB down, every type, mode and size");
            std::cout << "  er-only: energy after span + 5 ms at worst " << worst << " dB\n";
        }

        //== Level laws =========================================================
        {
            auto p = erOnly (room);
            const auto e0 = [&]
            {
                const auto ir = impulse (p, rate, 0.15);
                return energyOf (ir.l) + energyOf (ir.r);
            };

            const auto reference = e0();
            bool exact = true;

            for (const auto level : { -6.0f, -12.0f, -24.0f })
            {
                p.erLevelDb = level;
                exact = exact && std::abs (db (e0() / reference) - (double) level) <= 0.1;
            }

            check (exact, "ER LEVEL at -6, -12 and -24 moves the IR energy by exactly that, within 0.1 dB");

            p.erLevelDb = -40.0f;
            check (db (e0() / reference) <= -100.0, "ER LEVEL at -40 is off: at least 100 dB down");
        }

        //== The phasing trap, and MIX's two ends ===============================
        {
            auto noise = [] (size_t n)
            {
                std::vector<float> x (n);
                unsigned int seed = 4242u;

                for (auto& s : x)
                {
                    seed = seed * 1664525u + 1013904223u;
                    s = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                }

                return x;
            };

            const auto dry = noise (24000);

            const auto through = [&] (const DspCore::Params& p)
            {
                DspCore core;
                core.prepare (rate, 256, 2);
                core.setParams (p);

                Stereo io { dry, dry };
                run (core, io, 256);
                return io;
            };

            // MIX 50 %, both wet faders off, pre-delay 40 ms: the output is
            // half the dry signal and nothing else. Were the dry path delayed,
            // or summed against a delayed copy of itself, this would not null.
            {
                auto p = erOnly (room);
                p.mix = 0.5f;
                p.erLevelDb = -40.0f;
                p.verbLevelDb = -40.0f;
                p.preDelayMs = 40.0f;

                const auto out = through (p);
                double residual = 0.0, reference = 0.0;

                for (size_t i = 0; i < dry.size(); ++i)
                {
                    const auto scaled = 0.5 * (double) dry[i];
                    residual  += std::pow ((double) out.l[i] - scaled, 2.0) + std::pow ((double) out.r[i] - scaled, 2.0);
                    reference += 2.0 * scaled * scaled;
                }

                check (db (residual / reference) <= -80.0,
                       "the phasing trap: MIX 50 % with the wet faders off nulls against half the dry to -80 dB");
            }

            {
                auto p = erOnly (room);
                p.mix = 0.0f;

                const auto out = through (p);
                check (out.l == dry && out.r == dry, "MIX 0 is exactly the dry signal, to the bit, with the ER at 0 dB");
            }

            {
                auto p = erOnly (room);
                p.mix = 1.0f;
                p.erLevelDb = -40.0f;

                const auto out = through (p);
                check (energyOf (out.l) + energyOf (out.r) == 0.0,
                       "MIX 100 % has no dry in it: with the ER off, the output is silence");
            }
        }

        //== Block sizes: bit-identical ========================================
        {
            auto a = erOnly (room);
            a.erDensity = 0.8f;
            a.erHiCutHz = 5000.0f;

            auto b = erOnly (hall);
            b.erDensity = 1.0f;
            b.erMode = ErMode::energy;
            b.erVariation = kErCombVariation;

            auto c = erOnly (ambience);
            c.erMode = ErMode::blend;
            c.mix = 0.35f;
            c.erLevelDb = -9.0f;
            c.outputDb = -3.0f;

            bool identical = true;

            for (const auto& p : { a, b, c })
            {
                const auto input = [&]
                {
                    Stereo io;
                    io.l.assign (30000, 0.0f);
                    io.r = io.l;
                    io.l[0] = 1.0f;
                    unsigned int seed = 99u;

                    for (size_t i = 12000; i < io.l.size(); ++i)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        io.l[i] = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                        io.r[i] = 0.5f * io.l[i];
                    }

                    return io;
                };

                Stereo reference = input();
                {
                    DspCore core;
                    core.prepare (rate, 512, 2);
                    core.setParams (p);
                    run (core, reference, 512);
                }

                for (const auto block : { 1, 16, 32, 64, 127, 2048 })
                {
                    Stereo io = input();
                    DspCore core;
                    core.prepare (rate, block, 2);
                    core.setParams (p);
                    run (core, io, block);

                    identical = identical && io.l == reference.l && io.r == reference.r;
                }
            }

            check (identical, "block sizes 1/16/32/64/127/512/2048 produce bit-identical output");

            // **And while DENSITY moves**, which is what the control grid is
            // for: the tap weights are recomputed every kControlInterval
            // samples counted from reset(), not from a block's start, so a
            // sweep lands on the same samples at every block size. The move
            // has to start on the same sample for all of them, so the engine
            // is primed at DENSITY 0 by a zero-length call and handed DENSITY
            // 100 % before the first sample: the smoother then carries it
            // across some thirty grid points, through the bridge and all
            // three diffuser stages.
            {
                auto p = erOnly (room);
                p.erDensity = 0.0f;

                const auto moving = [&] (int block)
                {
                    DspCore core;
                    core.prepare (rate, block, 2);
                    core.setParams (p);
                    core.erEngine().process (nullptr, nullptr, nullptr, 0);

                    auto q = p;
                    q.erDensity = 1.0f;
                    core.setParams (q);

                    Stereo io;
                    io.l.assign (9600, 0.0f);
                    unsigned int seed = 404u;

                    for (auto& x : io.l)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        x = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                    }

                    io.r = io.l;
                    run (core, io, block);
                    return io;
                };

                const auto reference = moving (512);
                bool same = true;

                for (const auto block : { 1, 16, 32, 64, 127, 2048 })
                {
                    const auto io = moving (block);
                    same = same && io.l == reference.l && io.r == reference.r;
                }

                check (same, "with DENSITY moving, block sizes 1/16/32/64/127/512/2048 produce bit-identical output");
            }
        }

        //== Sample rates: tap times in ms, and latency exactly 0 ===============
        {
            TapScore score;
            bool timesHold = true;
            bool zeroLatency = true;

            for (const auto r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            {
                const auto& table = erTableFor (room);
                auto p = erOnly (room);
                p.erDensity = 0.0f;

                const auto ir = impulse (p, r, (double) (table.windowClampMs + 20.0f) * 0.001, 512);
                const auto k = scaleFor (table, p.sizeM);

                // Peak-pick every core tap, then read its time back in ms.
                const auto& c = table.variation[2].left;

                for (int i = 0; i < c.numTaps; ++i)
                {
                    if (c.taps[i].theta > 0.0f || ErEngine::endTaper (c.taps[i].timeMs * k, table.windowMs * k) < 0.05f)
                        continue;

                    const auto ms = c.taps[i].timeMs * k;
                    const auto n = (int) std::lround ((double) ms * r * 0.001);
                    int peak = n;

                    // Searched over +-0.4 ms, under half the 0.9 ms the table
                    // keeps between taps, so a tap in the wrong place is found
                    // in the wrong place rather than missed.
                    const auto reach = (int) (0.4 * r * 0.001);

                    for (int j = n - reach; j <= n + reach; ++j)
                        if (j >= 0 && j < (int) ir.l.size() && std::abs (ir.l[(size_t) j]) > std::abs (ir.l[(size_t) peak]))
                            peak = j;

                    timesHold = timesHold && std::abs ((double) peak * 1000.0 / r - (double) ms) <= 0.1;
                }

                ReverbDsp dsp;
                dsp.prepare (r, 512, 2);
                const auto v = defaults();
                zeroLatency = zeroLatency && DspCore::latencySamples() == 0
                           && dsp.latencyForParams (v.data(), (int) v.size()) == 0;

                // And the dry path, measured: an impulse at MIX 0 comes out on
                // sample 0 at every rate.
                auto dryOnly = p;
                dryOnly.mix = 0.0f;
                const auto wire = impulse (dryOnly, r, 0.01);
                zeroLatency = zeroLatency && wire.l[0] == 1.0f && energyOf (wire.l, 1) == 0.0;
            }

            check (timesHold, "at 44.1 to 192 kHz every tap is at the table's time in ms, within 0.1 ms");
            check (zeroLatency, "latency is exactly 0 at every rate, reported and measured");
        }

        //== The diffuser's path delays are all distinct =======================
        //
        // What makes a stage energy-preserving on a pulse: if two of a pulse's
        // paths land on one sample they add or cancel rather than sit side by
        // side. Only one stage is ever partly in -- stage s fades while every
        // earlier stage is fully in and every later one fully out -- so the
        // paths that coexist are the earlier stages' sums with and without
        // stage s's four delays: 5, then 20, then 80 of them, at every rate.
        {
            bool distinct = true;

            for (const auto r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            {
                const auto d = [r] (int s, int l) { return ErEngine::diffuserDelaySamples (s, l, r); };
                const auto unique = [] (std::vector<int> v)
                {
                    std::sort (v.begin(), v.end());
                    return std::adjacent_find (v.begin(), v.end()) == v.end();
                };

                std::vector<int> first { 0 }, second, third;

                for (int a = 0; a < 4; ++a)
                {
                    first.push_back (d (0, a));
                    second.push_back (d (0, a));

                    for (int b = 0; b < 4; ++b)
                    {
                        second.push_back (d (0, a) + d (1, b));
                        third.push_back (d (0, a) + d (1, b));

                        for (int c = 0; c < 4; ++c)
                            third.push_back (d (0, a) + d (1, b) + d (2, c));
                    }
                }

                distinct = distinct && unique (first) && unique (second) && unique (third);
            }

            check (distinct, "the diffuser's coexisting paths land on different samples at every rate");
        }

        //== NaN and fuzz over the schema's corners, at every type ==============
        {
            bool finite = true;
            bool silent = true;
            unsigned int seed = 2026u;

            const auto pick = [&seed] (const auto& s)
            {
                seed = seed * 1664525u + 1013904223u;
                const auto which = (seed >> 16) % 3u;
                return which == 0u ? s.min : (which == 1u ? s.max : s.def);
            };

            for (int type = 0; type < numTypes; ++type)
            {
                ReverbDsp dsp;
                dsp.prepare (rate, 256, 2);

                std::vector<float> l (256), r (256);
                float* ch[] { l.data(), r.data() };

                for (int block = 0; block < 400; ++block)
                {
                    auto v = defaults();

                    for (size_t i = 0; i < v.size(); ++i)
                        v[i] = pick (specs()[i]);

                    v[Index::type] = (float) type;
                    dsp.setParams (v.data(), (int) v.size());

                    for (int i = 0; i < 256; ++i)
                    {
                        const auto n = block * 256 + i;
                        float x = 0.0f;

                        switch ((block / 50) % 4)
                        {
                            case 0:  x = (n / 50) % 2 == 0 ? 1.0f : -1.0f; break;   // +-1 square
                            case 1:  x = 1.0f; break;                               // DC step
                            case 2:  x = 1.0e-40f; break;                           // a denormal
                            default:
                                seed = seed * 1664525u + 1013904223u;
                                x = (float) (seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
                        }

                        l[(size_t) i] = x;
                        r[(size_t) i] = 0.5f * x;   // not -x: the ER hears the mono sum, and -x would feed it silence
                    }

                    dsp.process (ch, 2, 256);

                    for (int i = 0; i < 256; ++i)
                        finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                }

                dsp.reset();

                // **An audible setting before the silence is checked.** The
                // last random corner is as likely as not to have the ER fader
                // at -40 or MIX at 0, and a reset that left the delay line
                // full would then still pass. At the defaults the ER is on
                // and fully in the mix, so anything reset() leaves is heard.
                {
                    auto audible = defaults();
                    audible[Index::type] = (float) type;
                    audible[Index::erlevel] = 0.0f;
                    audible[Index::mix] = 100.0f;
                    audible[Index::erdensity] = 100.0f;
                    dsp.setParams (audible.data(), (int) audible.size());
                }

                for (int block = 0; block < 200; ++block)
                {
                    std::fill (l.begin(), l.end(), 0.0f);
                    std::fill (r.begin(), r.end(), 0.0f);
                    dsp.process (ch, 2, 256);

                    for (int i = 0; i < 256; ++i)
                        silent = silent && l[(size_t) i] == 0.0f && r[(size_t) i] == 0.0f;
                }
            }

            check (finite, "every sample is finite over square, DC, denormal and noise inputs at every type's schema corners");
            check (silent, "after reset(), zeros in give exactly zeros out");
        }

        //== SIZE and TYPE changes do not click ================================
        {
            // SIZE halves, which doubles every gain: a 6 dB step if it were a
            // switch, spread over the 30 ms crossfade instead.
            {
                auto p = erOnly (room);
                p.erDensity = 0.3f;

                const auto e = ensembleWindows (p, [] (DspCore::Params& q) { q.sizeM *= 0.5f; }, 400, 200);
                const auto step = worstStepDb (e, 150, 0.0);

                check (step <= 3.0, "a SIZE change does not click: no 1 ms energy step above 3 dB");
                std::cout << "  size change: worst 1 ms step " << step << " dB\n";
            }

            // TYPE changes, bringing a different size with it as a type
            // change does through the host: the wet bus dips to nothing and
            // the table is swapped at the bottom.
            {
                auto p = erOnly (room);
                p.erDensity = 0.3f;

                const auto e = ensembleWindows (p, [] (DspCore::Params& q)
                {
                    q.type = Type::chamber;
                    q.sizeM *= 0.5f;
                }, 400, 200);

                double steady = 0.0, after = 0.0;
                for (int w = 150; w < 200; ++w)
                {
                    steady += e[(size_t) w] / 50.0;
                    after  += e[(size_t) w + 200] / 50.0;
                }

                // The floor is 10 dB under the louder of the two steady
                // levels, because the dip climbs out of silence *to* the new
                // level, and it is at the new level that a click would be.
                const auto step = worstStepDb (e, 150, 0.1 * std::max (steady, after));
                const auto bottom = *std::min_element (e.begin() + 200, e.begin() + 240);

                check (step <= 3.0, "a TYPE change does not click at level: no 1 ms step above 3 dB outside the dip");
                check (db (bottom / steady) <= -20.0, "a TYPE change dips the wet bus, and the swap is at the bottom of it");
                std::cout << "  type change: worst 1 ms step at level " << step << " dB, dip bottom "
                          << db (bottom / steady) << " dB\n";
            }

            // **And in the waveform.** A 1 ms energy criterion cannot see a
            // fall to silence in one sample -- the window it lands in is below
            // the dip's floor -- and that is a click. So both changes are made
            // again on a 100 Hz sine and held to the density sweep's
            // second-difference detector: 1 % of the ER's peak, against about
            // 0.02 % for a smooth 100 Hz signal.
            for (const auto typeToo : { false, true })
            {
                auto p = erOnly (room);
                p.erDensity = 0.3f;

                DspCore core;
                core.prepare (rate, 48, 2);
                core.setParams (p);

                Stereo io { std::vector<float> (48), std::vector<float> (48) };
                std::vector<float> out;
                size_t n = 0;

                for (int w = 0; w < 700; ++w)
                {
                    if (w == 400)
                    {
                        auto q = p;
                        q.sizeM *= 0.5f;

                        if (typeToo)
                            q.type = Type::chamber;

                        core.setParams (q);
                    }

                    for (int i = 0; i < 48; ++i, ++n)
                        io.l[(size_t) i] = io.r[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * 100.0 * (double) n / rate);

                    run (core, io, 48);
                    out.insert (out.end(), io.l.begin(), io.l.end());
                }

                float peak = 0.0f, worst = 0.0f;

                for (size_t i = (size_t) (0.3 * rate); i < out.size(); ++i)
                {
                    peak  = std::max (peak, std::abs (out[i]));
                    worst = std::max (worst, std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]));
                }

                check (worst <= 0.01f * peak, typeToo ? "a TYPE change does not click in the waveform"
                                                      : "a SIZE change does not click in the waveform");
                std::cout << (typeToo ? "  type" : "  size") << " change: worst second difference "
                          << worst / peak * 100.0f << " % of peak\n";
            }
        }

        //== Zero allocation in process(), with every parameter moving =========
        {
            ReverbDsp dsp;
            dsp.prepare (rate, 64, 2);

            std::vector<float> l (64), r (64);
            float* ch[] { l.data(), r.data() };
            unsigned int seed = 7u;

            const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); };

            auto v = defaults();
            allocations = 0;
            long observed = 0;

            for (int block = 0; block < 1500; ++block)
            {
                // Every parameter, every eighth block, somewhere in its range;
                // choices on their detents. Between moves the smoothers,
                // crossfades and dips are running.
                if (block % 8 == 0)
                    for (size_t i = 0; i < v.size(); ++i)
                    {
                        const auto& s = specs()[i];
                        v[i] = s.min + next() * (s.max - s.min);

                        if (i == (size_t) Index::type || i == (size_t) Index::ermode
                            || i == (size_t) Index::eqfilter || i == (size_t) Index::ervariation)
                            v[i] = std::round (v[i]);
                    }

                for (int i = 0; i < 64; ++i)
                    l[(size_t) i] = r[(size_t) i] = next() - 0.5f;

                countingAllocations = true;
                dsp.setParams (v.data(), (int) v.size());
                dsp.process (ch, 2, 64);
                countingAllocations = false;
            }

            observed = allocations.load();
            check (observed == 0, "process() and setParams() allocate nothing while every parameter moves");
        }

        //== The reported tail covers the ER ====================================
        //
        // `DspCore::tailSecondsFor` is not changed here and neither is
        // TailTests; what is asserted is that the figure it already reports is
        // at least the ER-only -60 dB time the engine now actually produces,
        // at the shortest DECAY, every type, mode, size and the density ends.
        {
            bool covered = true;
            double margin = 1.0e30;

            for (int type = 0; type < numTypes; ++type)
                for (const auto mode : { ErMode::taps, ErMode::energy })
                    for (const auto density : { 0.0f, 1.0f })
                        for (const auto size : { 0.5f, kReferenceSizeM, 80.0f })
                        {
                            auto p = erOnly (type);
                            p.erMode = mode;
                            p.erDensity = density;
                            p.sizeM = size;
                            p.decaySeconds = 0.1f;
                            p.dampLo = p.dampHi = 0.1f;

                            const auto ir = impulse (p, rate, 0.5);
                            const auto measured = (double) minus60 (ir.l, ir.r) / rate;
                            const auto reported = (double) DspCore::tailSecondsFor (p);

                            covered = covered && reported >= measured;
                            margin = std::min (margin, reported - measured);
                        }

            check (covered, "the reported tail is at least the measured ER-only -60 dB time, every type, mode and size");
            std::cout << "  tail: smallest margin of the report over the ER " << margin * 1000.0 << " ms\n";
        }
    }
}

int main()
{
    //== The schema and the adapter agree about length ========================
    {
        check (specs().size() == (size_t) Index::count, "the Index enum matches specs()");
        check (specs().size() == 30, "thirty parameters");
    }

    //== The adapter unpacks every field, and unpacks it correctly ============
    //
    // **This is the test that catches a transposed pair**, which is the one
    // mistake a thirty-field unpack invites and the one that no amount of
    // listening would localise. Every parameter is set to a value distinct
    // from every other, and every field of Params is read back.
    //
    // The type is **Plate** rather than an arbitrary one, and that matters
    // since the 2026-09-21 trim: six of `Params`' fields no longer come from
    // the array, and Plate's row differs from Room's on the ones it can. The
    // block after this one is what actually pins them.
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        auto v = defaults();

        v[Index::type]        = (float) plate;
        v[Index::size]        = 33.0f;
        v[Index::predelay]    = 72.0f;
        v[Index::decay]       = 4.25f;
        v[Index::feed]        = 40.0f;
        v[Index::damplo]      = 1.55f;
        v[Index::damphi]      = 0.65f;
        v[Index::eqfilter]    = (float) eqFilterHiCut;
        v[Index::eqlofreq]    = 140.0f;
        v[Index::eqlo]        = -6.0f;
        v[Index::eqloq]       = 1.35f;
        v[Index::eqmidfreq]   = 2600.0f;
        v[Index::eqmid]       = -7.5f;
        v[Index::eqmidq]      = 3.25f;
        v[Index::eqhifreq]    = 1400.0f;
        v[Index::eqhi]        = 4.5f;
        v[Index::eqhiq]       = 0.45f;
        v[Index::ermode]      = (float) energy;
        v[Index::erdensity]   = 82.0f;
        v[Index::erspread]    = 125.0f;
        v[Index::erhicut]     = 4500.0f;
        v[Index::ervariation] = 5.0f;
        v[Index::moddepth]    = 0.55f;
        v[Index::modrate]     = 0.90f;
        v[Index::width]       = 145.0f;
        v[Index::inhicut]     = 9000.0f;
        v[Index::erlevel]     = -18.5f;
        v[Index::verblevel]   = -3.5f;
        v[Index::mix]         = 45.0f;
        v[Index::output]      = -7.5f;

        dsp.setParams (v.data(), (int) v.size());

        const auto& p = dsp.getCore().getParams();

        check (p.type == Type::plate, "type");
        check (near (p.sizeM, 33.0f), "size");
        check (near (p.preDelayMs, 72.0f), "pre-delay");
        check (near (p.decaySeconds, 4.25f), "decay");
        check (near (p.dampLo, 1.55f), "low multiplier");
        check (near (p.dampHi, 0.65f), "high multiplier");
        check (near (p.eqLoFreqHz, 140.0f), "eq low freq");
        check (near (p.eqLoDb, -6.0f), "eq low");
        check (near (p.eqHiFreqHz, 1400.0f), "eq high freq");
        check (near (p.eqHiDb, 4.5f), "eq high");
        // **A middle position, not an end one.** `eqfilter` crossed the
        // adapter as `> 0.5f` while it was a bool, and that line would still
        // compile against the choice -- it would read Lo Cut, Hi Cut and
        // Bandpass all as "on". Hi Cut is index 2, so this fails on anything
        // that still treats the lane as a switch.
        check (p.eqFilter == EqFilter::hiCut, "eq filter");
        check (near (p.eqLoQ, 1.35f), "eq low q");
        check (near (p.eqMidFreqHz, 2600.0f), "eq mid freq");
        check (near (p.eqMidDb, -7.5f), "eq mid");
        check (near (p.eqMidQ, 3.25f), "eq mid q");
        check (near (p.eqHiQ, 0.45f), "eq high q");
        check (p.erMode == ErMode::energy, "er mode");
        check (near (p.erSpreadMs, 125.0f), "er spread");
        check (near (p.erHiCutHz, 4500.0f), "er hi-cut");
        check (p.erVariation == 5, "variation");
        check (near (p.modDepthMs, 0.55f), "mod depth");
        check (near (p.modRateHz, 0.90f), "mod rate");
        check (near (p.inHiCutHz, 9000.0f), "in hi-cut");
        check (near (p.erLevelDb, -18.5f), "er level");
        check (near (p.verbLevelDb, -3.5f), "reverb level");
        check (near (p.outputDb, -7.5f), "output");

        // **The conversions, which are the only places a host value is not
        // already an engine value.** They are in the adapter and nowhere else,
        // which is the thing worth pinning: a percentage that reached the
        // engine as 82 instead of 0.82 would be a hundredfold error in a
        // control that looks fine on the panel.
        check (near (p.feed, 0.40f), "source arrives as 0..1, not as per cent");
        check (near (p.erDensity, 0.82f), "density arrives as 0..1, not as per cent");
        check (near (p.mix, 0.45f), "mix arrives as 0..1, not as per cent");
        check (near (p.width, 1.45f), "width arrives as a 0..2 M/S gain, not as per cent");
    }

    //== The six fields with no host lane, and where they come from instead ===
    //
    // The 2026-09-21 control-set trim took ATTACK, DECAY SHAPE, ER SHAPE and
    // the two damping knees off the schema and into `TypeConstants`, and made
    // LINK ER a fixed constant. **So six of `Params`' fields are no longer
    // reachable from the array the adapter is handed**, and nothing above this
    // point would notice if they were wired to the wrong row, to Room's row
    // always, or to nothing at all.
    //
    // Asserted against the type's own namespace, per type, rather than against
    // "it changed": a check that only compared two types would pass on an
    // adapter that read the row one index off.
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        struct Row { int detent; float decayShape, attack, dampLo, dampHi, erShape; };

        const Row rows[] {
            { room,     roomDefaults::kDecayShape,     roomDefaults::kAttack,
                        roomDefaults::kDampLoFreqHz,   roomDefaults::kDampHiFreqHz,
                        roomDefaults::kErShape },
            { chamber,  chamberDefaults::kDecayShape,   chamberDefaults::kAttack,
                        chamberDefaults::kDampLoFreqHz, chamberDefaults::kDampHiFreqHz,
                        chamberDefaults::kErShape },
            { hall,     hallDefaults::kDecayShape,     hallDefaults::kAttack,
                        hallDefaults::kDampLoFreqHz,   hallDefaults::kDampHiFreqHz,
                        hallDefaults::kErShape },
            { cavern,   cavernDefaults::kDecayShape,    cavernDefaults::kAttack,
                        cavernDefaults::kDampLoFreqHz,  cavernDefaults::kDampHiFreqHz,
                        cavernDefaults::kErShape },
            { plate,    plateDefaults::kDecayShape,     plateDefaults::kAttack,
                        plateDefaults::kDampLoFreqHz,   plateDefaults::kDampHiFreqHz,
                        plateDefaults::kErShape },
            { ambience, ambienceDefaults::kDecayShape,  ambienceDefaults::kAttack,
                        ambienceDefaults::kDampLoFreqHz, ambienceDefaults::kDampHiFreqHz,
                        ambienceDefaults::kErShape },
        };

        for (const auto& row : rows)
        {
            auto v = defaults();
            v[Index::type] = (float) row.detent;
            dsp.setParams (v.data(), (int) v.size());

            const auto& p = dsp.getCore().getParams();
            const std::string who { kTypeNames[row.detent] };

            check (near (p.decayShape, row.decayShape),
                   ("decay shape is " + who + "'s constant").c_str());
            // Per cent on the row, 0..1 at the engine -- the same conversion
            // the knob used to go through, still in the adapter and still in
            // one place.
            check (near (p.attack, row.attack * 0.01f),
                   ("attack is " + who + "'s constant, as 0..1").c_str());
            check (near (p.dampLoFreqHz, row.dampLo),
                   ("the low knee is " + who + "'s constant").c_str());
            check (near (p.dampHiFreqHz, row.dampHi),
                   ("the high knee is " + who + "'s constant").c_str());
            check (near (p.erShape, row.erShape),
                   ("er shape is " + who + "'s constant").c_str());

            // Off, at every type. There is no value of anything that turns it
            // on, which is the point of cutting it.
            check (! p.linkEr, ("link er is off on " + who).c_str());
        }

        // **And the table is not flat**, or the loop above would pass against
        // an adapter that ignored the type entirely and stamped Room. Four of
        // the five differ across types; DECAY SHAPE deliberately does not --
        // every row is 3.50, linear, and params.h says so -- so it is asserted
        // as a constant rather than as a difference.
        check (! near (plateDefaults::kAttack, roomDefaults::kAttack),
               "Plate's attack differs from Room's, so the per-type read is not vacuous");
        check (! near (cavernDefaults::kDampHiFreqHz, roomDefaults::kDampHiFreqHz),
               "Cavern's high knee differs from Room's");
        check (! near (plateDefaults::kErShape, roomDefaults::kErShape),
               "Plate's ER shape differs from Room's");
        check (! near (cavernDefaults::kDampLoFreqHz, roomDefaults::kDampLoFreqHz),
               "Cavern's low knee differs from Room's");

        for (int t = 0; t < numTypes; ++t)
            check (near (constantsFor (t).decayShape, roomDefaults::kDecayShape),
                   "every type ships DECAY SHAPE linear -- a reverb should not arrive gated");
    }

    //== A short array is refused rather than read past ======================
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        auto v = defaults();
        v[Index::size] = 55.0f;
        dsp.setParams (v.data(), (int) v.size());

        // One short: the adapter must leave what it had rather than unpack a
        // partial array. A rack slot with fewer lanes than a module has
        // parameters is a real case (BMO DEQ has 159), so this is not
        // hypothetical defensiveness.
        dsp.setParams (v.data(), Index::count - 1);
        check (near (dsp.getCore().getParams().sizeM, 55.0f),
               "a short parameter array is refused, not partially unpacked");
    }

    //== Every detent maps to its own enumerator ==============================
    //
    // Index order is frozen with the choice lists in params.h, and an
    // off-by-one here would silently make every saved session select the
    // neighbouring type.
    {
        check (typeFor (0) == Type::room, "detent 0 is Room");
        check (typeFor (1) == Type::chamber, "detent 1 is Chamber");
        check (typeFor (2) == Type::hall, "detent 2 is Hall");
        // Cavern since 2026-09-21; the ordinal is unchanged, which is the whole
        // reason index 3 was renamed rather than cut (kTypeNames).
        check (typeFor (3) == Type::cavern, "detent 3 is Cavern");
        check (typeFor (4) == Type::plate, "detent 4 is Plate");
        check (typeFor (5) == Type::ambience, "detent 5 is Ambience");
        check (typeFor (-1) == Type::room && typeFor (99) == Type::room,
               "an out-of-range detent falls back to Room rather than reading off the end");

        check (erModeFor (0) == ErMode::taps, "er detent 0 is Taps");
        check (erModeFor (1) == ErMode::energy, "er detent 1 is Energy");
        check (erModeFor (2) == ErMode::blend, "er detent 2 is Blend");
        check (erModeFor (7) == ErMode::taps, "an out-of-range er detent falls back to Taps");
    }

    //== The per-type table, before anything has a chance to apply it =========
    //
    // What a type *stamps* is asserted through a real processor in
    // tests/plugin/ReverbTests.cpp, because the mechanism is a host-side one.
    // What can be asserted here, with no JUCE, is that the table it stamps
    // from is well formed -- which is the half that would still be wrong if
    // the mechanism were perfect.
    //
    // Room's row is real. **The other five are CALIBRATE placeholders**, so
    // none of their values is pinned here: pinning a placeholder makes it a
    // decision, which is precisely what the markers in params.h say it is not.
    // What is pinned is the shape, the reachability of every value, and the
    // one structural guarantee the re-entrancy argument rests on.
    {
        check ((int) (sizeof (kTypeConstants) / sizeof (TypeConstants)) == numTypes,
               "there is one constant row per type");

        for (int t = 0; t < numTypes; ++t)
        {
            const auto settings = typeSettings (t);

            // Nine since the 2026-09-21 trim: it was ten, and ER SHAPE lost
            // the parameter it was being written onto. The other four fields
            // the trim added are per-type too and are likewise unwritable, so
            // the row is fourteen wide and nine of it is a `Setting` list.
            check (settings.size() == 9,
                   "a type writes nine parameters -- the other five of its row have no host lane");

            for (const auto& s : settings)
            {
                const auto index = indexOfParam (specs(), s.id);

                if (index < 0)
                {
                    check (false, "a type writes a parameter that is not in the schema");
                    continue;
                }

                const auto& spec = specs()[(size_t) index];

                // **Reachable, and reachable exactly.** A constant outside its
                // parameter's range would be silently clamped, and one off the
                // step grid silently snapped -- so selecting a type would set
                // something other than the table says, and no assertion about
                // the table would notice. `clampReal` is the same arithmetic
                // the parameter itself applies.
                check (near (spec.clampReal (s.value), s.value),
                       "a type's constant must survive its own parameter's range and step");

                // The structural half of "a type change cannot recurse": the
                // list simply does not contain the parameter that triggers it.
                check (std::string (s.id) != std::string (kType),
                       "a type must not write 'type'");
            }
        }

        // Room's row is `roomDefaults` itself rather than a copy of it, which
        // is what makes "Room's defaults are Room's constants" true by
        // construction. Asserted against the schema, which is the thing that
        // would have to be edited to break it.
        for (const auto& s : typeSettings (room))
        {
            const auto index = indexOfParam (specs(), s.id);

            if (index >= 0)
                check (near (specs()[(size_t) index].def, s.value),
                       "Room's constant is the parameter's own default");
        }

        // **The five fields a `Setting` cannot reach, named.** Without this
        // the count above is the only thing standing between the schema and a
        // sixth field quietly going missing from `paramsFrom` -- and a field
        // the adapter forgot would read as Room's constant for every type,
        // which is exactly the failure that sounds like nothing being wrong.
        for (const auto* id : { "ershape", "decayshape", "attack",
                                "damplofreq", "damphifreq", "prelink" })
            check (indexOfParam (specs(), id) < 0,
                   "the trim's six are not parameters any more");
    }

    //== Latency: zero, everywhere, permanently ==============================
    //
    // Asserted over the whole schema rather than at the default, because there
    // is no parameter that *could* move it and the point is to notice the day
    // one arrives. Also asserted by impulse -- the placeholder is a wire, so
    // an impulse in at sample 0 must come out at sample 0.
    {
        ReverbDsp dsp;
        const auto v = defaults();

        check (dsp.latencyForParams (v.data(), (int) v.size()) == 0,
               "zero latency at the defaults");

        auto swept = v;
        for (size_t i = 0; i < swept.size(); ++i)
            swept[i] = specs()[i].max;

        check (dsp.latencyForParams (swept.data(), (int) swept.size()) == 0,
               "zero latency with every parameter at its maximum");

        for (size_t i = 0; i < swept.size(); ++i)
            swept[i] = specs()[i].min;

        check (dsp.latencyForParams (swept.data(), (int) swept.size()) == 0,
               "zero latency with every parameter at its minimum");
    }

    //== MIX 0 is a wire ======================================================
    //
    // The pass-through this block used to assert is gone -- the early
    // reflections are real since M2 -- but the dry path is still a wire at
    // MIX 0, to the bit, which is the half of it that has to survive.
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        auto v = defaults();
        v[Index::mix] = 0.0f;
        dsp.setParams (v.data(), (int) v.size());

        constexpr int n = 512;
        std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        float* channels[] { left.data(), right.data() };
        dsp.process (channels, 2, n);

        bool unchanged = left[0] == 1.0f && right[0] == 1.0f;

        for (int i = 1; i < n; ++i)
            unchanged = unchanged && left[(size_t) i] == 0.0f && right[(size_t) i] == 0.0f;

        check (unchanged, "at MIX 0 the module passes audio through untouched");
    }

    //== The Size law ========================================================
    //
    // t_k(S) = t_k,ref * S / S_ref, with gains as 1/d. Scaling the times while
    // keeping the pattern is what preserves a room's identity, and it is the
    // one piece of the ER generator that exists today -- because the panel
    // needs it.
    {
        const auto& first = kReferenceTaps[0];

        check (near (tapTimeMsAt (first, kReferenceSizeM), first.timeMs),
               "at the reference size a tap is at its tabulated time");
        check (near (tapTimeMsAt (first, kReferenceSizeM * 2.0f), first.timeMs * 2.0f),
               "twice the size is twice the time");
        check (near (tapGainAt (first, kReferenceSizeM * 2.0f), first.gain * 0.5f),
               "twice the size is half the gain, which is the 1/d law");

        // The pattern is preserved, not merely the endpoints: every ratio
        // between two taps is the same at any size. That is the property that
        // makes Size a room control rather than a delay control.
        bool ratiosHold = true;

        for (int i = 1; i < kNumReferenceTaps; ++i)
        {
            const auto a = tapTimeMsAt (kReferenceTaps[i], 5.0f) / tapTimeMsAt (kReferenceTaps[0], 5.0f);
            const auto b = tapTimeMsAt (kReferenceTaps[i], 60.0f) / tapTimeMsAt (kReferenceTaps[0], 60.0f);
            ratiosHold = ratiosHold && near (a, b, 1.0e-3f);
        }

        check (ratiosHold, "the tap pattern is preserved at every size");
        check (near (erSpanMsAt (kReferenceSizeM),
                     kReferenceTaps[kNumReferenceTaps - 1].timeMs),
               "the ER span is the last tap's time");
    }

    //== The tap table's shape ===============================================
    //
    // **The numbers in the table are a placeholder and none of 11 section 6's
    // rules is asserted against them** -- not the 0.9 ms minimum separation,
    // not the 2 % gap rule, not the Kuttruff level ceiling, not the flamming
    // rules. Those go in when the image-source generator lands, and a failing
    // table is re-seeded rather than patched (10 section 8).
    //
    // What is asserted is what the panel and the engine both rely on being
    // true of *any* table that replaces it: times ascend, gains decay, and
    // every bearing is a bearing.
    {
        bool ascending = true, decaying = true, panned = true;

        for (int i = 0; i < kNumReferenceTaps; ++i)
        {
            const auto& t = kReferenceTaps[i];

            panned = panned && t.pan >= -1.0f && t.pan <= 1.0f;

            if (i > 0)
            {
                ascending = ascending && t.timeMs > kReferenceTaps[i - 1].timeMs;
                decaying  = decaying  && t.gain  <  kReferenceTaps[i - 1].gain;
            }
        }

        check (ascending, "tap times ascend");
        check (decaying, "tap gains decay");
        check (panned, "every tap's bearing is within -1..+1");
        check (kNumReferenceTaps == 21, "the base tap count is 21, as 10 section 3 gives it");

        // The first two reflections stay near the centre so the phantom centre
        // holds -- the one property of the real set the stand-in reproduces,
        // and the one a re-seeded table must keep.
        check (std::abs (kReferenceTaps[0].pan) < 0.25f && std::abs (kReferenceTaps[1].pan) < 0.25f,
               "the first two reflections stay near the centre");
    }

    //== The tail figure the host will be told ===============================
    {
        DspCore::Params p;

        // preDelay + T_mid * max(1, r_lo, r_hi) + t_ER,max + 0.05
        const auto expected = 0.0f + 1.8f * 1.20f + erSpanMsAt (p.sizeM) * 0.001f + 0.05f;
        check (near (DspCore::tailSecondsFor (p), expected, 1.0e-3f),
               "the tail formula at the defaults");

        // The multiplier taken is the *largest*, and never below 1: a tail
        // cannot be reported shorter than its mid band just because both
        // damping knobs are under unity.
        DspCore::Params dark;
        dark.dampLo = 0.2f;
        dark.dampHi = 0.2f;
        check (DspCore::tailSecondsFor (dark) > dark.decaySeconds,
               "damping under unity does not shorten the reported tail below T_mid");

        DspCore::Params worst;
        worst.decaySeconds = 20.0f;
        worst.dampHi = 2.0f;
        worst.preDelayMs = 250.0f;
        worst.sizeM = 80.0f;
        check (near (DspCore::tailSecondsFor (worst), DspCore::kMaxTailSeconds, 1.0e-3f),
               "40 s of effective decay is reported as the 30 s ceiling");

        // Pre-delay is tail-only and can never be negative, so it can only
        // ever add to the figure.
        DspCore::Params delayed = p;
        delayed.preDelayMs = 250.0f;
        check (DspCore::tailSecondsFor (delayed) > DspCore::tailSecondsFor (p),
               "pre-delay lengthens the reported tail");
    }

    //== The Reverb EQ: three nodes, fixed shapes, and one mode ===============
    //
    // **Absolutes, not "it changed".** `tests/dsp/OptoDspTests.cpp` is the
    // house rule and the reason: a relative test there passed for a whole
    // release while both of the things it compared were broken. Every figure
    // below is either exactly zero -- which a matched-Z design at 0 dB really
    // is, numerator equal to denominator -- or a number a shape has to produce
    // in order to be that shape.
    {
        const auto db = [] (const EqSettings& s, double hz)
        {
            return EqNodes::design (s, kEqDesignRate).magnitudeDbAt (hz, kEqDesignRate);
        };

        const auto nodeDb = [] (const EqSettings& s, EqNode n, double hz)
        {
            return EqNodes::design (s, kEqDesignRate).nodeDbAt (n, hz, kEqDesignRate);
        };

        //-- Flat at the defaults, and flat means zero -------------------------
        //
        // The schema's own defaults, through the adapter, so this is the EQ a
        // fresh instance has rather than one written out here.
        {
            const auto v = defaults();
            const auto fresh = DspCore::eqSettingsFor (ReverbDsp::paramsFrom (v.data(), (int) v.size()));

            check (fresh.filter == EqFilter::off, "a fresh instance opens with FILTER off");

            for (const auto hz : { 20.0, 50.0, 200.0, 1000.0, 1600.0, 5000.0, 20000.0 })
                check (std::abs (db (fresh, hz)) < 1.0e-9,
                       "the Reverb EQ is exactly flat at its defaults");

            // Per node as well as summed: +6, 0 and -6 would also sum to flat,
            // and that is not the same claim.
            for (const auto n : { EqNode::low, EqNode::mid, EqNode::high })
                for (const auto hz : { 30.0, 1000.0, 12000.0 })
                    check (std::abs (nodeDb (fresh, n, hz)) < 1.0e-9,
                           "every node is individually flat at the defaults");
        }

        //-- The shapes are what they are said to be ---------------------------
        //
        // Node 1 low shelf, node 2 bell, node 3 high shelf, and no selector
        // anywhere. Through `eqShapeOf`, which is the only branch `filter`
        // causes, and then on the response -- so a table that agreed with
        // itself and with nothing audible would still fail.
        {
            // **All four positions and all three nodes**: twelve shapes
            // written out rather than a rule restated. Off and Bandpass are
            // what the bool's two modes were, exactly, and Lo Cut and Hi Cut
            // are the pair a bool could not express -- and the pair that a
            // mode reading "any cut means both cuts" would get wrong while
            // passing on the other two.
            check (eqShapeOf (EqNode::low,  EqFilter::off)      == bmo::dsp::Shape::lowShelf,  "Off: node 1 is a low shelf");
            check (eqShapeOf (EqNode::mid,  EqFilter::off)      == bmo::dsp::Shape::bell,      "Off: node 2 is a bell");
            check (eqShapeOf (EqNode::high, EqFilter::off)      == bmo::dsp::Shape::highShelf, "Off: node 3 is a high shelf");

            check (eqShapeOf (EqNode::low,  EqFilter::loCut)    == bmo::dsp::Shape::lowCut,    "Lo Cut: node 1 is a low cut");
            check (eqShapeOf (EqNode::mid,  EqFilter::loCut)    == bmo::dsp::Shape::bell,      "Lo Cut: node 2 is a bell");
            check (eqShapeOf (EqNode::high, EqFilter::loCut)    == bmo::dsp::Shape::highShelf, "Lo Cut leaves node 3 a high shelf");

            check (eqShapeOf (EqNode::low,  EqFilter::hiCut)    == bmo::dsp::Shape::lowShelf,  "Hi Cut leaves node 1 a low shelf");
            check (eqShapeOf (EqNode::mid,  EqFilter::hiCut)    == bmo::dsp::Shape::bell,      "Hi Cut: node 2 is a bell");
            check (eqShapeOf (EqNode::high, EqFilter::hiCut)    == bmo::dsp::Shape::highCut,   "Hi Cut: node 3 is a high cut");

            check (eqShapeOf (EqNode::low,  EqFilter::bandpass) == bmo::dsp::Shape::lowCut,    "Bandpass: node 1 is a low cut");
            check (eqShapeOf (EqNode::mid,  EqFilter::bandpass) == bmo::dsp::Shape::bell,      "Bandpass: node 2 is a bell");
            check (eqShapeOf (EqNode::high, EqFilter::bandpass) == bmo::dsp::Shape::highCut,   "Bandpass: node 3 is a high cut");

            // The detent order the host sees is the mode the DSP runs, through
            // the one function that converts them -- `params.h` and
            // `EqNodes.h` each name four positions and only this ties the two
            // lists together.
            check (eqFilterFor (eqFilterOff)      == EqFilter::off,      "detent 0 is Off");
            check (eqFilterFor (eqFilterLoCut)    == EqFilter::loCut,    "detent 1 is Lo Cut");
            check (eqFilterFor (eqFilterHiCut)    == EqFilter::hiCut,    "detent 2 is Hi Cut");
            check (eqFilterFor (eqFilterBandpass) == EqFilter::bandpass, "detent 3 is Bandpass");
            check (eqFilterFor (-1) == EqFilter::off && eqFilterFor (numEqFilters) == EqFilter::off,
                   "a detent outside the list is Off, not a cut nobody asked for");
            check (numEqFilters == kNumEqFilters,
                   "the schema's position count and the DSP's are the same four");

            EqSettings s;
            s.loDb = 6.0f;
            s.hiDb = -6.0f;
            s.midFreqHz = 1000.0f;
            s.midDb = 9.0f;
            s.midQ = 4.0f;

            check (std::abs (nodeDb (s, EqNode::low, 20.0) - 6.0) < 0.25,
                   "the low shelf reaches its gain below its corner");
            check (std::abs (nodeDb (s, EqNode::high, 19000.0) + 6.0) < 0.35,
                   "the high shelf reaches its gain above its corner");
            check (std::abs (nodeDb (s, EqNode::mid, 1000.0) - 9.0) < 0.05,
                   "a bell is at its gain at its own centre");
            check (std::abs (nodeDb (s, EqNode::mid, 20.0)) < 0.2
                     && std::abs (nodeDb (s, EqNode::mid, 18000.0)) < 0.2,
                   "a Q-4 bell is spent well away from its centre");
        }

        //-- FILTER moves nodes 1 and 3 and does not move node 2 --------------
        //
        // **The three claims separately**, because "the curve changed" is also
        // true of a change that broke the bell. Node 2 has to be the same
        // filter either way -- same shape, frequency, Q and gain -- so it is
        // an exact equality rather than a tolerance.
        {
            EqSettings shelves;
            shelves.loFreqHz = 200.0f;  shelves.loDb = 6.0f;   shelves.loQ = 0.71f;
            shelves.midFreqHz = 900.0f; shelves.midDb = -5.0f; shelves.midQ = 2.0f;
            shelves.hiFreqHz = 1600.0f; shelves.hiDb = 6.0f;   shelves.hiQ = 0.71f;

            auto cuts = shelves;
            cuts.filter = EqFilter::bandpass;

            auto loOnly = shelves;
            loOnly.filter = EqFilter::loCut;

            auto hiOnly = shelves;
            hiOnly.filter = EqFilter::hiCut;

            for (const auto& s : { shelves, loOnly, hiOnly, cuts })
                for (const auto hz : { 30.0, 300.0, 900.0, 4000.0, 15000.0 })
                    check (nodeDb (shelves, EqNode::mid, hz) == nodeDb (s, EqNode::mid, hz),
                           "FILTER must not move node 2 by so much as a rounding bit");

            check (nodeDb (shelves, EqNode::low, 20.0) > 5.0,
                   "as a shelf, node 1 lifts below its corner");
            check (nodeDb (cuts, EqNode::low, 20.0) < -18.0,
                   "as a cut, node 1 removes below its corner");
            check (nodeDb (shelves, EqNode::high, 16000.0) > 5.0,
                   "as a shelf, node 3 lifts above its corner");
            check (nodeDb (cuts, EqNode::high, 16000.0) < -18.0,
                   "as a cut, node 3 removes above its corner");

            // **The two halves, separately.** Lo Cut has to cut node 1 and
            // leave node 3 *bit for bit* the shelf it was, and Hi Cut the
            // other way about -- which is the whole of what the four positions
            // buy over the bool, and what a mode that fell back to "any cut
            // means both" would fail on while passing everything above.
            check (nodeDb (loOnly, EqNode::low, 20.0) < -18.0,
                   "Lo Cut removes below node 1's corner");
            check (nodeDb (loOnly, EqNode::high, 16000.0) == nodeDb (shelves, EqNode::high, 16000.0),
                   "Lo Cut leaves node 3 exactly the shelf it was");

            check (nodeDb (hiOnly, EqNode::high, 16000.0) < -18.0,
                   "Hi Cut removes above node 3's corner");
            check (nodeDb (hiOnly, EqNode::low, 20.0) == nodeDb (shelves, EqNode::low, 20.0),
                   "Hi Cut leaves node 1 exactly the shelf it was");

            // And Bandpass is the two of them at once rather than a third
            // behaviour: each node reads exactly what its own cut position
            // gave it.
            check (nodeDb (cuts, EqNode::low, 20.0) == nodeDb (loOnly, EqNode::low, 20.0)
                     && nodeDb (cuts, EqNode::high, 16000.0) == nodeDb (hiOnly, EqNode::high, 16000.0),
                   "Bandpass is Lo Cut and Hi Cut together, node for node");

            // A second-order cut is 3 dB down at its own corner, which is what
            // makes a corner a corner -- and what tells a cut from a shelf
            // that happens to lean the same way.
            EqSettings butterworth;
            butterworth.filter = EqFilter::bandpass;
            butterworth.loFreqHz = 200.0f;  butterworth.loQ = 0.7071f;
            butterworth.hiFreqHz = 2000.0f; butterworth.hiQ = 0.7071f;

            check (std::abs (nodeDb (butterworth, EqNode::low, 200.0) + 3.01) < 0.25,
                   "a Butterworth low cut is 3 dB down at its corner");
            check (std::abs (nodeDb (butterworth, EqNode::high, 2000.0) + 3.01) < 0.25,
                   "a Butterworth high cut is 3 dB down at its corner");
        }

        //-- GAIN does not reach a cut, and the mode does not eat it ----------
        //
        // The DSP half of greying a shelf's GAIN knob out. The parameter keeps
        // whatever the user set -- nothing writes it -- so the value survives a
        // trip through a cut position and back, and all that changes is whether
        // it reaches the design.
        //
        // **Per node and per position since 2026-09-22**, because the two
        // outer nodes can now be in different shapes at once: Lo Cut withholds
        // node 1's gain while node 3 goes on using its own.
        {
            EqSettings s;
            s.loDb = 9.0f;
            s.hiDb = -9.0f;

            check (near (eqGainReachingDesign (s, EqNode::low), 9.0f),
                   "as a shelf, node 1's GAIN reaches the design");
            check (eqNodeHasGain (EqNode::low, EqFilter::off) && eqNodeHasGain (EqNode::high, EqFilter::off),
                   "both shelves have gain");

            // The greying table, all four positions and both outer nodes.
            check (eqNodeHasGain (EqNode::low, EqFilter::hiCut),
                   "Hi Cut leaves node 1's GAIN live -- node 1 is still a shelf");
            check (! eqNodeHasGain (EqNode::low, EqFilter::loCut),
                   "Lo Cut takes node 1's GAIN");
            check (eqNodeHasGain (EqNode::high, EqFilter::loCut),
                   "Lo Cut leaves node 3's GAIN live -- node 3 is still a shelf");
            check (! eqNodeHasGain (EqNode::high, EqFilter::hiCut),
                   "Hi Cut takes node 3's GAIN");

            for (const auto f : { EqFilter::off, EqFilter::loCut, EqFilter::hiCut, EqFilter::bandpass })
                check (eqNodeHasGain (EqNode::mid, f),
                       "the bell keeps its gain in every position");

            // One cut at a time reaches the design: the other node's gain is
            // still there, at the number the knob holds.
            auto lo = s;  lo.filter = EqFilter::loCut;
            auto hi = s;  hi.filter = EqFilter::hiCut;

            check (near (eqGainReachingDesign (lo, EqNode::low), 0.0f)
                     && near (eqGainReachingDesign (lo, EqNode::high), -9.0f),
                   "Lo Cut withholds node 1's GAIN and lets node 3's through");
            check (near (eqGainReachingDesign (hi, EqNode::low), 9.0f)
                     && near (eqGainReachingDesign (hi, EqNode::high), 0.0f),
                   "Hi Cut withholds node 3's GAIN and lets node 1's through");

            s.filter = EqFilter::bandpass;

            check (near (eqGainReachingDesign (s, EqNode::low), 0.0f)
                     && near (eqGainReachingDesign (s, EqNode::high), 0.0f),
                   "as a cut, neither outer node's GAIN reaches the design");
            check (! eqNodeHasGain (EqNode::low, EqFilter::bandpass)
                     && ! eqNodeHasGain (EqNode::high, EqFilter::bandpass),
                   "neither outer node has gain in Bandpass -- this is what greys the knobs");

            check (near (eqKnobGainDbOf (s, EqNode::low), 9.0f)
                     && near (eqKnobGainDbOf (s, EqNode::high), -9.0f),
                   "FILTER does not eat the shelf gains it is ignoring");

            // **Withheld, never zeroed: the round trip.** A shelf gain set,
            // driven through every cut position in turn and brought back to
            // Off, has to design the same filter it did before it left -- both
            // the knob's own value and the response it produces. A mode that
            // wrote the parameter instead of ignoring it would pass every
            // check above and fail this one.
            //
            // Through the adapter and not through a struct literal, because
            // the parameter array is the thing a mode could write to.
            {
                auto v = defaults();
                v[Index::eqlo] = 9.0f;
                v[Index::eqhi] = -9.0f;

                const auto settingsAt = [&v] (EqFilterChoice f)
                {
                    v[Index::eqfilter] = (float) f;
                    return DspCore::eqSettingsFor (ReverbDsp::paramsFrom (v.data(), (int) v.size()));
                };

                const auto before = settingsAt (eqFilterOff);

                for (const auto f : { eqFilterLoCut, eqFilterHiCut, eqFilterBandpass })
                {
                    const auto cut = settingsAt (f);

                    check (near (eqKnobGainDbOf (cut, EqNode::low), 9.0f)
                             && near (eqKnobGainDbOf (cut, EqNode::high), -9.0f),
                           "a cut position must not write the shelf gains it is ignoring");
                }

                const auto after = settingsAt (eqFilterOff);

                check (near (eqKnobGainDbOf (after, EqNode::low), 9.0f)
                         && near (eqKnobGainDbOf (after, EqNode::high), -9.0f),
                       "a shelf gain comes back off a trip through the cuts");

                for (const auto hz : { 20.0, 200.0, 1000.0, 16000.0 })
                    check (db (before, hz) == db (after, hz),
                           "the EQ designs the same filter it did before the cuts");
            }

            // Turning a cut's gain knob does nothing to the sound, which is
            // the claim a greyed knob makes to the eye.
            auto moved = s;
            moved.loDb = -24.0f;
            moved.hiDb = 12.0f;

            for (const auto hz : { 30.0, 1000.0, 15000.0 })
                check (db (s, hz) == db (moved, hz),
                       "a cut's GAIN knob changes nothing at all");
        }

        //-- Stable and finite everywhere a host can put it -------------------
        //
        // Both ends and the default of all seven continuous EQ controls, both
        // modes, four rates. A design that went unstable on one combination is
        // a burst of noise in somebody's session and there is no listening
        // pass that finds it first.
        {
            const auto& sp = specs();

            const auto ends = [&sp] (int i)
            {
                return std::array<float, 3> { sp[(size_t) i].min, sp[(size_t) i].def, sp[(size_t) i].max };
            };

            auto unstable = 0;

            for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (const auto filter : { EqFilter::off, EqFilter::loCut,
                                           EqFilter::hiCut, EqFilter::bandpass })
                    for (const auto lf : ends (Index::eqlofreq))
                        for (const auto lq : ends (Index::eqloq))
                            for (const auto mf : ends (Index::eqmidfreq))
                                for (const auto mg : ends (Index::eqmid))
                                    for (const auto mq : ends (Index::eqmidq))
                                        for (const auto hf : ends (Index::eqhifreq))
                                            for (const auto hq : ends (Index::eqhiq))
                                            {
                                                EqSettings s;
                                                s.filter = filter;
                                                s.loFreqHz = lf;  s.loDb = sp[Index::eqlo].min;  s.loQ = lq;
                                                s.midFreqHz = mf; s.midDb = mg;                  s.midQ = mq;
                                                s.hiFreqHz = hf;  s.hiDb = sp[Index::eqhi].max;  s.hiQ = hq;

                                                if (! EqNodes::design (s, rate).isStable())
                                                    ++unstable;
                                            }

            check (unstable == 0,
                   "every corner of the EQ's own ranges designs a stable, finite filter");
        }
    }

    //== The early-reflection tables are the generator's, bit for bit =========
    //
    // 11 section 6's Golden STATE row asks for the *derived* tables to be
    // pinned, because "a failing table is re-seeded, not patched" (10 section
    // 8) only means something if the table is what the seed makes. So every
    // type is regenerated here from its recipe and pinned seed and compared
    // with the committed ErTableData.inc field by field, with ==. The
    // generator rounds onto a coarse grid before it returns, which is what
    // lets this be equality rather than a tolerance on every platform.
    //
    // A red here means the generator, a recipe or a type's default SIZE moved
    // and the data did not: run `measure_reverb taps --emit`, then `--audit`,
    // and commit both halves together. It never means "edit the .inc".
    {
        int mismatches = 0, unbuilt = 0;

        for (int t = 0; t < numTypes; ++t)
        {
            ErTable fresh;

            if (! ergen::generate (t, ergen::recipeFor (t).seed, fresh))
            {
                ++unbuilt;
                continue;
            }

            const auto& shipped = erTableFor (t);

            for (int v = 0; v < kErVariations; ++v)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto& a = ch == 0 ? fresh.variation[v].left : fresh.variation[v].right;
                    const auto& b = ch == 0 ? shipped.variation[v].left : shipped.variation[v].right;

                    mismatches += a.numTaps != b.numTaps ? 1 : 0;

                    for (int i = 0; i < kErMaxTaps; ++i)
                    {
                        const auto& x = a.taps[i];
                        const auto& y = b.taps[i];
                        mismatches += (x.timeMs != y.timeMs || x.gain != y.gain || x.theta != y.theta
                                       || x.pan != y.pan || x.band != y.band) ? 1 : 0;
                    }
                }

            mismatches += fresh.combDelayMs != shipped.combDelayMs ? 1 : 0;
            mismatches += fresh.combGain != shipped.combGain ? 1 : 0;
            mismatches += fresh.windowMs != shipped.windowMs ? 1 : 0;
            mismatches += fresh.windowClampMs != shipped.windowClampMs ? 1 : 0;
            mismatches += fresh.beta != shipped.beta ? 1 : 0;
            mismatches += fresh.seed != shipped.seed ? 1 : 0;

            for (int b = 0; b < kErBands; ++b)
                mismatches += fresh.bandCutoffHz[b] != shipped.bandCutoffHz[b] ? 1 : 0;
        }

        check (unbuilt == 0, "every type's pinned seed generates a table");
        check (mismatches == 0, "the shipped ER tables are the generator's at the pinned seeds, bit for bit");
    }

    //== What the engine may rely on of every table ============================
    {
        bool shaped = true, ascending = true, cored = true, thresholds = true, banded = true,
             bearings = true, inside = true, positive = true, combMono = true;

        for (int t = 0; t < numTypes; ++t)
        {
            const auto& table = erTableFor (t);

            for (int v = 0; v < kErVariations; ++v)
                for (const auto* ch : { &table.variation[v].left, &table.variation[v].right })
                {
                    shaped = shaped && ch->numTaps == kErMaxTaps;
                    int core = 0;

                    for (int i = 0; i < ch->numTaps; ++i)
                    {
                        const auto& tap = ch->taps[i];
                        core += tap.theta == 0.0f ? 1 : 0;
                        thresholds = thresholds && tap.theta >= 0.0f && tap.theta < 1.0f - DspCore::kRampWidth;
                        banded     = banded && tap.band >= 0 && tap.band < kErBands;
                        bearings   = bearings && tap.pan >= -1.0f && tap.pan <= 1.0f;
                        inside     = inside && tap.timeMs > 0.0f && tap.timeMs <= table.windowMs;
                        positive   = positive && tap.gain > 0.0f;

                        if (i > 0)
                            ascending = ascending && tap.timeMs > ch->taps[i - 1].timeMs;
                    }

                    cored = cored && core == kErCoreTaps;
                }

            // VARIATION 6 carries the mono set in both channels; the engine
            // builds Schroeder's pair from it.
            const auto& six = table.variation[kErCombVariation];

            for (int i = 0; i < kErMaxTaps; ++i)
                combMono = combMono && six.left.taps[i].timeMs == six.right.taps[i].timeMs
                                    && six.left.taps[i].gain == six.right.taps[i].gain;
        }

        check (shaped, "every channel of every table holds 48 taps");
        check (ascending, "every channel's taps ascend in time");
        check (cored, "every channel carries exactly the 21 core taps at theta 0");
        // The engine ramps literally, w = clamp ((D - theta) / 0.08, 0, 1), so a
        // threshold above 1 - 0.08 would never reach full weight at DENSITY
        // 100 %, and one at 1 would never sound at all.
        check (thresholds, "every density threshold is under 0.92, so every infill pulse is fully on at 100 %");
        check (banded, "every tap names one of the four bands");
        check (bearings, "every bearing is within -1..+1");
        check (inside, "every tap is after the direct sound and inside its table's window");
        check (positive, "every gain is positive, so no tap pair can be anti-correlated");
        check (combMono, "VARIATION 6 carries one set in both channels");

        // The window clamps are 10 section 3's, and beta runs 0.70 Room to
        // 0.88 Cavern in type order.
        check (erTableFor (room).windowClampMs == 100.0f && erTableFor (chamber).windowClampMs == 100.0f
                   && erTableFor (ambience).windowClampMs == 100.0f,
               "Room, Chamber and Ambience clamp their window at 100 ms");
        check (erTableFor (hall).windowClampMs == 200.0f && erTableFor (cavern).windowClampMs == 200.0f
                   && erTableFor (plate).windowClampMs == 200.0f,
               "Hall, Cavern and Plate clamp their window at 200 ms");
        check (erTableFor (room).beta == 0.70f && erTableFor (cavern).beta == 0.88f
                   && erTableFor (chamber).beta > 0.70f && erTableFor (hall).beta > erTableFor (chamber).beta
                   && erTableFor (hall).beta < 0.88f,
               "beta runs 0.70 Room to 0.88 Cavern in type order");
    }

    //== The cutoff law and the span, read through the contract's functions ===
    {
        // erBandCutoffHzAt writes kappa again rather than including the
        // generator's; the tables' cutoffs are quoted with the generator's, so
        // the two must agree or the engine's bands drift off the audit's.
        bool agree = true;

        for (int t = 0; t < numTypes; ++t)
            for (int b = 0; b < kErBands; ++b)
            {
                const auto& table = erTableFor (t);
                const auto s = constantsFor (t).sizeM;
                const auto expected = (double) table.bandCutoffHz[b]
                                    * std::pow ((double) kReferenceSizeM / s, ergen::kKappa);
                agree = agree && std::abs (erBandCutoffHzAt (table, b, s) - expected) <= 1.0e-4 * expected;
            }

        check (agree, "erBandCutoffHzAt uses the generator's kappa");

        // The span is the last tap by the Size law, held to the clamp: Room's
        // is under its clamp at 12 m, and every table is at its clamp by 80 m.
        const auto& roomTable = erTableFor (room);
        check (erSpanMsAt (roomTable, kReferenceSizeM) > 90.0f && erSpanMsAt (roomTable, kReferenceSizeM) < 100.0f,
               "Room's ER span at 12 m is its last tap, inside 90-100 ms");

        bool clamped = true;

        for (int t = 0; t < numTypes; ++t)
            if (t != plate)
                clamped = clamped && erSpanMsAt (erTableFor (t), 80.0f) == erTableFor (t).windowClampMs;

        check (clamped, "at 80 m every room table's span is its clamp");

        // Plate spans about 39 ms at its 22 m default, so even at 80 m it stays
        // inside its 200 ms clamp: about 140 ms, the Size law and no more.
        const auto plateSpan = erSpanMsAt (erTableFor (plate), 80.0f);
        check (plateSpan > 120.0f && plateSpan < 160.0f, "at 80 m Plate's span is its last tap, 120-160 ms, inside its clamp");
    }

    //== The audits of 10 section 3 and 11 section 6, on every shipped table ==
    //
    // Each rule is in ErAudit.cpp, evaluated at the type's default SIZE, after
    // the jitter; `measure_reverb taps --audit` prints every margin. Every
    // rule passes at every type **but one**, and it is named here rather than
    // hidden: Cavern fails flam rule (i) by 1.50 dB at its 55 m default SIZE,
    // because a room that large puts its first-order walls 40 ms and more
    // behind the floor bounce (see the testing note). **This is a known
    // failure on purpose, not a desk bug: the owner will decide it by ear at
    // the listening checkpoint -- Cavern at 55 m against a smaller SIZE --
    // 2026-09-23. Do not search seeds for it.** It is held to an absolute
    // floor so it cannot quietly get worse.
    //
    // Plate's room rules print n/a and pass by construction: the owner ruled
    // on 2026-09-23 that a plate is not a room (ErAudit.h). Its own three
    // plate rules are asserted here like any other rule.
    {
        for (int t = 0; t < numTypes; ++t)
        {
            const auto rep = ergen::audit (erTableFor (t), ergen::contextFor (t));

            for (int r = 0; r < ergen::numRules; ++r)
            {
                const std::string what = std::string (kTypeNames[t]) + ": " + ergen::ruleName (r);

                if (t == cavern && r == ergen::ruleFlamLate)
                {
                    check (rep.margin[r] >= -1.6, (what + " -- the known failure, no worse than -1.6 dB").c_str());
                    continue;
                }

                check (rep.pass[r], what.c_str());
            }
        }
    }

    //== The audits are not vacuous: each one reddens on a table broken for it =
    //
    // Room's shipped table, copied and broken by hand in exactly one way per
    // rule -- a tap moved, a band changed, a gain raised -- never re-seeded.
    // Each break must fail the rule it was made for. This is the permanent
    // form of breaking a table to watch a test go red.
    {
        const auto ctx = ergen::contextFor (room);
        const auto& base = erTableFor (room);

        const auto fails = [&ctx] (const ErTable& broken, int rule)
        {
            return ! ergen::audit (broken, ctx).pass[rule];
        };

        const auto sortAll = [] (ErTable& table)
        {
            for (auto& v : table.variation)
                for (auto* ch : { &v.left, &v.right })
                    std::sort (ch->taps, ch->taps + ch->numTaps,
                               [] (const ErTap& a, const ErTap& b) { return a.timeMs < b.timeMs; });
        };

        // The index of the first tap at or after `ms`, in one channel.
        const auto at = [] (const ErChannel& ch, float ms)
        {
            int i = 0;

            while (i < ch.numTaps - 1 && ch.taps[i].timeMs < ms)
                ++i;

            return i;
        };

        {
            auto b = base;
            auto& ch = b.variation[2].left;
            const int i = at (ch, 40.0f);
            ch.taps[i].timeMs = ch.taps[i - 1].timeMs + 0.3f;
            check (fails (b, ergen::ruleSeparation), "a tap 0.3 ms from its neighbour fails the separation audit");
        }

        {
            // Three consecutive core taps given two equal gaps.
            auto b = base;
            auto& ch = b.variation[0].left;
            int core[3] { -1, -1, -1 }, n = 0;

            for (int i = at (ch, 30.0f); i < ch.numTaps && n < 3; ++i)
                if (ch.taps[i].theta == 0.0f)
                    core[n++] = i;

            ch.taps[core[2]].timeMs = ch.taps[core[1]].timeMs + (ch.taps[core[1]].timeMs - ch.taps[core[0]].timeMs);
            sortAll (b);
            check (n == 3 && fails (b, ergen::ruleGaps), "two equal core gaps fail the gap audit");
        }

        {
            auto b = base;
            b.variation[3].right.taps[0].band = 0;   // the floor bounce, inside 8 ms, made full-band
            check (fails (b, ergen::ruleFullBand), "a full-band tap between 1 and 8 ms fails its audit");
        }

        {
            auto b = base;
            auto& ch = b.variation[1].left;
            ch.taps[at (ch, 10.0f)].gain = 0.3f;     // -10.5 dB at about 10 ms, over -14
            check (fails (b, ergen::ruleKuttruff), "a -10.5 dB tap near 10 ms fails the Kuttruff ceiling");
        }

        {
            auto b = base;
            auto& ch = b.variation[4].right;
            ch.taps[at (ch, 60.0f)].gain = 0.2f;     // -14 dB, past the Kuttruff window
            check (fails (b, ergen::ruleTapCeiling), "a -14 dB tap fails the -15.3 dB ceiling");
        }

        {
            auto b = base;
            auto& ch = b.variation[2].right;
            auto& tap = ch.taps[at (ch, 60.0f)];
            tap.gain = 0.12f;                        // -18.4 dB: under the ceiling, over (i)
            tap.theta = 0.0f;
            check (fails (b, ergen::ruleFlamLate), "a -18 dB tap at 60 ms fails flam rule (i)");
            check (! fails (b, ergen::ruleTapCeiling), "... and is still under the tap ceiling");
        }

        {
            auto b = base;
            auto& ch = b.variation[5].left;
            auto& tap = ch.taps[at (ch, 72.0f)];
            tap.gain = 0.05f;                        // -26 dB where the envelope is near -38
            tap.theta = 0.0f;
            check (fails (b, ergen::ruleFlamOnset), "a -26 dB tap at 72 ms fails flam rule (ii)");
        }

        {
            auto b = base;

            for (auto& v : b.variation)
                for (auto* ch : { &v.left, &v.right })
                    for (int i = 0; i < ch->numTaps; ++i)
                        ch->taps[i].gain *= 6.0f;

            check (fails (b, ergen::ruleLoc), "every tap 15.6 dB louder fails flam rule (iii)");
        }

        {
            auto b = base;

            for (auto& v : b.variation)
                for (auto* ch : { &v.left, &v.right })
                    ch->taps[0].timeMs = 6.0f;       // the floor bounce, moved out of the first 5 ms

            check (fails (b, ergen::ruleProximity), "no tap inside 5 ms fails flam rule (iv)");
        }

        {
            auto b = base;
            b.variation[0].left.taps[0].pan = 0.8f;
            check (fails (b, ergen::ruleCentre), "a first reflection at pan 0.8 fails the centre rule");
        }

        {
            auto b = base;
            b.variation[3] = b.variation[2];          // two positions with one gamma
            check (fails (b, ergen::ruleGamma), "two VARIATION positions alike fail the gamma rule");
        }

        {
            auto b = base;

            for (auto* ch : { &b.variation[2].left, &b.variation[2].right })
                for (int i = 0; i < ch->numTaps; ++i)
                    ch->taps[i].pan = 0.0f;

            check (fails (b, ergen::ruleLateral), "every image on the median plane fails the lateral fraction");
        }

        {
            auto b = base;

            for (int i = 0; i < b.variation[2].left.numTaps; ++i)
                b.variation[2].left.taps[i].theta = 0.0f;   // 48 "core" taps against Moorer's 19

            check (fails (b, ergen::ruleMoorer), "48 core taps fail the Moorer sanity check");
        }
    }

    //== Plate is held to plate rules, and only to them ========================
    //
    // Owner decision, 2026-09-23: a plate is a physical metal plate model, not
    // a room, and the room rules do not apply (ErAudit.h). Three things here:
    // the room rules are withdrawn for Plate and for no room; the plate rules
    // are withdrawn for every room; and each plate rule reddens on a Plate
    // table broken by hand for it.
    {
        const auto plateCtx = ergen::contextFor (plate);
        const auto plateRep = ergen::audit (erTableFor (plate), plateCtx);
        const auto roomRep  = ergen::audit (erTableFor (room), ergen::contextFor (room));

        bool withdrawn = true, stillRoom = true;

        for (const int r : { ergen::ruleSeparation, ergen::ruleGaps, ergen::ruleFullBand, ergen::ruleKuttruff,
                             ergen::ruleFlamLate, ergen::ruleFlamOnset, ergen::ruleLoc, ergen::ruleProximity,
                             ergen::ruleCentre, ergen::ruleLateral, ergen::ruleMoorer })
        {
            withdrawn = withdrawn && std::isinf (plateRep.margin[r]);
            stillRoom = stillRoom && ! std::isinf (roomRep.margin[r]);
        }

        check (withdrawn, "Plate is not held to any room rule");
        check (stillRoom, "Room is still held to every room rule");
        check (! std::isinf (plateRep.margin[ergen::ruleTapCeiling]) && ! std::isinf (plateRep.margin[ergen::ruleGamma]),
               "Plate is still held to the -15.3 dB ceiling and to gamma >= 0");
        bool noPlateRules = true;

        for (const int r : { ergen::rulePlateOnset, ergen::rulePlateFront, ergen::rulePlateSwellTime,
                             ergen::rulePlateSwellRise, ergen::rulePlateBands, ergen::rulePlateLr500 })
            noPlateRules = noPlateRules && std::isinf (roomRep.margin[r]);

        check (noPlateRules, "Room is not held to the plate rules");

        // Each measured plate rule (ErAudit.h: measured, 16 EMT 140 IRs,
        // research doc section 8, 2026-09-23) reddens on a Plate table broken
        // by hand for it. Times in the table are at 12 m; Plate plays at 22 m.
        const auto fails = [&plateCtx] (const ErTable& broken, int rule)
        {
            return ! ergen::audit (broken, plateCtx).pass[rule];
        };

        const float toDefault = constantsFor (plate).sizeM / kReferenceSizeM;

        const auto tapAt = [toDefault] (const ErChannel& ch, float ms)
        {
            int i = 0;

            while (i < ch.numTaps - 1 && ch.taps[i].timeMs * toDefault < ms)
                ++i;

            return i;
        };

        {
            // The whole left channel of VARIATION 1 starting at 3 ms.
            auto b = erTableFor (plate);
            auto& ch = b.variation[1].left;
            const auto shift = 3.0f / toDefault - ch.taps[0].timeMs;

            for (int i = 0; i < ch.numTaps; ++i)
                ch.taps[i].timeMs += shift;

            check (fails (b, ergen::rulePlateOnset), "a plate that starts at 3 ms fails the onset rule");
        }

        {
            // A loud bright tap at about 3 ms: the front is no longer weak,
            // and the swell no longer rises 8 dB over it.
            auto b = erTableFor (plate);
            auto& ch = b.variation[2].right;
            auto& tap = ch.taps[tapAt (ch, 3.0f)];
            tap.gain = 0.12f;
            tap.theta = 0.0f;
            check (fails (b, ergen::rulePlateFront), "a strong tap inside 5 ms fails the front rule");
            check (fails (b, ergen::rulePlateSwellRise), "... and the swell-rise rule");
        }

        {
            // A loud tap at 35 ms moves the envelope's peak past 25 ms.
            auto b = erTableFor (plate);
            auto& ch = b.variation[3].left;
            auto& tap = ch.taps[tapAt (ch, 35.0f)];
            tap.gain = 0.12f;
            tap.theta = 0.0f;
            tap.band = 0;
            check (fails (b, ergen::rulePlateSwellTime), "a plate peaking at 35 ms fails the swell-time rule");
        }

        {
            // The 8 kHz band's taps relabelled into the 125 Hz band: lows first.
            auto b = erTableFor (plate);
            auto& ch = b.variation[0].left;

            for (int i = 0; i < ch.numTaps; ++i)
                if (ch.taps[i].band == 0)
                    ch.taps[i].band = kErBands - 1;

            check (fails (b, ergen::rulePlateBands), "a plate whose lows arrive first fails the band-onset rule");
        }

        {
            // The right channel of VARIATION 4 made the left: no 500 Hz lag.
            auto b = erTableFor (plate);
            b.variation[4].right = b.variation[4].left;
            check (fails (b, ergen::rulePlateLr500), "a plate with no right-later 500 Hz lag fails the L/R rule");
        }
    }

    //== Cavern B: a candidate for the owner's ear, pinned, never shipped ======
    //
    // Tool-only (ImageSource.h): it lives in bmo_reverb_ergen, which no plugin
    // links, and erTableFor (cavern) is still the shipping Cavern -- which the
    // pin above holds bit for bit. Cavern B is held to every rule the
    // shipping tables are, and to flam (i) at least 4 dB clear, which is the
    // point of it (2026-09-24).
    {
        const auto* b = ergen::erCandidateTable ("cavern-b");
        check (b != nullptr, "the cavern-b candidate is reachable from the tools");
        check (ergen::erCandidateTable ("cavern") == nullptr && ergen::erCandidateTable (nullptr) == nullptr,
               "only named candidates exist");

        if (b != nullptr)
        {
            ErTable fresh;
            const bool built = ergen::generateCandidate ("cavern-b", ergen::candidateRecipe ("cavern-b")->seed, fresh);
            check (built, "cavern-b's pinned seed generates a table");
            check (built && std::memcmp (&fresh, b, sizeof (ErTable)) == 0,
                   "cavern-b is the generator's at its pinned seed, bit for bit");
            check (b != &erTableFor (cavern), "the shipping Cavern is not the candidate");

            const auto ctx = ergen::candidateContext ("cavern-b");
            const auto rep = ergen::audit (*b, ctx);

            for (int r = 0; r < ergen::numRules; ++r)
                check (rep.pass[r], (std::string ("Cavern B: ") + ergen::ruleName (r)).c_str());

            check (rep.margin[ergen::ruleFlamLate] >= 4.0, "Cavern B passes flam (i) at least 4 dB clear");

            // A hand break: one late tap raised to the shipping Cavern's level.
            auto broken = *b;
            auto& ch = broken.variation[2].left;
            int i = 0;

            while (i < ch.numTaps - 1 && ch.taps[i].timeMs * ctx.sizeM / kReferenceSizeM < 100.0f)
                ++i;

            ch.taps[i].gain *= 4.0f;
            ch.taps[i].theta = 0.0f;
            check (! ergen::audit (broken, ctx).pass[ergen::ruleFlamLate], "a Cavern B cluster tap 12 dB up fails flam (i)");
        }
    }

    //== Every number in every table is finite =================================
    {
        bool finite = true;

        for (int t = 0; t < numTypes; ++t)
        {
            const auto& table = erTableFor (t);

            for (const auto& v : table.variation)
                for (const auto* ch : { &v.left, &v.right })
                    for (int i = 0; i < ch->numTaps; ++i)
                        finite = finite && std::isfinite (ch->taps[i].timeMs) && std::isfinite (ch->taps[i].gain)
                                        && std::isfinite (ch->taps[i].theta) && std::isfinite (ch->taps[i].pan);

            for (const auto c : table.bandCutoffHz)
                finite = finite && std::isfinite (c) && c > 0.0f;

            finite = finite && std::isfinite (table.windowMs) && std::isfinite (table.beta)
                            && std::isfinite (table.combDelayMs) && std::isfinite (table.combGain);
        }

        check (finite, "every number in every ER table is finite");
    }

    //== prepare() and reset() are reachable and do not throw ================
    // Thin, and it is worth having: the real engine allocates in prepare() and
    // nowhere else, and this is the call that will start doing so.
    {
        ReverbDsp dsp;

        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const auto block : { 1, 16, 127, 512, 2048 })
            {
                dsp.prepare (rate, block, 2);
                dsp.reset();
            }

        check (true, "prepare and reset survive the rate and block matrix");
    }

    //== The early reflections (M2) ===========================================
    erEngineTests();

    if (failures == 0)
        std::cout << "reverb_dsp: all checks passed\n";

    return failures == 0 ? 0 : 1;
}
