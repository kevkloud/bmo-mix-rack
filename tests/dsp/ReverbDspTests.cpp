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

int main()
{
    //== The schema and the adapter agree about length ========================
    {
        check (specs().size() == (size_t) Index::count, "the Index enum matches specs()");
        check (specs().size() == 24, "twenty-four parameters");
    }

    //== The adapter unpacks every field, and unpacks it correctly ============
    //
    // **This is the test that catches a transposed pair**, which is the one
    // mistake a twenty-four-field unpack invites and the one that no amount of
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
        v[Index::eqlofreq]    = 140.0f;
        v[Index::eqlo]        = -6.0f;
        v[Index::eqhifreq]    = 1400.0f;
        v[Index::eqhi]        = 4.5f;
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
