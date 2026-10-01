/*
    BMO Linger's DSP, JUCE-free.

    **There is no reverb under this yet.** `modules/reverb/dsp/DspCore.h` is a
    marked placeholder that passes audio through, so what can be asserted here
    is everything that is true of the *frame* rather than of the engine: the
    adapter's unpacking, the latency contract, the tail arithmetic, the Size
    law, and the tap table's shape.

    That is a short list, and the long one is written down where it will be
    read rather than discovered. `docs/reverb/11-integration-and-test-plan.md`
    section 6 is the whole suite this file grows into -- T60 by Schroeder
    backward integration fitted over two ranges, per-octave damping ratios, tap
    times to the sample against the image-source table, the comb and flamming
    rules as assertions, mono correlation at all seven VARIATION positions,
    normalised echo density and mixing time, the modal-density rule that 10
    section 4 expects Plate to *fail*, the modulation pitch bound, the level
    laws and the two phasing nulls, and the invariance matrix over rate and
    block size. None of it can be written against a wire.

    What this file does instead is make sure the wire is a wire, and that
    everything the engine will be built on top of already agrees with itself.
*/

#include "modules/reverb/dsp/ImageSource.h"
#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
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

    //== Rendering the early reflections =======================================

    struct Ir
    {
        std::vector<float> l, r;
        double rate = 48000.0;

        int    size() const noexcept { return (int) l.size(); }
        float  energy (int from, int to) const noexcept
        {
            float e = 0.0f;
            for (int i = std::max (0, from); i < std::min (to, size()); ++i)
                e += l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i];
            return e;
        }
        int msToSamples (float ms) const noexcept { return (int) std::lround (ms * 0.001 * rate); }
    };

    /** The ER-only condition every rule in 11 section 6's ER block is written
        in: REVERB off, ER at 0 dB, MIX 100 %, OUTPUT 0 dB, ER HI-CUT open.
        `edit` then moves whatever the test is about. A pre-roll of 300 ms of
        silence lets every smoothed gain, the density weighting and any
        crossfade a parameter change started settle before the impulse. */
    Ir render (const std::function<void (std::vector<float>&)>& edit,
               double rate = 48000.0, int block = 512, float seconds = 0.6f, int channels = 2)
    {
        auto v = defaults();
        v[Index::verblevel] = -40.0f;
        v[Index::erlevel]   = 0.0f;
        v[Index::mix]       = 100.0f;
        v[Index::output]    = 0.0f;
        v[Index::erhicut]   = 20000.0f;
        v[Index::erdensity] = 0.0f;
        v[Index::ervariation] = 0.0f;
        edit (v);

        ReverbDsp dsp;
        dsp.prepare (rate, block, channels);
        dsp.setParams (v.data(), (int) v.size());

        const auto preroll = (int) (0.3 * rate);
        const auto n       = (int) (seconds * rate);

        std::vector<float> l ((size_t) (preroll + n), 0.0f), r ((size_t) (preroll + n), 0.0f);
        l[(size_t) preroll] = 1.0f;
        r[(size_t) preroll] = 1.0f;

        for (int at = 0; at < preroll + n; at += block)
        {
            const auto count = std::min (block, preroll + n - at);
            float* chans[] { l.data() + at, r.data() + at };
            dsp.setParams (v.data(), (int) v.size());
            dsp.process (chans, channels, count);
        }

        Ir ir;
        ir.rate = rate;
        ir.l.assign (l.begin() + preroll, l.end());
        ir.r.assign (channels > 1 ? r.begin() + preroll : l.begin() + preroll, channels > 1 ? r.end() : l.end());
        return ir;
    }

    /** A tap as measured off an IR: the sample where |l| + |r| peaks inside
        `window` samples after `from`, and its gain as the constant-power sum
        of the two channels' DC sums over that window -- the one-pole band
        filters have unity DC gain, so the sum recovers the table's gain
        while the peak alone would be attenuated by the filter. */
    struct Measured { int sample; float gain; };

    Measured measureTap (const Ir& ir, int from, int to)
    {
        int best = from;
        float peak = -1.0f;
        float sumL = 0.0f, sumR = 0.0f;

        for (int i = from; i < std::min (to, ir.size()); ++i)
        {
            const auto a = std::abs (ir.l[(size_t) i]) + std::abs (ir.r[(size_t) i]);
            if (a > peak) { peak = a; best = i; }
            sumL += ir.l[(size_t) i];
            sumR += ir.r[(size_t) i];
        }

        return { best, std::sqrt (sumL * sumL + sumR * sumR) };
    }

    float db (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-12f)); }

    /** L/R correlation of the ER bus over its span. */
    float correlation (const Ir& ir, int to)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < std::min (to, ir.size()); ++i)
        {
            lr += ir.l[(size_t) i] * ir.r[(size_t) i];
            ll += ir.l[(size_t) i] * ir.l[(size_t) i];
            rr += ir.r[(size_t) i] * ir.r[(size_t) i];
        }
        return ll > 0.0 && rr > 0.0 ? (float) (lr / std::sqrt (ll * rr)) : 0.0f;
    }

    /** Magnitude of the mono IR at one frequency, by direct summation. */
    double magnitudeAt (const Ir& ir, double hz, int to)
    {
        double re = 0.0, im = 0.0;
        const auto w = 2.0 * 3.14159265358979 * hz / ir.rate;
        for (int i = 0; i < std::min (to, ir.size()); ++i)
        {
            const auto m = 0.5 * (ir.l[(size_t) i] + ir.r[(size_t) i]);
            re += m * std::cos (w * i);
            im -= m * std::sin (w * i);
        }
        return std::sqrt (re * re + im * im);
    }

    /** 1/3-octave smoothed magnitude in dB: the power mean over nine
        frequencies spanning the band. */
    double thirdOctaveDb (const Ir& ir, double centreHz, int to)
    {
        double p = 0.0;
        for (int k = -4; k <= 4; ++k)
        {
            const auto f = centreHz * std::pow (2.0, (double) k / 24.0);
            const auto m = magnitudeAt (ir, f, to);
            p += m * m;
        }
        return 10.0 * std::log10 (std::max (p / 9.0, 1.0e-24));
    }

    /** What `stepRatioAcross` measured, and how many times the edit it was
        asked to measure actually ran. `settled` is the largest step over the
        last quarter second against the same first-half figure: the new
        setting's own level, so a ratio near it is the level moving and not a
        click. Printed, not asserted. */
    struct StepRatio { float ratio; int edits; float settled; };

    /** Runs a steady 1 kHz sine through the module while `edit` is applied
        at the halfway point, and returns the largest sample-to-sample step
        in the output over the second half relative to the largest over the
        first -- a click is a step the signal did not have before.

        **The edit lands on the first block boundary at or after the halfway
        point**, and the count of edits comes back with the ratio. Until the
        2026-09-30 review it was applied only when a block started exactly
        at 24000, and with 256-sample blocks none does: the edit never ran in
        940 block iterations, and all five "does not click" checks were
        comparing a steady sine with itself. */
    StepRatio stepRatioAcross (const std::function<void (std::vector<float>&)>& setup,
                               const std::function<void (std::vector<float>&)>& edit)
    {
        auto v = defaults();
        v[Index::verblevel] = -40.0f;
        v[Index::erlevel]   = 0.0f;
        v[Index::mix]       = 100.0f;
        setup (v);

        ReverbDsp dsp;
        dsp.prepare (48000.0, 256, 2);
        dsp.setParams (v.data(), (int) v.size());

        constexpr int total = 48000;
        std::vector<float> l ((size_t) total), r ((size_t) total);
        for (int i = 0; i < total; ++i)
            l[(size_t) i] = r[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / 48000.0f);

        float before = 0.0f, after = 0.0f, settled = 0.0f;
        int edits = 0;

        for (int at = 0; at < total; at += 256)
        {
            if (edits == 0 && at >= total / 2)
            {
                edit (v);
                ++edits;
            }

            float* chans[] { l.data() + at, r.data() + at };
            dsp.setParams (v.data(), (int) v.size());
            dsp.process (chans, 2, std::min (256, total - at));
        }

        for (int i = 1; i < total; ++i)
        {
            const auto step = std::abs (l[(size_t) i] - l[(size_t) i - 1]);
            if (i < total / 2 && i > 12000) before = std::max (before, step);
            if (i >= total / 2) after = std::max (after, step);
            if (i >= total - 12000) settled = std::max (settled, step);
        }

        return { before > 0.0f ? after / before : 0.0f, edits, before > 0.0f ? settled / before : 0.0f };
    }

    //== Steady noise through the module ======================================
    //
    // The review of PR #27 (2026-09-30) found the ER going permanently silent
    // on paths the impulse renders above never take: a second prepare(), a
    // first block longer than the TYPE dip, a TYPE change at a large block. An
    // impulse can land in a silent stretch and say nothing, so these run a
    // fixed white noise instead -- every tap is busy on every sample, and an
    // exact zero on the output means a silent table, not a quiet input.

    /** The setting the review reproduced every silence in: Taps, DENSITY 50,
        VARIATION 4, ER at 0 dB, REVERB off, MIX 100 %. */
    std::vector<float> erOnly (int type)
    {
        auto v = defaults();
        v[Index::type]        = (float) type;
        v[Index::ermode]      = (float) taps;
        v[Index::erdensity]   = 50.0f;
        v[Index::ervariation] = 4.0f;
        v[Index::erlevel]     = 0.0f;
        v[Index::verblevel]   = -40.0f;
        v[Index::mix]         = 100.0f;
        return v;
    }

    /** Sample `n` of a fixed white noise in -0.5..0.5, hashed from its index
        so that two instances fed "the same samples" really are, whatever
        block size or start point each one ran with. */
    float noiseAt (int n)
    {
        auto x = (std::uint32_t) n * 2654435761u + 0x9e3779b9u;
        x ^= x >> 15; x *= 0x2c1b3c6du;
        x ^= x >> 12; x *= 0x297a2d39u;
        x ^= x >> 15;
        return (float) ((double) x / 4294967296.0 - 0.5);
    }

    struct Stereo { std::vector<float> l, r; };

    /** Runs samples [from, to) of the noise through `dsp` in blocks of
        `block`, calling `before (at)` ahead of each block -- where a test
        moves a parameter -- and appends what comes out to `out`. `silent`
        feeds zeros over the same span instead. */
    void runNoise (ReverbDsp& dsp, Stereo& out, int from, int to, int block,
                   const std::function<void (int)>& before = {}, bool silent = false)
    {
        std::vector<float> l ((size_t) block), r ((size_t) block);

        for (int at = from; at < to; at += block)
        {
            const auto n = std::min (block, to - at);

            if (before)
                before (at);

            for (int i = 0; i < n; ++i)
                l[(size_t) i] = r[(size_t) i] = silent ? 0.0f : noiseAt (at + i);

            float* chans[] { l.data(), r.data() };
            dsp.process (chans, 2, n);

            out.l.insert (out.l.end(), l.begin(), l.begin() + n);
            out.r.insert (out.r.end(), r.begin(), r.begin() + n);
        }
    }

    /** Mean power of both channels over [from, to), in dB; -300 for silence. */
    double powerDb (const Stereo& s, int from, int to)
    {
        double p = 0.0;
        for (int i = from; i < to; ++i)
            p += (double) s.l[(size_t) i] * s.l[(size_t) i] + (double) s.r[(size_t) i] * s.r[(size_t) i];
        p /= 2.0 * (double) std::max (1, to - from);
        return p > 0.0 ? 10.0 * std::log10 (p) : -300.0;
    }

    /** The power of a - b over [from, to) against the power of a, in dB:
        how far two renders that should be the same sample for sample are
        from it. */
    double residualDb (const Stereo& a, const Stereo& b, int from, int to)
    {
        double d = 0.0, p = 0.0;
        for (int i = from; i < to; ++i)
        {
            const auto dl = (double) a.l[(size_t) i] - b.l[(size_t) i];
            const auto dr = (double) a.r[(size_t) i] - b.r[(size_t) i];
            d += dl * dl + dr * dr;
            p += (double) a.l[(size_t) i] * a.l[(size_t) i] + (double) a.r[(size_t) i] * a.r[(size_t) i];
        }
        return p > 0.0 ? 10.0 * std::log10 (std::max (d, 1.0e-300) / p) : 0.0;
    }

    /** The longest run of samples at or under `floor` in magnitude, in
        either channel, over [from, to). A floor of zero counts exact zeros;
        a small one counts near-silence too, which is what a silent table
        behind still-ringing band poles looks like. */
    int longestRunUnder (const Stereo& s, int from, int to, float floor)
    {
        int longest = 0, runL = 0, runR = 0;
        for (int i = from; i < std::min (to, (int) s.l.size()); ++i)
        {
            runL = std::abs (s.l[(size_t) i]) <= floor ? runL + 1 : 0;
            runR = std::abs (s.r[(size_t) i]) <= floor ? runR + 1 : 0;
            longest = std::max (longest, std::max (runL, runR));
        }
        return longest;
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
        check (erModeFor (2) == ErMode::taps && erModeFor (7) == ErMode::taps,
               "an out-of-range er detent, Blend's old 2 included, falls back to Taps");
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

    //==========================================================================
    //== M2: the early reflections. 11 section 6's ER block, in the ER-only
    //== condition: REVERB off, ER at 0 dB, MIX 100 %, hi-cut open.
    //==========================================================================

    //== The baked tables are the geometry's, to the printed precision ========
    //
    // This is what makes "re-seeded, not patched" enforceable: every row in
    // TapTables.h is re-derived here from `imagesource::geometryFor`, so a
    // number edited by hand fails the build.
    {
        bool match = true;

        for (int t = 0; t < kNumTapTypes; ++t)
        {
            const auto rows = imagesource::table (imagesource::geometryFor (t));

            if ((int) rows.size() != kNumReferenceTaps) { match = false; continue; }

            for (int k = 0; k < kNumReferenceTaps; ++k)
            {
                const auto& baked = kTypeTaps[t][k];
                match = match && std::abs (baked.timeMs - rows[(size_t) k].timeMs) <= 0.0006f
                              && std::abs (baked.gain   - rows[(size_t) k].gain)   <= 0.00006f
                              && std::abs (baked.pan    - rows[(size_t) k].pan)    <= 0.0006f
                              && baked.order == rows[(size_t) k].order;
            }

            check (imagesource::audit (rows)[0] == 0, "the generated table passes its own audit");
        }

        check (match, "every baked table matches the image-source generator to the printed precision");
        check (kNumTapTypes == numTypes, "the tap tables cover exactly the schema's types");
    }

    //== The comb rules, as assertions on the tables =============================
    {
        for (int t = 0; t < kNumTapTypes; ++t)
        {
            const auto* table = kTypeTaps[t];
            bool separated = true, distinct = true, ceilinged = true, kuttruff = true;

            for (int k = 1; k < kNumReferenceTaps; ++k)
                separated = separated && table[k].timeMs - table[k - 1].timeMs >= 0.9f;

            for (int i = 1; i < kNumReferenceTaps; ++i)
                for (int j = i + 1; j < kNumReferenceTaps; ++j)
                {
                    const auto gi = table[i].timeMs - table[i - 1].timeMs;
                    const auto gj = table[j].timeMs - table[j - 1].timeMs;
                    distinct = distinct && std::abs (gi - gj) / std::max (gi, gj) >= 0.02f;
                }

            for (int k = 0; k < kNumReferenceTaps; ++k)
            {
                ceilinged = ceilinged && db (table[k].gain) <= -15.3f + 0.01f;

                if (table[k].timeMs >= 2.0f && table[k].timeMs <= 20.0f)
                    kuttruff = kuttruff && db (table[k].gain) <= imagesource::kuttruffCeilingDb (table[k].timeMs, table[k].pan) + 0.01f;
            }

            check (separated, "no two taps closer than 0.9 ms");
            check (distinct,  "no two inter-tap gaps within 2 % of each other");
            check (ceilinged, "no single tap above -15.3 dB");
            check (kuttruff,  "taps in the 2-20 ms window obey Kuttruff's ceiling");
        }
    }

    //== ER taps: the engine plays the table ======================================
    //
    // At DENSITY minimum, hi-cut open, VARIATION 0: every core tap of every
    // type is found in the rendered IR within +-1 sample of the table and
    // within +-0.2 dB of its gain.
    {
        for (int t = 0; t < kNumTapTypes; ++t)
        {
            const auto ir = render ([t] (auto& v) { v[Index::type] = (float) t; });
            const auto* table = kTypeTaps[t];
            bool timesOk = true, gainsOk = true;

            for (int k = 0; k < kNumReferenceTaps; ++k)
            {
                const auto expected = ir.msToSamples (table[k].timeMs);
                const auto next     = k + 1 < kNumReferenceTaps ? ir.msToSamples (table[k + 1].timeMs) : expected + 40;
                const auto m        = measureTap (ir, expected - 2, std::min (expected + 40, next - 1));

                timesOk = timesOk && std::abs (m.sample - expected) <= 1;
                gainsOk = gainsOk && std::abs (db (m.gain) - db (table[k].gain)) <= 0.2f;

                if (! (std::abs (m.sample - expected) <= 1) || ! (std::abs (db (m.gain) - db (table[k].gain)) <= 0.2f))
                    std::cerr << "  type " << t << " tap " << k << ": expected " << expected << " / " << db (table[k].gain)
                              << " dB, got " << m.sample << " / " << db (m.gain) << " dB\n";
            }

            check (timesOk, "every core tap arrives within one sample of the table");
            check (gainsOk, "every core tap's gain is within 0.2 dB of the table");
        }

        // A tap inside 8 ms goes through the proximity band, whose one-pole
        // sits below 1.5 kHz: its peak sample is well under its DC sum.
        {
            ReverbDsp dsp;
            dsp.prepare (48000.0, 512, 2);
            auto v = defaults();
            dsp.setParams (v.data(), (int) v.size());
            check (dsp.getCore().earlyReflections().bandCutoffHz (0) < 1500.0f,
                   "the proximity band's one-pole is below 1.5 kHz");
            check (dsp.getCore().earlyReflections().bandCutoffHz (1) > dsp.getCore().earlyReflections().bandCutoffHz (3),
                   "higher orders are darker");
        }
    }

    //== Mono compatibility: gamma >= 0 at all seven VARIATION positions =========
    {
        float gamma[7] {};

        for (int v = 0; v < 7; ++v)
        {
            const auto ir = render ([v] (auto& p) { p[Index::ervariation] = (float) v; });
            gamma[v] = correlation (ir, ir.msToSamples (erSpanMsAt (0, roomDefaults::kSizeM) + 10.0f));
        }

        bool nonNegative = true, monotone = true;
        for (int v = 0; v < 7; ++v) nonNegative = nonNegative && gamma[v] >= -1.0e-4f;
        for (int v = 1; v < 6; ++v) monotone = monotone && gamma[v] <= gamma[v - 1] + 1.0e-4f;

        check (nonNegative, "gamma >= 0 at every VARIATION, so the mono loss never exceeds 3 dB");
        check (monotone, "gamma falls monotonically from VARIATION 0 to 5");
        check (gamma[0] >= 0.90f, "VARIATION 0 is nearly mono (gamma >= 0.9)");
        check (gamma[5] <= 0.15f, "VARIATION 5 is nearly decorrelated (gamma <= 0.15)");
        check (std::abs (gamma[6]) <= 0.03f, "VARIATION 6's complementary pair has gamma of zero");

        // VARIATION 6: the mono sum is exactly flat -- (M + D) + (M - D) = 2 M.
        {
            const auto ir = render ([] (auto& p) { p[Index::ervariation] = 6.0f; });
            const auto to = ir.msToSamples (erSpanMsAt (0, roomDefaults::kSizeM) + 20.0f);
            double sum = 0.0, each = 0.0;
            for (int i = 0; i < to; ++i)
            {
                const auto s = ir.l[(size_t) i] + ir.r[(size_t) i];
                sum  += s * s;
                each += ir.l[(size_t) i] * ir.l[(size_t) i] + ir.r[(size_t) i] * ir.r[(size_t) i];
            }
            check (std::abs (sum / each - 1.0) < 0.02, "VARIATION 6 sums to mono with the energy of one channel pair -- the comb pair cancels exactly");
        }

        for (int v = 0; v < 7; ++v)
            std::cout << "  gamma[" << v << "] = " << gamma[v] << '\n';
    }

    //== Flamming, on the rendered Room IR ========================================
    {
        const auto ir = render ([] (auto&) {});
        const auto span = erSpanMsAt (0, roomDefaults::kSizeM);

        // Each tap's energy as heard: the IR's energy between it and the next.
        const auto e25 = ir.energy (0, ir.msToSamples (25.0f));
        bool ruleI = true;
        for (int k = 0; k < kNumReferenceTaps; ++k)
        {
            const auto& t = kTypeTaps[0][k];
            if (t.timeMs > 25.0f)
            {
                const auto from = ir.msToSamples (t.timeMs) - 1;
                const auto to   = k + 1 < kNumReferenceTaps ? ir.msToSamples (kTypeTaps[0][k + 1].timeMs) - 1 : from + 200;
                ruleI = ruleI && ir.energy (from, to) <= e25 * std::pow (10.0f, -1.2f) * 1.02f;
            }
        }
        check (ruleI, "no tap after 25 ms is above -12 dB relative to the ER energy at 25 ms");

        std::vector<float> windows;
        for (float w = 0.0f; w < span + 5.0f; w += 5.0f)
            windows.push_back (ir.energy (ir.msToSamples (w), ir.msToSamples (w + 5.0f)));

        size_t peak = 0;
        for (size_t w = 1; w < windows.size(); ++w)
            if (windows[w] > windows[peak]) peak = w;

        // A window holding only the one-pole tail of the tap before it is
        // empty for this rule: under -30 dB re the peak window.
        bool ruleII = true;
        auto previous = windows[peak];
        for (size_t w = peak + 1; w < windows.size(); ++w)
        {
            if (windows[w] <= windows[peak] * 1.0e-3f) continue;
            if (windows[w] > previous * 1.02f)   // 2 % for the one-pole tails that spill into the next window
            {
                ruleII = false;
                std::cerr << "  window " << w * 5 << " ms rises: " << 10.0f * std::log10 (windows[w] / previous) << " dB over the last non-empty window\n";
            }
            previous = windows[w];
        }
        check (ruleII, "energy per 5 ms window never rises again after the peak window: no second onset");

        check (ir.energy (0, ir.msToSamples (30.0f)) >= 0.5f * ir.energy (0, ir.msToSamples (span + 5.0f)),
               "at least half the ER energy arrives before 30 ms");
    }

    //== Lateral energy at the default VARIATION ==================================
    //
    // ISO 3382-1's LF divides the lateral energy in 5-80 ms by the total
    // energy in 0-80 ms *including the direct sound*. With every tap held at
    // or under -15.3 dB (the colouration ceiling, 10 section 3) the whole
    // cluster is about a tenth of the direct sound's energy, so that figure
    // cannot reach 0.10 whatever the bearings do -- it is printed for the
    // record. What VARIATION actually controls is asserted instead: the
    // lateral fraction **of the ER bus itself**, in the same 0.10-0.35 range
    // 10 section 3 gives, at the default position. The lateral component is
    // (L - R)^2 / 2, which is what a figure-of-eight facing the source hears.
    {
        const auto ir = render ([] (auto& p) { p[Index::ervariation] = 2.0f; });
        double lateral = 0.0, er = 0.0;

        for (int i = ir.msToSamples (5.0f); i < ir.msToSamples (80.0f); ++i)
        {
            const auto d = ir.l[(size_t) i] - ir.r[(size_t) i];
            lateral += 0.5 * d * d;
        }
        for (int i = 0; i < ir.msToSamples (80.0f); ++i)
            er += ir.l[(size_t) i] * ir.l[(size_t) i] + ir.r[(size_t) i] * ir.r[(size_t) i];

        const auto lfIso = lateral / (er + 2.0);
        const auto lfEr  = lateral / er;
        std::cout << "  early lateral fraction at VARIATION 2: ISO (with direct) " << lfIso << ", of the ER bus " << lfEr << '\n';
        check (lfEr >= 0.10 && lfEr <= 0.35, "the ER bus's lateral fraction is 0.10-0.35 at the default VARIATION");
    }

    //== ER-only: self-terminating, ramped out, and flat ==========================
    {
        const auto ir = render ([] (auto&) {});
        const auto span = erSpanMsAt (0, roomDefaults::kSizeM);
        const auto er   = ir.energy (0, ir.msToSamples (span + 5.0f));
        const auto after = ir.energy (ir.msToSamples (span + 5.0f), ir.size());

        check (after <= er * 1.0e-6f, "energy after (ER span + 5 ms) is at least 60 dB below the ER energy");

        const auto& last = kTypeTaps[0][kNumReferenceTaps - 1];
        const auto& prev = kTypeTaps[0][kNumReferenceTaps - 2];
        check (last.gain < prev.gain && last.timeMs - prev.timeMs >= 5.0f,
               "the last tap ramps out: it is quieter than the one before and at least 5 ms after it");

        // 1/3-octave smoothed magnitude, 200 Hz - 10 kHz, at three densities.
        //
        // **The tilt is removed before the +-3 dB band is applied**: the
        // order-banded one-poles 10 section 3 asks for sit at 4-9 kHz and
        // darken every tap by design, so the raw response falls across the
        // top octave and no table could make it flat. What the rule is there
        // to catch is comb ripple, which is what is left after a straight
        // line in dB against log-frequency is taken out.
        //
        // **And it is asserted at DENSITY 100 %, not at 0 %.** Twenty-one
        // discrete taps in solo comb by their nature -- two equal taps 1.7 ms
        // apart are a comb with a 580 Hz period, and a 1/3-octave band at
        // 250 Hz is 46 Hz wide and cannot smooth it -- and that colouration
        // is exactly what the density bridge exists to take out (10 section
        // 3). The 0 % and 50 % figures print so the listening pass can see
        // what the taps alone do.
        const auto rippleAt = [] (const Ir& r, float spanMs)
        {
            std::vector<double> f, m;
            const auto to = r.msToSamples (spanMs + 10.0f);
            for (double hz = 200.0; hz <= 10000.0; hz *= std::pow (2.0, 1.0 / 3.0))
            {
                f.push_back (std::log2 (hz));
                m.push_back (thirdOctaveDb (r, hz, to));
            }

            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (size_t i = 0; i < f.size(); ++i) { sx += f[i]; sy += m[i]; sxx += f[i] * f[i]; sxy += f[i] * m[i]; }
            const auto n = (double) f.size();
            const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            const auto icept = (sy - slope * sx) / n;

            double lo = 1.0e9, hi = -1.0e9;
            for (size_t i = 0; i < f.size(); ++i)
            {
                const auto residual = m[i] - (icept + slope * f[i]);
                lo = std::min (lo, residual);
                hi = std::max (hi, residual);
            }
            return std::pair<double, double> { slope, hi - lo };
        };

        // Octave smoothing too, over the same range, so the owner can choose
        // the rule with both figures in front of them.
        const auto octaveRippleAt = [] (const Ir& r, float spanMs)
        {
            std::vector<double> f, m;
            const auto to = r.msToSamples (spanMs + 10.0f);
            for (double hz = 250.0; hz <= 8000.0; hz *= 2.0)
            {
                double p = 0.0;
                for (int k = -12; k <= 12; ++k)
                {
                    const auto fk = hz * std::pow (2.0, (double) k / 24.0);
                    const auto mag = magnitudeAt (r, fk, to);
                    p += mag * mag;
                }
                f.push_back (std::log2 (hz));
                m.push_back (10.0 * std::log10 (std::max (p / 25.0, 1.0e-24)));
            }

            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (size_t i = 0; i < f.size(); ++i) { sx += f[i]; sy += m[i]; sxx += f[i] * f[i]; sxy += f[i] * m[i]; }
            const auto n = (double) f.size();
            const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
            const auto icept = (sy - slope * sx) / n;

            double lo = 1.0e9, hi = -1.0e9, rawLo = 1.0e9, rawHi = -1.0e9;
            for (size_t i = 0; i < f.size(); ++i)
            {
                const auto residual = m[i] - (icept + slope * f[i]);
                lo = std::min (lo, residual); hi = std::max (hi, residual);
                rawLo = std::min (rawLo, m[i]); rawHi = std::max (rawHi, m[i]);
            }
            return std::pair<double, double> { hi - lo, rawHi - rawLo };
        };

        for (const auto density : { 0.0f, 50.0f, 100.0f })
        {
            const auto r = render ([density] (auto& p) { p[Index::erdensity] = density; });
            const auto [tilt, ripple] = rippleAt (r, span);
            const auto [octave, octaveRaw] = octaveRippleAt (r, span);
            std::cout << "  ER-only magnitude at DENSITY " << density << " %, 1/3-octave smoothed, 200 Hz-10 kHz: tilt "
                      << tilt << " dB/octave, ripple about the tilt " << ripple << " dB peak to peak; octave-smoothed 250 Hz-8 kHz: "
                      << octave << " dB about the tilt, " << octaveRaw << " dB raw\n";

            // **The rule, Frosty's on 2026-09-24:** octave-smoothed, 250 Hz to
            // 8 kHz, ripple about the tilt within 6 dB peak to peak, asserted
            // at DENSITY 100 % -- where the density bridge is meant to have
            // taken the discrete cluster's colour out -- and printed at 0 and
            // 50 %. The 1/3-octave figure 11 section 6 first asked for is
            // printed beside it: no sparse cluster meets +-3 dB under a 46 Hz
            // band at 200 Hz, and that is recorded in the M2 note.
            (void) ripple;

            if (density == 100.0f)
                check (octave <= 6.0, "ER-only magnitude at DENSITY 100 %, octave-smoothed 250 Hz-8 kHz, ripples within 6 dB about its tilt");
        }
    }

    //== Density: constant energy, no click, and 2000 pulses/s at the top ========
    {
        float e0 = 0.0f;
        bool constant = true, finite = true;

        for (int step = 0; step <= 20; ++step)
        {
            const auto d = (float) step * 5.0f;
            const auto ir = render ([d] (auto& p) { p[Index::erdensity] = d; });
            const auto span = erSpanMsAt (0, roomDefaults::kSizeM);
            const auto e = ir.energy (0, ir.msToSamples (span + 15.0f));

            if (step == 0) e0 = e;
            constant = constant && std::abs (10.0f * std::log10 (e / e0)) <= 0.2f;
            std::cout << "  DENSITY " << d << " %: " << 10.0f * std::log10 (e / e0) << " dB re DENSITY 0\n";

            for (int i = 0; i < ir.size(); ++i)
                finite = finite && std::isfinite (ir.l[(size_t) i]) && std::isfinite (ir.r[(size_t) i]);

            if (step == 20)
            {
                int pulses = 0;
                float peak = 0.0f;
                for (int i = 0; i < ir.msToSamples (span); ++i)
                    peak = std::max (peak, std::abs (ir.l[(size_t) i]));
                for (int i = 0; i < ir.msToSamples (span); ++i)
                    if (std::abs (ir.l[(size_t) i]) > peak * 0.001f)
                        ++pulses;
                const auto rate = (float) pulses / (span * 0.001f);
                std::cout << "  pulse rate at DENSITY 100 %: " << rate << " /s\n";
                check (rate >= 2000.0f, "at the top of DENSITY the pulse rate clears 2000/s");
            }
        }

        check (constant, "ER energy is constant within +-0.2 dB across the DENSITY sweep");
        check (finite, "every sample across the DENSITY sweep is finite");

        const auto jump = stepRatioAcross ([] (auto& p) { p[Index::erdensity] = 0.0f; },
                                           [] (auto& p) { p[Index::erdensity] = 100.0f; });
        std::cout << "  step ratio across a DENSITY jump 0 -> 100 %: " << jump.ratio << " (" << jump.edits
                  << " edit; settled at " << jump.settled << ")\n";
        check (jump.edits == 1, "the DENSITY jump was actually applied");
        check (jump.ratio <= 1.5f, "a DENSITY jump does not click");
    }

    //== ER hi-cut: -3 dB where it says, and no tap moves =========================
    {
        const auto open = render ([] (auto&) {});
        const auto to   = open.msToSamples (erSpanMsAt (0, roomDefaults::kSizeM) + 10.0f);

        for (const auto hz : { 2000.0f, 4000.0f, 8000.0f, 16000.0f })
        {
            // Two claims, separately. The design's corner is -3 dB where it
            // says: analytic, on the coefficient. And the running filter is
            // the design: the IR with the cut, against the IR with the cut
            // "open" at 20 kHz, drops at the corner by what the two designs'
            // magnitudes differ by -- the open setting is itself a pole and
            // not a wire, so a bare -3 dB against it would be the wrong
            // number to ask for.
            const auto coef   = ErGenerator::hiCutCoefFor (hz, 48000.0);
            const auto design = ErGenerator::hiCutMagnitudeDb (coef, hz, 48000.0);
            check (std::abs (design + 3.0103) <= 0.3, "the ER hi-cut's design is -3 dB at its corner, within 10 %");

            // 12 dB/octave (Frosty, 2026-09-26), against the one pole it
            // replaced at the same corner, an octave above and two.
            if (hz <= 4000.0f)
            {
                const auto one = ErGenerator::onePoleCoefFor (hz, 48000.0);
                for (const auto m : { 2.0, 4.0 })
                    std::cout << "  hi-cut " << hz << " Hz at " << m << "x: two poles "
                              << ErGenerator::hiCutMagnitudeDb (coef, m * hz, 48000.0) << " dB, one pole "
                              << ErGenerator::onePoleMagnitudeDb (one, m * hz, 48000.0) << " dB\n";

                // At four times the corner the pair is 4.2 dB (4 kHz) and
                // 5.0 dB (2 kHz) below the one pole it replaced, on ICE QUEEN
                // 2026-09-26; the knee is soft, so the gap keeps growing above.
                const auto gap = ErGenerator::hiCutMagnitudeDb (coef, 4.0 * hz, 48000.0)
                               - ErGenerator::onePoleMagnitudeDb (one, 4.0 * hz, 48000.0);
                check (gap <= -3.5, "the ER hi-cut is two poles: 3.5 dB or more under one pole at 4x its corner");
            }

            const auto cut = render ([hz] (auto& p) { p[Index::erhicut] = hz; });
            const auto measured = 20.0 * std::log10 (magnitudeAt (cut, hz, to) / magnitudeAt (open, hz, to));
            const auto expected = design - ErGenerator::hiCutMagnitudeDb (ErGenerator::hiCutCoefFor (20000.0f, 48000.0), hz, 48000.0);
            std::cout << "  hi-cut " << hz << " Hz: " << measured << " dB at the corner against the open pole, design " << expected << " dB\n";
            check (std::abs (measured - expected) <= 0.3, "the running ER hi-cut matches its design at the corner");

            // Cross-correlate: the lag of the largest correlation is 0, +-1.
            double best = -1.0; int lag = 99;
            for (int k = -3; k <= 3; ++k)
            {
                double c = 0.0;
                for (int i = 8; i < to - 8; ++i)
                    c += open.l[(size_t) i] * cut.l[(size_t) (i + k)];
                if (c > best) { best = c; lag = k; }
            }
            // Two poles have twice one pole's group delay: 2 samples at a
            // 2 kHz corner, 0.04 ms. That is the filter's own delay, not a
            // tap moving -- taps are 0.9 ms apart at the least -- so the
            // bound is +-2 since the hi-cut went to 12 dB/octave.
            std::cout << "  hi-cut " << hz << " Hz: peak lag " << lag << " samples\n";
            check (std::abs (lag) <= 2, "the hi-cut moves no tap (peak lag within its own group delay, +-2 samples)");
        }
    }

    //== Level laws ==============================================================
    {
        const auto ref = render ([] (auto&) {});
        const auto to  = ref.msToSamples (erSpanMsAt (0, roomDefaults::kSizeM) + 10.0f);
        const auto e0  = ref.energy (0, to);

        for (const auto level : { -6.0f, -12.0f, -24.0f })
        {
            const auto ir = render ([level] (auto& p) { p[Index::erlevel] = level; });
            const auto moved = 10.0f * std::log10 (ir.energy (0, to) / e0);
            check (std::abs (moved - level) <= 0.1f, "the ER fader moves the IR energy by exactly its dB figure");
        }

        {
            const auto ir = render ([] (auto& p) { p[Index::erlevel] = -40.0f; });
            check (ir.energy (0, to) <= e0 * 1.0e-10f, "ER at -40 is off, not -40 dB");
        }

        // MIX 50 %, both faders off: the output nulls against dry.
        {
            const auto ir = render ([] (auto& p) { p[Index::erlevel] = -40.0f; p[Index::mix] = 50.0f; });
            bool null = std::abs (ir.l[0] - 1.0f) <= 1.0e-4f;
            for (int i = 1; i < ir.size(); ++i)
                null = null && std::abs (ir.l[(size_t) i]) <= 1.0e-4f;
            check (null, "MIX 50 % with the wet faders off nulls against dry to -80 dB");
        }

        // The MIX law itself, Frosty's on 2026-09-24, at its five points: the
        // dry sample (sample 0, before any reflection) and the ER energy, each
        // against the ER-only render at MIX 100 %.
        {
            const auto erAt100 = ref.energy (1, to);   // after sample 0: the reflections alone

            struct Point { float mix, dry, wetDb; };
            for (const auto pt : { Point { 0.0f, 1.0f, -200.0f }, Point { 25.0f, 1.0f, -6.0206f },
                                   Point { 50.0f, 1.0f, 0.0f }, Point { 75.0f, 0.5f, 0.0f }, Point { 100.0f, 0.0f, 0.0f } })
            {
                const auto ir = render ([pt] (auto& p) { p[Index::mix] = pt.mix; });
                const auto wet = ir.energy (1, to);
                const auto wetDb = wet > 0.0f ? 10.0f * std::log10 (wet / erAt100) : -200.0f;
                check (std::abs (ir.l[0] - pt.dry) <= 1.0e-4f, "the MIX law's dry gain is what the law says at 0, 25, 50, 75 and 100 %");
                check (pt.wetDb <= -100.0f ? wet <= erAt100 * 1.0e-10f : std::abs (wetDb - pt.wetDb) <= 0.1f,
                       "the MIX law's wet gain is what the law says at 0, 25, 50, 75 and 100 %");
            }

            check (DspCore::dryGainFor (0.5f) == 1.0f && DspCore::wetGainFor (0.5f) == 1.0f,
                   "MIX 50 % is dry at unity and wet at unity: input unchanged, verb heard");
            check (DspCore::dryGainFor (1.0f) == 0.0f && DspCore::wetGainFor (1.0f) == 1.0f,
                   "MIX 100 % is verb only, for use as a send");
        }

        // OUTPUT is a plain trim.
        {
            const auto ir = render ([] (auto& p) { p[Index::output] = -12.0f; });
            check (std::abs (10.0f * std::log10 (ir.energy (0, to) / e0) + 12.0f) <= 0.1f, "OUTPUT trims the whole output");
        }
    }

    //== Block size: bit-identical for fixed parameters ===========================
    {
        const auto ref = render ([] (auto&) {}, 48000.0, 512, 0.25f);
        bool identical = true;

        for (const auto block : { 1, 16, 32, 64, 127, 2048 })
        {
            const auto ir = render ([] (auto&) {}, 48000.0, block, 0.25f);
            for (int i = 0; i < ref.size(); ++i)
                identical = identical && ir.l[(size_t) i] == ref.l[(size_t) i] && ir.r[(size_t) i] == ref.r[(size_t) i];
        }

        check (identical, "the IR is bit-identical at block sizes 1, 16, 32, 64, 127, 512 and 2048");
    }

    //== Sample rate: tap times in ms do not move ================================
    {
        for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        {
            const auto ir = render ([] (auto&) {}, rate, 512, 0.3f);
            bool ok = true;

            for (int k = 0; k < kNumReferenceTaps; ++k)
            {
                const auto expected = ir.msToSamples (kTypeTaps[0][k].timeMs);
                const auto next     = k + 1 < kNumReferenceTaps ? ir.msToSamples (kTypeTaps[0][k + 1].timeMs) : expected + 40;
                const auto m        = measureTap (ir, expected - 2, std::min (expected + 40, next - 1));
                ok = ok && std::abs ((double) m.sample / rate * 1000.0 - kTypeTaps[0][k].timeMs) <= 0.1;
            }

            check (ok, "tap times in ms are within 0.1 ms at every rate");
        }
    }

    //== Parameter changes do not click ==========================================
    //
    // **What these measure, since the edit really runs (2026-09-30):** the
    // largest sample-to-sample step from the change on, against the largest
    // the steady 1 kHz ER output had before it. So a check passes when the
    // change makes no step bigger than the signal's own biggest, and it
    // cannot see a click smaller than that. Where the new setting is quieter
    // -- Hall, 24 m, Energy, VARIATION 6 are all quieter than Room at 1 kHz --
    // the largest step in the window is one of the few samples before the
    // edit lands and the ratio sits just under one whatever the change did.
    // VARIATION 6 read 1.76 here once the edit ran and before the comb line
    // was kept current: entering 6 read an empty line, and that was a step.
    {
        const auto type = stepRatioAcross ([] (auto&) {}, [] (auto& p) { p[Index::type] = (float) hall; });
        const auto size = stepRatioAcross ([] (auto&) {}, [] (auto& p) { p[Index::size] = 24.0f; });
        const auto mode = stepRatioAcross ([] (auto&) {}, [] (auto& p) { p[Index::ermode] = (float) energy; });
        const auto var  = stepRatioAcross ([] (auto&) {}, [] (auto& p) { p[Index::ervariation] = 6.0f; });
        std::cout << "  step ratios (settled level in brackets): TYPE " << type.ratio << " (" << type.settled << "), SIZE "
                  << size.ratio << " (" << size.settled << "), ER MODE " << mode.ratio << " (" << mode.settled
                  << "), VARIATION " << var.ratio << " (" << var.settled << ")\n";
        check (type.edits == 1 && size.edits == 1 && mode.edits == 1 && var.edits == 1,
               "each parameter change was actually applied, once");
        check (type.ratio <= 1.5f, "a TYPE switch dips and swaps without a click");
        check (size.ratio <= 1.5f, "a SIZE jump crossfades without a click");
        check (mode.ratio <= 1.5f, "an ER MODE change crossfades without a click");
        check (var.ratio  <= 1.5f, "a VARIATION change crossfades without a click");
    }

    //== A built table is never left unweighted ===================================
    //
    // The review of PR #27, 2026-09-30, measured exact-zero output for good on
    // three paths with one cause: `rebuild()` zeroed every tap's weight and
    // left the weighing to the next block's density update, which skips when
    // DENSITY has not moved. A second prepare() with nothing changed, a fresh
    // instance whose first block is longer than the TYPE dip, and a TYPE
    // change at a 4096 block all ended silent; at small blocks the same defect
    // was a dropout inside every TYPE change. Each path is asserted here, in
    // the ER-only setting the review used.
    {
        // (a) A second prepare() plays exactly what a fresh instance does, at
        // every type -- straight after the first at 48 kHz / 512, and after a
        // reset() at 44.1 kHz / 256.
        struct Case { double rate; int block; bool resetFirst; };
        for (const auto c : { Case { 48000.0, 512, false }, Case { 44100.0, 256, true } })
        {
            bool same = true;
            const auto n = (int) (0.5 * c.rate);

            for (int t = 0; t < numTypes; ++t)
            {
                const auto v = erOnly (t);

                ReverbDsp fresh;
                fresh.prepare (c.rate, c.block, 2);
                fresh.setParams (v.data(), (int) v.size());
                Stereo a;
                runNoise (fresh, a, 0, n, c.block);

                ReverbDsp again;
                again.prepare (c.rate, c.block, 2);
                again.setParams (v.data(), (int) v.size());
                Stereo warm;
                runNoise (again, warm, 0, n / 2, c.block);
                if (c.resetFirst)
                    again.reset();
                again.prepare (c.rate, c.block, 2);
                again.setParams (v.data(), (int) v.size());
                Stereo b;
                runNoise (again, b, 0, n, c.block);

                const auto pa = powerDb (a, n / 2, n), pb = powerDb (b, n / 2, n);
                if (! (pa > -100.0 && std::abs (pa - pb) <= 0.01))
                {
                    same = false;
                    std::cerr << "  " << kTypeNames[t] << " at " << c.rate << " / " << c.block << (c.resetFirst ? " after reset()" : "")
                              << ": fresh " << pa << " dB, prepared twice " << pb << " dB\n";
                }
            }

            check (same, c.resetFirst ? "reset() then prepare() plays what a fresh instance does, within 0.01 dB, at every type"
                                      : "a second prepare() plays what a fresh instance does, within 0.01 dB, at every type");
        }

        // (b) A fresh instance plays at every block size, and plays the same
        // thing: the TYPE dip of a first block that is not Room must not
        // finish inside a block that also built the table.
        {
            bool playing = true, invariant = true;
            const auto n = 48000;

            for (int t = 0; t < numTypes; ++t)
            {
                double reference = 0.0;

                for (const auto block : { 64, 512, 2048, 3000, 4096, 8192 })
                {
                    const auto v = erOnly (t);
                    ReverbDsp dsp;
                    dsp.prepare (48000.0, block, 2);
                    dsp.setParams (v.data(), (int) v.size());
                    Stereo s;
                    runNoise (dsp, s, 0, n, block);

                    const auto p = powerDb (s, n / 2, n);
                    if (block == 64)
                        reference = p;

                    if (! (p > -100.0))
                        playing = false;
                    if (! (std::abs (p - reference) <= 0.01))
                        invariant = false;

                    if (! (p > -100.0) || ! (std::abs (p - reference) <= 0.01))
                        std::cerr << "  fresh " << kTypeNames[t] << " at block " << block << ": " << p
                                  << " dB against " << reference << " dB at block 64\n";
                }
            }

            check (playing, "a fresh instance is not silent at any type or block size, 64 to 8192");
            check (invariant, "a fresh instance's level is the same at every block size, within 0.01 dB");
        }

        // (c) A running Room switched to Hall at a 4096 block boundary
        // settles at Hall's own level -- against a fresh Hall fed the same
        // noise, so the two match sample for sample once the line has turned
        // over.
        {
            const auto block = 4096, n = 72000;
            const auto roomV = erOnly (room), hallV = erOnly (hall);

            ReverbDsp dsp;
            dsp.prepare (48000.0, block, 2);
            dsp.setParams (roomV.data(), (int) roomV.size());
            Stereo s;
            runNoise (dsp, s, 0, n, block, [&] (int at) {
                if (at >= 24000)
                    dsp.setParams (hallV.data(), (int) hallV.size());
            });

            ReverbDsp ref;
            ref.prepare (48000.0, 64, 2);
            ref.setParams (hallV.data(), (int) hallV.size());
            Stereo h;
            runNoise (ref, h, 0, n, 64);

            const auto p = powerDb (s, 48000, n), ph = powerDb (h, 48000, n);
            std::cout << "  Room -> Hall at block 4096: " << p << " dB one second on, a fresh Hall " << ph << " dB\n";
            check (p > -100.0 && std::abs (p - ph) <= 0.01,
                   "a TYPE change at a 4096 block recovers to the new type's steady level, within 0.01 dB");
        }

        // (d) And inside the change, the only exact zero is the designed
        // one: the dip's raised cosine touches zero at its midpoint, one
        // sample, and the new table plays from the next. The review measured
        // 546 samples of exact zero at block 2048 and 34 at 256 and 512, with
        // the switch landing elsewhere in the block than it does here.
        //
        // **Exact zero alone undercounts the dropout**: the old set's band
        // poles and the hi-cut go on ringing out after its weights are gone,
        // so a silent table reads as tiny non-zero samples for the first
        // fifty or so. The near-silence run under -100 dBFS is asserted too,
        // against what the designed dip alone gives: its gain is under 2e-4
        // -- a -25 dBFS noise under -100 dBFS -- for about 13 samples either
        // side of the midpoint, so 48 samples (1 ms) is the dip with room to
        // spare and not a dropout.
        {
            bool short_ = true, quiet = true;

            for (const auto block : { 256, 512, 2048 })
            {
                const auto roomV = erOnly (room), hallV = erOnly (hall);
                ReverbDsp dsp;
                dsp.prepare (48000.0, block, 2);
                dsp.setParams (roomV.data(), (int) roomV.size());
                Stereo s;
                int switchedAt = -1;
                runNoise (dsp, s, 0, 48000, block, [&] (int at) {
                    if (at >= 24000 && switchedAt < 0)
                    {
                        switchedAt = at;
                        dsp.setParams (hallV.data(), (int) hallV.size());
                    }
                });

                const auto run  = longestRunUnder (s, switchedAt, switchedAt + 9600, 0.0f);
                const auto hush = longestRunUnder (s, switchedAt, switchedAt + 9600, 1.0e-5f);
                std::cout << "  Room -> Hall at block " << block << ": longest exact-zero run " << run
                          << " samples, longest under -100 dBFS " << hush << " samples\n";
                short_ = short_ && run <= 1;
                quiet  = quiet && hush <= 48;
            }

            check (short_, "a TYPE change outputs exact zero for at most the dip's one midpoint sample, at blocks 256, 512 and 2048");
            check (quiet, "a TYPE change is under -100 dBFS for no longer than the dip itself (48 samples), at blocks 256, 512 and 2048");
        }
    }

    //== VARIATION 6 never replays what it heard before it was left ==============
    //
    // The review's experiment, exactly: a burst at VARIATION 6, then 5 through
    // a second of silent input, then back to 6 with the input still silent.
    // The comb pair's delay line was written only while VARIATION was 6, so
    // coming back read the burst out of it a second after it ended -- 0.112
    // peak, -19 dBFS, from ~64 samples to +39 ms. Silence in has to be
    // silence out.
    {
        for (const auto away : { 5, 4 })
        {
            auto v = erOnly (room);
            v[Index::ervariation] = 6.0f;

            ReverbDsp dsp;
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v.data(), (int) v.size());

            Stereo s;
            runNoise (dsp, s, 0, 9600, 512);                                    // the burst, at 6

            v[Index::ervariation] = (float) away;
            dsp.setParams (v.data(), (int) v.size());
            runNoise (dsp, s, 9600, 9600 + 48000, 512, {}, true);               // a second of silence, away

            v[Index::ervariation] = 6.0f;
            dsp.setParams (v.data(), (int) v.size());
            const auto back = (int) s.l.size();
            runNoise (dsp, s, back, back + 9600, 512, {}, true);                // back at 6, still silent

            float peak = 0.0f;
            for (int i = back; i < (int) s.l.size(); ++i)
                peak = std::max (peak, std::max (std::abs (s.l[(size_t) i]), std::abs (s.r[(size_t) i])));

            std::cout << "  VARIATION 6 -> " << away << " -> 6 through silence: peak " << db (peak) << " dBFS after returning\n";
            check (peak <= 1.0e-6f, "returning to VARIATION 6 through silence is silent (below -120 dBFS): no stale comb audio");
        }
    }

    //== A finished crossfade leaves the diffuser's normaliser on the new set =====
    //
    // `weigh()` updates the per-band powers the diffuser's normaliser is built
    // from only for the active set, and the crossfade flips which set that is
    // without weighing it again -- so a table change at a steady DENSITY kept
    // the old set's powers, and the old set's normaliser, for good. Against a
    // fresh instance fed the same noise, the two must agree sample for sample
    // once the line has turned over.
    {
        struct Move { const char* what; int index; float value; };
        bool same = true;

        for (const auto m : { Move { "VARIATION 4 -> 0", Index::ervariation, 0.0f },
                              Move { "VARIATION 4 -> 5", Index::ervariation, 5.0f },
                              Move { "ER MODE Taps -> Energy", Index::ermode, (float) energy },
                              Move { "SIZE -> 30 m", Index::size, 30.0f } })
        {
            auto v = erOnly (room);
            v[Index::erdensity] = 100.0f;      // all three diffuser stages in
            auto to = v;
            to[(size_t) m.index] = m.value;

            ReverbDsp dsp;
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v.data(), (int) v.size());
            Stereo s;
            runNoise (dsp, s, 0, 72000, 512, [&] (int at) {
                if (at >= 24000)
                    dsp.setParams (to.data(), (int) to.size());
            });

            ReverbDsp ref;
            ref.prepare (48000.0, 512, 2);
            ref.setParams (to.data(), (int) to.size());
            Stereo h;
            runNoise (ref, h, 0, 72000, 512);

            const auto r = residualDb (h, s, 48000, 72000);
            std::cout << "  " << m.what << " at DENSITY 100: " << r << " dB residual against a fresh instance there\n";
            same = same && r <= -100.0;
        }

        check (same, "after a table crossfade the output is a fresh instance's, sample for sample (residual under -100 dB)");
    }

    //== reset() in the middle of a crossfade finishes the move ===================
    //
    // reset() cleared the fade flag without flipping to the set the fade was
    // going to, and the config was already recorded as current -- so the old
    // table went on playing until something else changed. The same for a
    // TYPE dip cut short before its midpoint, which had not built the new
    // table yet.
    //
    // The bar is -80 dB and not the -100 the crossfade check above uses:
    // **a reset() instance and a fresh one differ by -86 dB even when nothing
    // moved at all** (measured on AURORA, 2026-09-30, and steady from 200 ms
    // on), because reset() snaps the smoothed values -- the ER hi-cut's
    // coefficient, the fader gains -- to their targets, where a fresh
    // instance glides there from its construction defaults and, in float,
    // stalls a few hundred ulps short. (Holding the hi-cut's glide alone
    // moved the floor to -91 dB.) The old table playing on measured +0.18 dB.
    {
        struct Move { const char* what; int index; float value; };
        bool landed = true;

        for (const auto m : { Move { "a VARIATION crossfade", Index::ervariation, 0.0f },
                              Move { "a TYPE dip", Index::type, (float) hall } })
        {
            auto v = erOnly (room);
            auto to = v;
            to[(size_t) m.index] = m.value;

            ReverbDsp dsp;
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v.data(), (int) v.size());
            Stereo warm;
            runNoise (dsp, warm, 0, 24000, 512);
            dsp.setParams (to.data(), (int) to.size());
            runNoise (dsp, warm, 24000, 24064, 64);        // 64 samples into a 1440-sample move
            dsp.reset();
            Stereo s;
            runNoise (dsp, s, 0, 24000, 512);

            ReverbDsp ref;
            ref.prepare (48000.0, 512, 2);
            ref.setParams (to.data(), (int) to.size());
            Stereo h;
            runNoise (ref, h, 0, 24000, 512);

            const auto r = residualDb (h, s, 14400, 24000);
            std::cout << "  reset() in " << m.what << ": " << r << " dB residual against a fresh instance at the new setting\n";
            landed = landed && r <= -80.0;
        }

        check (landed, "reset() during a crossfade or a TYPE dip lands on the new setting, not the old one (residual under -80 dB)");
    }

    //== Energy: finite, and energy-renormalised to the room =====================
    {
        const auto taps  = render ([] (auto&) {});
        const auto to    = taps.msToSamples (600.0f);
        const auto eTaps = taps.energy (0, to);

        const auto ir = render ([] (auto& p) { p[Index::ermode] = (float) energy; });
        bool finite = true;
        for (int i = 0; i < ir.size(); ++i)
            finite = finite && std::isfinite (ir.l[(size_t) i]) && std::isfinite (ir.r[(size_t) i]);
        check (finite, "Energy mode is finite");

        const auto ratio = 10.0f * std::log10 (ir.energy (0, to) / eTaps);
        std::cout << "  Energy vs Taps: " << ratio << " dB\n";
        check (std::abs (ratio) <= 1.0f, "Energy carries the room's core energy within 1 dB");
    }

    //== NaN / silence ==============================================================
    {
        bool finite = true;

        for (int t = 0; t < numTypes; ++t)
            for (const auto density : { 0.0f, 100.0f })
                for (const auto v : { 0, 6 })
                {
                    ReverbDsp dsp;
                    dsp.prepare (48000.0, 256, 2);
                    auto p = defaults();
                    p[Index::type] = (float) t;
                    p[Index::erdensity] = density;
                    p[Index::ervariation] = (float) v;
                    p[Index::size] = t % 2 ? 80.0f : 0.5f;
                    dsp.setParams (p.data(), (int) p.size());

                    std::vector<float> l (256), r (256);
                    for (int block = 0; block < 40; ++block)
                    {
                        for (int i = 0; i < 256; ++i)
                        {
                            const auto x = block < 10 ? (i % 48 < 24 ? 1.0f : -1.0f)   // a +-1 square
                                         : block < 20 ? 1.0f                              // a DC step
                                         : block < 30 ? 1.0e-38f                          // denormal-range input
                                                      : 0.0f;
                            l[(size_t) i] = r[(size_t) i] = x;
                        }
                        float* chans[] { l.data(), r.data() };
                        dsp.process (chans, 2, 256);
                        for (int i = 0; i < 256; ++i)
                            finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    }
                }

        check (finite, "every sample is finite over squares, DC, denormal input and silence at every type and both size ends");

        // After reset(), zeros in gives exactly zeros out.
        {
            ReverbDsp dsp;
            dsp.prepare (48000.0, 256, 2);
            auto p = defaults();
            dsp.setParams (p.data(), (int) p.size());
            std::vector<float> l (256, 1.0f), r (256, 1.0f);
            float* chans[] { l.data(), r.data() };
            dsp.process (chans, 2, 256);
            dsp.reset();
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            bool zeros = true;
            for (int block = 0; block < 4; ++block)
            {
                dsp.process (chans, 2, 256);
                for (int i = 0; i < 256; ++i)
                    zeros = zeros && l[(size_t) i] == 0.0f && r[(size_t) i] == 0.0f;
            }
            check (zeros, "after reset(), zeros in gives exactly zeros out");
        }
    }

    //== Buses: a mono bus is finite and within 3 dB of a stereo channel ===========
    {
        const auto stereo = render ([] (auto&) {});
        const auto mono   = render ([] (auto&) {}, 48000.0, 512, 0.6f, 1);
        const auto to     = stereo.msToSamples (erSpanMsAt (0, roomDefaults::kSizeM) + 10.0f);

        double eStereo = 0.0, eMono = 0.0;
        for (int i = 1; i < to; ++i)
        {
            eStereo += stereo.l[(size_t) i] * stereo.l[(size_t) i];
            eMono   += mono.l[(size_t) i] * mono.l[(size_t) i];
        }

        const auto diff = 10.0 * std::log10 (eMono / eStereo);
        std::cout << "  mono bus vs one stereo channel: " << diff << " dB\n";
        check (std::abs (diff) <= 3.0, "a mono bus hears the ER within 3 dB of a stereo channel");
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

    //== The tap tables' shape ===============================================
    //
    // What the panel and the engine both rely on being true of every table:
    // times ascend, the gain contour falls, every bearing is a bearing, and
    // the first two reflections stay near the centre so the phantom centre
    // holds. Gains are *not* asserted to fall tap by tap: the level ceilings
    // flatten the early cluster to one figure, so what falls is the contour
    // -- the least-squares slope of dB against time -- and the last tap.
    {
        for (int t = 0; t < kNumTapTypes; ++t)
        {
            const auto* table = kTypeTaps[t];
            bool ascending = true, panned = true, ordered = true;
            float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;

            for (int i = 0; i < kNumReferenceTaps; ++i)
            {
                panned  = panned  && table[i].pan >= -1.0f && table[i].pan <= 1.0f;
                ordered = ordered && table[i].order >= 1 && table[i].order <= 3;

                if (i > 0)
                    ascending = ascending && table[i].timeMs > table[i - 1].timeMs;

                const auto y = 20.0f * std::log10 (table[i].gain);
                sx += table[i].timeMs; sy += y; sxx += table[i].timeMs * table[i].timeMs; sxy += table[i].timeMs * y;
            }

            const auto n = (float) kNumReferenceTaps;
            const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);

            check (ascending, "tap times ascend");
            check (slope < 0.0f, "the gain contour falls with time");
            check (table[kNumReferenceTaps - 1].gain < table[0].gain, "the last tap is quieter than the first");
            check (panned, "every tap's bearing is within -1..+1");
            check (ordered, "every tap's order is 1..3");
            check (std::abs (table[0].pan) < 0.25f && std::abs (table[1].pan) < 0.25f,
                   "the first two reflections stay near the centre");
            check (table[0].timeMs < 5.0f, "a deliberate allocation inside 5 ms");
        }

        check (kNumReferenceTaps == 21, "the base tap count is 21, as 10 section 3 gives it");
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

        // **t_ER,max is the span of the mode in use.** Energy mode lays its
        // pulses over its own window, 3.1 x ER SPREAD up to 500 ms, and not
        // over the Taps table's 61 ms; the formula added the Taps span in
        // both modes until the 2026-09-30 review, so at a short DECAY a
        // bounce cut most of an Energy cluster off.
        //   0 + 1.8 * 1.20 + 500 ms + 0.05 = 2.71
        DspCore::Params spread = p;
        spread.erMode     = ErMode::energy;
        spread.erSpreadMs = 200.0f;
        check (near (DspCore::tailSecondsFor (spread), 2.71f, 1.0e-3f),
               "Energy mode's tail carries its own 500 ms window at ER SPREAD 200");

        // And the figure covers what actually plays, in both modes, at the
        // shortest DECAY -- where the ER span is most of the answer.
        for (const auto mode : { taps, energy })
        {
            const auto edit = [mode] (std::vector<float>& v)
            {
                v[Index::ermode]  = (float) mode;
                v[Index::erspread] = 200.0f;
                v[Index::decay]   = 0.1f;
            };

            const auto ir = render (edit, 48000.0, 512, 1.0f);

            auto v = defaults();
            edit (v);
            ReverbDsp dsp;
            const auto tail = dsp.tailSecondsForParams (v.data(), (int) v.size());
            const auto cut  = (int) (tail * 48000.0);
            const auto all  = ir.energy (0, ir.size());
            const auto past = ir.energy (cut, ir.size());

            std::cout << "  reported tail in " << (mode == taps ? "Taps" : "Energy") << " mode at DECAY 0.1 s: " << tail
                      << " s; ER energy past it " << 10.0f * std::log10 (std::max (past, 1.0e-30f) / all) << " dB\n";
            check (past <= all * 1.0e-6f,
                   "the reported tail covers the rendered ER to -60 dB in both modes at DECAY 0.1 s");
        }
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

    //== The early reflections stop getting louder below a 6 m room ===========
    //
    // A tap's gain is 1 / d, so halving the room doubles it, and SIZE reaches
    // 0.5 m. The review of PR #27 measured what that does at the settings a
    // user actually has -- each type's own voicing, MIX at its default, pink
    // noise at -18 dBFS RMS -- and the output went over full scale under
    // about 4 m and reached +15 to +18 dBFS at 0.5 m. Nothing below 6 m was in
    // either listening set, so Frosty's call on 2026-10-01 was to hold the
    // level at its 6 m figure for every smaller room: everything he heard is
    // untouched, and a small room is still earlier and tighter, because the
    // tap *times* go on scaling. These three checks are that decision.
    {
        const auto& first = kTypeTaps[room][0];

        // (b) The law itself: untouched from 6 m up, flat below.
        bool lawAbove = true, flatBelow = true, stillEarlier = true;

        for (const auto size : { 6.0f, 8.0f, 12.0f, 24.0f, 80.0f })
            lawAbove = lawAbove && near (tapGainAt (first, size), first.gain * kReferenceSizeM / size, 1.0e-6f);

        for (const auto size : { 0.5f, 1.0f, 2.0f, 3.0f, 5.9f })
        {
            flatBelow    = flatBelow && near (tapGainAt (first, size), tapGainAt (first, kGainFloorSizeM), 1.0e-6f);
            stillEarlier = stillEarlier && tapTimeMsAt (first, size) < tapTimeMsAt (first, kGainFloorSizeM);
        }

        check (lawAbove,     "at 6 m and above a tap's gain is the 1/d law, exactly as it was heard");
        check (flatBelow,    "below 6 m a tap's gain is its 6 m gain, so a smaller room is not a louder one");
        check (stillEarlier, "below 6 m a tap still arrives earlier, so a smaller room is still a tighter one");

        // A fixed pink noise at -18 dBFS RMS, two seconds: the file's own
        // hashed white noise through the usual three-decade pinking filter, so
        // every platform runs the same samples.
        const auto n = 96000;
        std::vector<float> pink ((size_t) n);
        {
            double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, sum = 0;

            for (int i = 0; i < n; ++i)
            {
                const auto w = (double) noiseAt (i);
                b0 = 0.99886 * b0 + w * 0.0555179;  b1 = 0.99332 * b1 + w * 0.0750759;
                b2 = 0.96900 * b2 + w * 0.1538520;  b3 = 0.86650 * b3 + w * 0.3104856;
                b4 = 0.55000 * b4 + w * 0.5329522;  b5 = -0.7616 * b5 - w * 0.0168980;
                const auto p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
                b6 = w * 0.115926;
                pink[(size_t) i] = (float) p;
                sum += p * p;
            }

            const auto k = std::pow (10.0, -18.0 / 20.0) / std::sqrt (sum / (double) n);
            for (auto& s : pink)
                s = (float) ((double) s * k);
        }

        /** Peak of both channels, in dBFS, of the pink noise through `v`. */
        const auto peakThrough = [&pink, n] (const std::vector<float>& v)
        {
            ReverbDsp dsp;
            dsp.prepare (48000.0, 512, 2);
            dsp.setParams (v.data(), (int) v.size());

            std::vector<float> l (512), r (512);
            float peak = 0.0f;

            for (int at = 0; at < n; at += 512)
            {
                const auto count = std::min (512, n - at);

                for (int i = 0; i < count; ++i)
                    l[(size_t) i] = r[(size_t) i] = pink[(size_t) (at + i)];

                float* chans[] { l.data(), r.data() };
                dsp.setParams (v.data(), (int) v.size());
                dsp.process (chans, 2, count);

                for (int i = 0; i < count; ++i)
                    peak = std::max (peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
            }

            return db (peak);
        };

        // (a) At the settings a user has, no type goes over full scale from 2 m
        // up, and none goes more than 2 dB over it anywhere. The cap does not
        // make the bottom of the knob perfectly level: with the gains held and
        // the times still shrinking, the taps bunch up and sum more coherently
        // in the bass, which is worth up to 7 dB at 0.5 m on a bass-heavy
        // signal. Before the cap the same corner read +18.6 dBFS; it now reads
        // +1.8, and that residue is recorded as an open point for the owner
        // rather than hidden behind a looser check.
        float worst = -300.0f, worstFromTwo = -300.0f;
        std::string worstAt;

        for (int t = 0; t < numTypes; ++t)
            for (const auto size : { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 24.0f, 80.0f })
            {
                auto v = defaults();
                v[Index::type] = (float) t;

                for (const auto& s : typeSettings (t))
                    if (const auto index = indexOfParam (specs(), s.id); index >= 0)
                        v[(size_t) index] = s.value;

                v[Index::size] = size;

                const auto peak = peakThrough (v);

                if (size >= 2.0f)
                    worstFromTwo = std::max (worstFromTwo, peak);

                if (peak > worst)
                {
                    worst   = peak;
                    worstAt = std::string (kTypeNames[t]) + " at " + std::to_string (size) + " m";
                }
            }

        if (! (worstFromTwo < 0.0f && worst <= 2.0f))
            std::cerr << "  worst output peak " << worst << " dBFS, " << worstAt
                      << "; worst from 2 m up " << worstFromTwo << " dBFS\n";

        check (worstFromTwo < 0.0f,
               "pink noise at -18 dBFS RMS stays under full scale at every type's own voicing from 2 m up");
        check (worst <= 2.0f,
               "and is never more than 2 dB over full scale at any SIZE, where it was 18.6 dB over");

        // (c) Smaller, and no longer much louder: with the ER alone, the peak a
        // small room makes is within 7.5 dB of the 6 m room's (measured: up to
        // 6.9 dB, Plate at 0.5 m), where the uncapped law put 0.5 m 23 to 28 dB
        // above it. What is left is the bunching described under (a).
        bool held = true;

        for (int t = 0; t < numTypes; ++t)
        {
            auto v = erOnly (t);
            v[Index::size] = kGainFloorSizeM;
            const auto atFloor = peakThrough (v);

            for (const auto size : { 0.5f, 1.0f, 2.0f, 3.0f })
            {
                v[Index::size] = size;
                const auto rise = peakThrough (v) - atFloor;

                if (! (rise <= 7.5f))
                {
                    held = false;
                    std::cerr << "  " << kTypeNames[t] << " at " << size << " m: " << rise << " dB above its 6 m peak\n";
                }
            }
        }

        check (held, "the ER alone at 0.5 to 3 m peaks within 7.5 dB of its 6 m peak, where it was up to 28 dB above");
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

    if (failures == 0)
        std::cout << "reverb_dsp: all checks passed\n";

    return failures == 0 ? 0 : 1;
}
