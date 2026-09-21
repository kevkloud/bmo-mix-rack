/*
    BMO Linger as a host sees it: schema, value strings, latency, state, presets.
    See EqTests.cpp for why the schema table is written out in full.

    **The DSP under this is still the placeholder** in
    modules/reverb/dsp/DspCore.h, so what is asserted here is everything that
    does not depend on there being a reverb yet -- which is the whole of the
    module's contract with a host. What is missing is named where it would
    otherwise sit, rather than left to be noticed.
*/

#include "TestUtil.h"
#include "modules/reverb/dsp/DspCore.h"
#include "modules/reverb/dsp/TapTables.h"
#include "modules/reverb/presets/FactoryPresets.h"
#include "products/reverb/Product.h"

using namespace test;
namespace P = bmo::reverb;

namespace
{
    // Permanent and append-only from the first release. The table is
    // docs/reverb/11-integration-and-test-plan.md section 4, which is the
    // authoritative copy of the schema.
    //
    // `steps` is 0 for every float, stepped or not: juce::AudioParameterFloat
    // reports the host's default step count whatever its interval is, so
    // VARIATION's seven positions and WIDTH's 201 both come back as 0 here.
    // The stepping is real and is asserted on the range below.
    //
    // If any of these thirty ever needs to change, this is the first thing
    // that fails, and 11 section 4 is where the argument has to be made.
    const Expected kSchema[]
    {
        { P::kType,       "Type",             0.0f,     5.0f,     0.0f,     6 },
        { P::kSize,       "Size",             0.5f,    80.0f,    12.0f,     0 },
        { P::kPreDelay,   "Pre-Delay",        0.0f,   250.0f,     0.0f,     0 },
        { P::kPreLink,    "Link ER",          0.0f,     1.0f,     0.0f,     2 },
        { P::kDecay,      "Decay",            0.1f,    20.0f,     1.8f,     0 },
        { P::kDecayShape, "Decay Shape",     0.04f,     3.5f,     3.5f,     0 },
        { P::kAttack,     "Attack",           0.0f,   100.0f,    30.0f,     0 },
        { P::kFeed,       "Source",           0.0f,   100.0f,    70.0f,     0 },
        { P::kDampLoFreq, "Low x Freq",      16.0f,  1600.0f,   200.0f,     0 },
        { P::kDampLo,     "Low x",           0.10f,    2.00f,    1.20f,     0 },
        { P::kDampHiFreq, "High x Freq",   1000.0f,  2100.0f,  1600.0f,     0 },
        { P::kDampHi,     "High x",          0.10f,    2.00f,    0.40f,     0 },
        { P::kEqLoFreq,   "EQ Low Freq",     16.0f,  1600.0f,   200.0f,     0 },
        { P::kEqLo,       "EQ Low",         -24.0f,    12.0f,     0.0f,     0 },
        { P::kEqHiFreq,   "EQ High Freq",  1000.0f,  2100.0f,  1600.0f,     0 },
        { P::kEqHi,       "EQ High",        -24.0f,    12.0f,     0.0f,     0 },
        { P::kErMode,     "ER Mode",          0.0f,     2.0f,     0.0f,     3 },
        { P::kErDensity,  "Density",          0.0f,   100.0f,    50.0f,     0 },
        { P::kErShape,    "ER Shape",         0.0f,     3.0f,     1.0f,     0 },
        { P::kErSpread,   "ER Spread",        5.0f,   200.0f,    80.0f,     0 },
        { P::kErHiCut,    "ER Hi-Cut",     1000.0f, 20000.0f,  7000.0f,     0 },
        { P::kErVariation,"Variation",        0.0f,     6.0f,     2.0f,     0 },
        { P::kModDepth,   "Mod Depth",        0.1f,     0.8f,    0.28f,     0 },
        { P::kModRate,    "Mod Rate",         0.1f,     1.2f,    0.50f,     0 },
        { P::kWidth,      "Width",            0.0f,   200.0f,   100.0f,     0 },
        { P::kInHiCut,    "In Hi-Cut",     2000.0f, 20000.0f, 20000.0f,     0 },
        { P::kErLevel,    "ER",             -40.0f,     0.0f,    -6.0f,     0 },
        { P::kVerbLevel,  "Reverb",         -40.0f,     0.0f,    -6.0f,     0 },
        { P::kMix,        "Mix",              0.0f,   100.0f,   100.0f,     0 },
        { P::kOutput,     "Output",         -24.0f,     0.0f,     0.0f,     0 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createReverb;

    //== The golden schema =====================================================
    {
        auto proc = createReverb();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
        check (P::specs().size() == 30, "v1 ships exactly thirty parameters");
    }

    //== Thirty against thirty-two, and the two spare are the point ===========
    //
    // A rack slot shows a host 32 lanes. Thirty fits, so nothing is held off
    // the grid and none of BMO DEQ's SlotOverflow machinery is needed -- and
    // **two lanes is all that is left**. A later Freeze, a ducking control or
    // the tempo-sync pair would exhaust them between them, which is why the
    // headroom is asserted rather than described: a thirty-first parameter
    // added without an argument fails the second line here.
    {
        check (P::specs().size() <= 32, "every parameter gets a rack host lane");
        check (32 - (int) P::specs().size() == 2,
               "exactly two host lanes are spare -- see modules/reverb/AGENTS.md");
    }

    //== The parameters that were considered and left out =====================
    //
    // Every one of these is a decision in 10 section 1 or 11 section 2 rather
    // than an oversight, and "we decided not to" and "somebody forgot" look
    // identical in a parameter list -- so they are asserted.
    //
    // `voicing` is the interesting one: the era block is three fields in each
    // type's constant table, deliberately, so that promoting them to a
    // 3-position control in v2 changes no type ordinals and no state layout.
    {
        for (const auto* absent : { "freeze", "duck", "ducking", "syncon", "syncdiv",
                                    "tempo", "voicing", "era", "oversampling",
                                    "lookahead", "bypass", "solo", "listen",
                                    "diffusion", "beta", "lines" })
            check (bmo::indexOfParam (P::specs(), absent) < 0,
                   juce::String ("there is no '") + absent + "' parameter");

        // Output is last, so an appended control cannot land between two
        // parameters a saved session already refers to by position.
        check (P::Index::output == P::Index::count - 1,
               "output is the last parameter, so anything new appends after it");
    }

    //== Room's defaults are Room's constants =================================
    //
    // **This is the assertion the whole per-type table will be written
    // against.** A fresh instance opens on TYPE = Room, so what it shows for
    // these eight is a claim about what Room *is* -- and if the DSP pass later
    // picks different Room constants, the panel lies about itself on the first
    // thing anyone sees. The per-type tables do not exist yet (10 section 1
    // names the categories and gives no numbers); when they are written,
    // Room's row is pinned here.
    {
        const auto def = [] (int i) { return P::specs()[(size_t) i].def; };

        checkClose (def (P::Index::size),      P::roomDefaults::kSizeM,      1.0e-4, "Room's SIZE");
        checkClose (def (P::Index::erdensity), P::roomDefaults::kErDensity,  1.0e-4, "Room's DENSITY");
        checkClose (def (P::Index::ershape),   P::roomDefaults::kErShape,    1.0e-4, "Room's ER SHAPE");
        checkClose (def (P::Index::erspread),  P::roomDefaults::kErSpreadMs, 1.0e-4, "Room's ER SPREAD");
        checkClose (def (P::Index::moddepth),  P::roomDefaults::kModDepthMs, 1.0e-4, "Room's MOD DEPTH");
        checkClose (def (P::Index::modrate),   P::roomDefaults::kModRateHz,  1.0e-4, "Room's MOD RATE");
        checkClose (def (P::Index::inhicut),   P::roomDefaults::kInHiCutHz,  1.0e-4, "Room's IN HI-CUT");
        checkClose (def (P::Index::feed),      P::roomDefaults::kFeed,       1.0e-4, "Room's SOURCE");

        // And the tap table is quoted at the same size, so a fresh instance's
        // display draws the table's own numbers rather than a scaled copy.
        checkClose (P::kReferenceSizeM, P::roomDefaults::kSizeM, 1.0e-4,
                    "the reference tap table is quoted at Room's SIZE");

        // TYPE opens on Room, which is what makes any of the above mean
        // anything.
        check ((int) P::specs()[P::Index::type].def == (int) P::room,
               "a fresh instance opens on Room");
    }

    //== Both choice lists, in order ==========================================
    //
    // Names and index order freeze with the ids. `tools/snapshot` sets a
    // choice by name, so these strings are also a command-line interface.
    //
    // **Appending is legal for state and lossy for automation.** A session
    // stores plain values keyed by id (ParamSet::toXml), so a seventh type
    // changes nothing a saved file refers to -- but AudioParameterChoice
    // normalises as index/(n-1), so every automation point on this lane
    // rescales. Asserted here as arithmetic, because it is the trade someone
    // adding Church will be making and there is nowhere else it is written
    // down as a number.
    {
        auto proc = createReverb();

        const auto choice = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        check (choice (P::kType, 0.0f) == "Room", "type 0 is Room");
        check (choice (P::kType, 1.0f) == "Chamber", "type 1 is Chamber");
        check (choice (P::kType, 2.0f) == "Hall", "type 2 is Hall");
        check (choice (P::kType, 3.0f) == "Large Hall", "type 3 is Large Hall");
        check (choice (P::kType, 4.0f) == "Plate", "type 4 is Plate");
        check (choice (P::kType, 5.0f) == "Ambience", "type 5 is Ambience");

        check (choice (P::kErMode, 0.0f) == "Taps", "er mode 0 is Taps");
        check (choice (P::kErMode, 1.0f) == "Energy", "er mode 1 is Energy");
        check (choice (P::kErMode, 2.0f) == "Blend", "er mode 2 is Blend");

        // Ambience is 1.0 normalised with six types and 0.833 with seven, so
        // an automation lane that pointed at Ambience would land on Plate.
        const auto normalisedOf = [] (int index, int n)
        {
            return (float) index / (float) (n - 1);
        };

        checkClose (normalisedOf (P::ambience, P::numTypes), 1.0, 1.0e-6,
                    "Ambience is the top of the lane with six types");
        check (std::abs (normalisedOf (P::ambience, P::numTypes + 1) - 1.0f) > 0.1f,
               "a seventh type would move Ambience off the top of the lane");
    }

    //== Displayed values, at both ends of every knob =========================
    //
    // **Every string here is ASCII**, and that is a build constraint rather
    // than a preference: the two display faces are licensed individually and
    // live outside this repository (see .bmo-fontdir), so a glyph outside
    // ASCII is one this suite cannot promise it can draw. That is why the
    // damping multipliers print "1.20x" rather than using a multiplication
    // sign, and why the panel's captions read "LOW x".
    {
        auto proc = createReverb();

        const auto text = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        const auto isAscii = [] (const juce::String& s)
        {
            for (auto c : s)
                if (c < 32 || c > 126)
                    return false;

            return true;
        };

        // SIZE in metres and DECAY in seconds are the two formats this module
        // added to ParamSpec, on top of the shared textParam hunk. They are
        // asserted here because they are the only two call sites in the suite.
        check (text (P::kSize, 12.0f) == "12.0 m",
               "SIZE at the default should read '12.0 m', got '" + text (P::kSize, 12.0f) + "'");
        check (text (P::kSize, 0.5f) == "0.5 m",
               "SIZE at the bottom should read '0.5 m', got '" + text (P::kSize, 0.5f) + "'");
        check (text (P::kSize, 80.0f) == "80.0 m",
               "SIZE at the top should read '80.0 m', got '" + text (P::kSize, 80.0f) + "'");

        check (text (P::kDecay, 1.8f) == "1.80 s",
               "DECAY at the default should read '1.80 s', got '" + text (P::kDecay, 1.8f) + "'");
        check (text (P::kDecay, 20.0f) == "20.0 s",
               "DECAY at the top should read '20.0 s', got '" + text (P::kDecay, 20.0f) + "'");

        // The multipliers are not gains and not times, so they carry an x.
        check (text (P::kDampLo, 1.20f) == "1.20x",
               "LOW x should read '1.20x', got '" + text (P::kDampLo, 1.20f) + "'");
        check (text (P::kDampHi, 0.40f) == "0.40x",
               "HIGH x should read '0.40x', got '" + text (P::kDampHi, 0.40f) + "'");

        // **"Off" and not "-40.0 dB".** At the bottom of its travel a fader is
        // silence, and 11 section 6 tests it as a silence rather than as a
        // level -- so a string promising an audible tail 40 dB down would be
        // promising something that is not there.
        check (text (P::kErLevel, -40.0f) == "Off",
               "ER at the bottom should read 'Off', got '" + text (P::kErLevel, -40.0f) + "'");
        check (text (P::kVerbLevel, -40.0f) == "Off",
               "REVERB at the bottom should read 'Off', got '" + text (P::kVerbLevel, -40.0f) + "'");
        check (text (P::kErLevel, -6.0f) == "-6.0 dB",
               "ER at the default should read '-6.0 dB', got '" + text (P::kErLevel, -6.0f) + "'");

        // **"Cut" and not "-24.0 dB".** -24 on a shelf is where an EQ move
        // stops being a move.
        check (text (P::kEqLo, -24.0f) == "Cut",
               "EQ LOW at the bottom should read 'Cut', got '" + text (P::kEqLo, -24.0f) + "'");
        check (text (P::kEqHi, -24.0f) == "Cut",
               "EQ HIGH at the bottom should read 'Cut', got '" + text (P::kEqHi, -24.0f) + "'");
        check (text (P::kEqLo, 3.0f) == "+3.0 dB",
               "EQ LOW above zero should read '+3.0 dB', got '" + text (P::kEqLo, 3.0f) + "'");

        // 3.5 is linear -- the truncation switched off -- and the word is what
        // the number means.
        check (text (P::kDecayShape, 3.5f) == "3.50 (Linear)",
               "DECAY SHAPE at the top should read '3.50 (Linear)', got '"
                   + text (P::kDecayShape, 3.5f) + "'");

        // VARIATION 6 is not more of VARIATION 5: it is the complementary-comb
        // pair, where the ER vanish entirely in a mono sum. The value string
        // is the only place a host's automation lane can say so.
        check (text (P::kErVariation, 2.0f) == "Var 2",
               "VARIATION at the default should read 'Var 2', got '"
                   + text (P::kErVariation, 2.0f) + "'");
        check (text (P::kErVariation, 6.0f).contains ("mono"),
               "VARIATION 6 should name its mono behaviour, got '"
                   + text (P::kErVariation, 6.0f) + "'");

        // MOD DEPTH keeps two decimals: 0.28 is not a round number, it is the
        // peak deviation the 3-cent pitch bound allows at 1 Hz.
        check (text (P::kModDepth, 0.28f) == "0.28 ms",
               "MOD DEPTH should read '0.28 ms', got '" + text (P::kModDepth, 0.28f) + "'");

        for (const auto& spec : P::specs())
            for (const auto v : { 0.0f, 0.37f, 0.5f, 1.0f })
                check (isAscii (param (*proc, spec.id).getText (v, 0)),
                       juce::String ("'") + spec.id + "' prints ASCII only at normalised "
                           + juce::String (v));
    }

    //== Latency: zero, at every setting ======================================
    //
    // Not "zero at the default" -- zero everywhere, because there is no
    // lookahead, no oversampling and **no negative pre-delay**. The last of
    // those is the one that had to be refused by name: it delays the module's
    // whole output against the host timeline, which is latency, and an
    // automatable one would thrash PDC on every move (10 section 2). Latency
    // is permanent once shipped, so this is the costly one to get wrong.
    {
        auto proc = createReverb();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);

        const auto latencyAt = [&proc] (float typeValue, float pre, float dec, float sz)
        {
            setValue (*proc, P::kType, typeValue);
            setValue (*proc, P::kPreDelay, pre);
            setValue (*proc, P::kDecay, dec);
            setValue (*proc, P::kSize, sz);
            proc->prepareToPlay (48000.0, 512);
            return proc->getLatencySamples();
        };

        for (float t = 0.0f; t < (float) P::numTypes; t += 1.0f)
            for (const auto pre : { 0.0f, 250.0f })
                for (const auto dec : { 0.1f, 20.0f })
                    for (const auto sz : { 0.5f, 80.0f })
                        check (latencyAt (t, pre, dec, sz) == 0, "zero latency at every setting");

        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto fresh = createReverb();
            fresh->setPlayConfigDetails (2, 2, rate, 64);
            fresh->prepareToPlay (rate, 64);
            check (fresh->getLatencySamples() == 0,
                   "a fresh instance reports zero latency at " + juce::String (rate));
        }
    }

    //== The tail figure, which nothing reports yet ===========================
    //
    // `getTailLengthSeconds()` is hardcoded to 0.0 in both processors and
    // `ModuleDsp` has no tail accessor, so **there is nothing to assert
    // against a host here** -- adding the accessor lands on every module's
    // vtable and is milestone M5 and its own reviewed commit (11 section 2a).
    // What can be asserted now is the arithmetic that commit will call, and
    // the ceiling that keeps it sane.
    {
        using Core = bmo::reverb::DspCore;

        Core::Params p;
        check (Core::tailSecondsFor (p) > p.decaySeconds,
               "a reported tail is longer than the decay it is computed from");

        // 20 s at a 2.0 multiplier is an effective 40 s, and the host is told
        // 30: over-reporting only costs idle pulling, but 40 s per instance is
        // past the point where that is a fair trade.
        Core::Params worst;
        worst.decaySeconds = 20.0f;
        worst.dampLo = 2.0f;
        worst.dampHi = 2.0f;
        worst.preDelayMs = 250.0f;
        worst.sizeM = 80.0f;
        checkClose (Core::tailSecondsFor (worst), (double) Core::kMaxTailSeconds, 1.0e-4,
                    "the reported tail is clamped to 30 s");

        // And it still reports zero to the host today, which is what makes the
        // M5 commit a change rather than a fix.
        auto proc = createReverb();
        proc->prepareToPlay (48000.0, 512);
        checkClose (proc->getTailLengthSeconds(), 0.0, 1.0e-9,
                    "the processor still reports no tail -- 11 section 2a is M5");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kType, (float) P::hall },
            { P::kSize, 33.5f },
            { P::kPreDelay, 72.5f },
            { P::kPreLink, 1.0f },
            { P::kDecay, 4.25f },
            { P::kDecayShape, 1.2f },
            { P::kAttack, 55.0f },
            { P::kFeed, 40.0f },
            { P::kDampLo, 1.55f },
            { P::kDampHi, 0.65f },
            { P::kEqLo, -6.0f },
            { P::kEqHi, 4.5f },
            { P::kErMode, (float) P::energy },
            { P::kErDensity, 82.0f },
            { P::kErShape, 2.4f },
            { P::kErSpread, 125.0f },
            { P::kErHiCut, 4500.0f },
            { P::kErVariation, 5.0f },
            { P::kModDepth, 0.55f },
            { P::kModRate, 0.9f },
            { P::kWidth, 145.0f },
            { P::kInHiCut, 9000.0f },
            { P::kErLevel, -18.5f },
            { P::kVerbLevel, -3.5f },
            { P::kMix, 45.0f },
            { P::kOutput, -7.5f },
        };

        {
            auto proc = createReverb();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createReverb();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.05,
                        juce::String ("state round-trip of '") + s.id + "'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>");

        // **State is plain values keyed by id, which is what makes the type
        // list append-only for sessions.** Asserted against the XML rather
        // than against the parameter list, because it is the storage form that
        // gives the guarantee: a seventh type changes no normalisation here.
        if (xml != nullptr)
        {
            auto* typeElement = xml->getChildByAttribute ("id", P::kType);
            check (typeElement != nullptr, "type is stored by id");

            if (typeElement != nullptr)
                checkClose (typeElement->getDoubleAttribute ("value"), (double) P::hall, 1.0e-6,
                            "type is stored as its plain detent index, not as a normalised value");
        }
    }

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-reverb-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createReverb();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        check (presets.extension() == ".bmoreverb", "presets are .bmoreverb files");

        // Init is index 0 and is all defaults -- modules/AGENTS.md step 6.
        check (P::factory()[0].settings.empty(), "Init sets nothing, so it is the defaults");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);
            check (presets.getCurrentName() == preset.name, "loading names the preset");

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.05,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        presets.loadFactory (0);
        setValue (*proc, P::kDecay, 6.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kDecay), 6.0, 0.05, "user preset restores decay");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== Not asserted yet, and here is where it goes ==========================
    //
    // **No preset level check.** Every other module's suite checks that a
    // preset comes out at the level it went in, against its OUTPUT. This one
    // has an OUTPUT, so the check belongs -- but the DSP is a pass-through, so
    // every preset comes out at exactly the input level and the check would
    // pass for the wrong reason. It is written when there is a reverb to
    // match. That is a deferral, unlike BMO Defang's, where the absence is by
    // construction.
    //
    // **Nothing about metering, solo or an analyser tap**, and that absence is
    // permanent: the panel's display is drawn from the parameters and from the
    // shared tap table, neither doc asks this module for a meter, and a reverb
    // has no gain reduction to report.
    //
    // **Everything about the sound** -- T60, damping, tap times, comb and flam
    // rules, echo density, modulation, the level laws and the phasing nulls --
    // is arithmetic on DspCore and lives in tests/dsp/ReverbDspTests.cpp,
    // where it can run in the seconds-long Linux job with no JUCE. 11 section
    // 6 is the list.

    return finish ("BMO Linger");
}
