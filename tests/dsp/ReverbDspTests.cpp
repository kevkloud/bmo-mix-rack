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

#include "modules/reverb/dsp/ReverbDsp.h"
#include "modules/reverb/dsp/TapTables.h"

#include <cmath>
#include <iostream>
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
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        auto v = defaults();

        v[Index::type]        = (float) plate;
        v[Index::size]        = 33.0f;
        v[Index::predelay]    = 72.0f;
        v[Index::prelink]     = 1.0f;
        v[Index::decay]       = 4.25f;
        v[Index::decayshape]  = 1.20f;
        v[Index::attack]      = 55.0f;
        v[Index::feed]        = 40.0f;
        v[Index::damplofreq]  = 320.0f;
        v[Index::damplo]      = 1.55f;
        v[Index::damphifreq]  = 1900.0f;
        v[Index::damphi]      = 0.65f;
        v[Index::eqlofreq]    = 140.0f;
        v[Index::eqlo]        = -6.0f;
        v[Index::eqhifreq]    = 1400.0f;
        v[Index::eqhi]        = 4.5f;
        v[Index::ermode]      = (float) energy;
        v[Index::erdensity]   = 82.0f;
        v[Index::ershape]     = 2.40f;
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
        check (p.linkEr, "link er");
        check (near (p.decaySeconds, 4.25f), "decay");
        check (near (p.decayShape, 1.20f), "decay shape");
        check (near (p.dampLoFreqHz, 320.0f), "low knee");
        check (near (p.dampLo, 1.55f), "low multiplier");
        check (near (p.dampHiFreqHz, 1900.0f), "high knee");
        check (near (p.dampHi, 0.65f), "high multiplier");
        check (near (p.eqLoFreqHz, 140.0f), "eq low freq");
        check (near (p.eqLoDb, -6.0f), "eq low");
        check (near (p.eqHiFreqHz, 1400.0f), "eq high freq");
        check (near (p.eqHiDb, 4.5f), "eq high");
        check (p.erMode == ErMode::energy, "er mode");
        check (near (p.erShape, 2.40f), "er shape");
        check (near (p.erSpreadMs, 125.0f), "er spread");
        check (near (p.erHiCutHz, 4500.0f), "er hi-cut");
        check (p.erVariation == 5, "variation");
        check (near (p.modDepthMs, 0.55f), "mod depth");
        check (near (p.modRateHz, 0.90f), "mod rate");
        check (near (p.inHiCutHz, 9000.0f), "in hi-cut");
        check (near (p.erLevelDb, -18.5f), "er level");
        check (near (p.verbLevelDb, -3.5f), "reverb level");
        check (near (p.outputDb, -7.5f), "output");

        // **The four conversions, which are the only places a host value is
        // not already an engine value.** They are in the adapter and nowhere
        // else, which is the thing worth pinning: a percentage that reached
        // the engine as 82 instead of 0.82 would be a hundredfold error in a
        // control that looks fine on the panel.
        check (near (p.attack, 0.55f), "attack arrives as 0..1, not as per cent");
        check (near (p.feed, 0.40f), "source arrives as 0..1, not as per cent");
        check (near (p.erDensity, 0.82f), "density arrives as 0..1, not as per cent");
        check (near (p.mix, 0.45f), "mix arrives as 0..1, not as per cent");
        check (near (p.width, 1.45f), "width arrives as a 0..2 M/S gain, not as per cent");
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
        check (typeFor (3) == Type::largeHall, "detent 3 is Large Hall");
        check (typeFor (4) == Type::plate, "detent 4 is Plate");
        check (typeFor (5) == Type::ambience, "detent 5 is Ambience");
        check (typeFor (-1) == Type::room && typeFor (99) == Type::room,
               "an out-of-range detent falls back to Room rather than reading off the end");

        check (erModeFor (0) == ErMode::taps, "er detent 0 is Taps");
        check (erModeFor (1) == ErMode::energy, "er detent 1 is Energy");
        check (erModeFor (2) == ErMode::blend, "er detent 2 is Blend");
        check (erModeFor (7) == ErMode::taps, "an out-of-range er detent falls back to Taps");
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

    //== The placeholder is a wire, and says so ==============================
    {
        ReverbDsp dsp;
        dsp.prepare (48000.0, 512, 2);

        const auto v = defaults();
        dsp.setParams (v.data(), (int) v.size());

        constexpr int n = 512;
        std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        float* channels[] { left.data(), right.data() };
        dsp.process (channels, 2, n);

        bool unchanged = near (left[0], 1.0f) && near (right[0], 1.0f);

        for (int i = 1; i < n; ++i)
            unchanged = unchanged && left[(size_t) i] == 0.0f && right[(size_t) i] == 0.0f;

        check (unchanged, "the placeholder passes audio through untouched and adds no tail");
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
