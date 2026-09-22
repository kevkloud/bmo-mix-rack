/*
    BMO FET as a host sees it: schema, value strings, latency, state, presets.
    See EqTests.cpp for why the schema table is written out in full.

    **The DSP under this is still the placeholder** in
    modules/fetcomp/dsp/DspCore.h, so what is asserted here is everything that
    does not depend on there being a compressor yet -- which is the whole of
    the module's contract with a host. The two things that are missing are
    named where they would otherwise sit, rather than left to be noticed:
    preset level matching, and anything about gain reduction.
*/

#include "TestUtil.h"
#include "modules/fetcomp/presets/FactoryPresets.h"
#include "products/fetcomp/Product.h"

using namespace test;
namespace P = bmo::fetcomp;

namespace
{
    // Permanent and append-only from the first release. The table is
    // docs/fet-comp/11-integration-and-test-plan.md section 2, and the two
    // gain ranges are the wide ones on purpose: +45 dB of input is about 5 dB
    // short of 30 dB of reduction at 4:1 from a -18 dBFS source, and +/-24 dB
    // of output cannot restore 30 dB of it. If either is ever narrowed, this
    // is the first thing that fails.
    const Expected kSchema[]
    {
        { P::kInput,        "Input",        -20.0f,  60.0f,   0.0f, 0 },
        { P::kOutput,       "Output",       -36.0f,  36.0f,   0.0f, 0 },
        { P::kAttack,       "Attack",         1.0f,   7.0f,   4.0f, 0 },
        { P::kRelease,      "Release",        1.0f,   7.0f,   4.0f, 0 },
        { P::kRatio,        "Ratio",          0.0f,   4.0f,   0.0f, 5 },
        { P::kMix,          "Mix",            0.0f, 100.0f, 100.0f, 0 },
        { P::kVoicing,      "Voicing",        0.0f,   1.0f,   1.0f, 2 },
        { P::kOversampling, "Oversampling",   0.0f,   2.0f,   0.0f, 3 },
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using bmo::products::createFetcomp;

    //== The golden schema =====================================================
    {
        auto proc = createFetcomp();
        checkSchema (*proc, kSchema);
        check (P::specs().size() == (size_t) P::Index::count, "the Index enum matches specs()");
    }

    //== There is no sidechain filter and no stereo-link parameter =============
    // Both were considered and left out for v1 -- stereo is always linked, the
    // same reasoning LTV Comp records for having no LINK switch. Either could
    // be appended later at the end of specs(); neither can be inserted.
    // Asserted rather than left implied, because "we decided not to" and
    // "somebody forgot" look identical in a parameter list.
    {
        auto proc = createFetcomp();

        check (bmo::indexOfParam (P::specs(), "sidechain") < 0, "there is no sidechain parameter");
        check (bmo::indexOfParam (P::specs(), "link") < 0, "there is no stereo-link parameter");
        check (bmo::indexOfParam (P::specs(), "threshold") < 0, "there is no threshold parameter");
    }

    //== Attack and release run backwards, and they say what they mean ========
    //
    // This is the assertion that fails if the direction is ever "corrected".
    // The parameter *is* the knob position -- 1 slowest, 7 fastest -- so the
    // host's automation lane runs the same way the knob does, and the value
    // string has to carry the time the position selects or a host leaning on
    // the raw number shows a bare "4".
    {
        auto proc = createFetcomp();

        const auto text = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        check (text (P::kAttack, 1.0f) == "1 (800 us)",
               "attack at position 1 should read '1 (800 us)', got '" + text (P::kAttack, 1.0f) + "'");
        check (text (P::kAttack, 4.0f) == "4 (126 us)",
               "attack at position 4 should read '4 (126 us)', got '" + text (P::kAttack, 4.0f) + "'");
        check (text (P::kAttack, 7.0f) == "7 (20 us)",
               "attack at position 7 should read '7 (20 us)', got '" + text (P::kAttack, 7.0f) + "'");

        check (text (P::kRelease, 1.0f) == "1 (1100 ms)",
               "release at position 1 should read '1 (1100 ms)', got '" + text (P::kRelease, 1.0f) + "'");
        check (text (P::kRelease, 7.0f) == "7 (50 ms)",
               "release at position 7 should read '7 (50 ms)', got '" + text (P::kRelease, 7.0f) + "'");

        // Higher position = faster, at every pair.
        for (int i = 1; i < 7; ++i)
        {
            check (P::attackMicrosecondsFor ((float) (i + 1)) < P::attackMicrosecondsFor ((float) i),
                   "attack position " + juce::String (i + 1) + " should be faster than " + juce::String (i));
            check (P::releaseMillisecondsFor ((float) (i + 1)) < P::releaseMillisecondsFor ((float) i),
                   "release position " + juce::String (i + 1) + " should be faster than " + juce::String (i));
        }

        // A position between two detents prints as one, not as an integer it
        // is not sitting on.
        check (text (P::kAttack, 4.5f).startsWith ("4.50 ("),
               "an attack between detents should print its position, got '" + text (P::kAttack, 4.5f) + "'");
    }

    //== The choice lists, in order ============================================
    // Names and index order freeze with the ids. `tools/snapshot` sets a
    // choice by name, so these strings are also a command-line interface.
    {
        auto proc = createFetcomp();

        const auto choice = [&proc] (const char* id, float v)
        {
            setValue (*proc, id, v);
            return param (*proc, id).getCurrentValueAsText();
        };

        check (choice (P::kRatio, 0.0f) == "4:1", "ratio 0 is 4:1");
        check (choice (P::kRatio, 4.0f) == "All", "ratio 4 is the all-buttons position");
        check (choice (P::kVoicing, 0.0f) == "Blue", "voicing 0 is Blue");
        check (choice (P::kVoicing, 1.0f) == "Black", "voicing 1 is Black");
        check (choice (P::kOversampling, 2.0f) == "4x", "oversampling 2 is 4x");
    }

    //== Displayed values, for the two that carry a unit ======================
    {
        auto proc = createFetcomp();

        setValue (*proc, P::kInput, 18.0f);
        check (param (*proc, P::kInput).getCurrentValueAsText() == "+18.0 dB",
               "Input should read '+18.0 dB', got '" + param (*proc, P::kInput).getCurrentValueAsText() + "'");

        setValue (*proc, P::kMix, 35.0f);
        check (param (*proc, P::kMix).getCurrentValueAsText() == "35 %",
               "Mix should read '35 %', got '" + param (*proc, P::kMix).getCurrentValueAsText() + "'");
    }

    //== Latency: 0 / 40 / 60, zero at the default, the same in both voicings ==
    // Pinned as numbers rather than derived from the oversampler, the way the
    // EQ review pinned its own: a figure the host is told has to be a figure
    // somebody chose, not whatever a shared header happens to return.
    {
        auto proc = createFetcomp();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);

        const auto latencyAt = [&proc] (float factor, float voicing)
        {
            setValue (*proc, P::kOversampling, factor);
            setValue (*proc, P::kVoicing, voicing);
            proc->prepareToPlay (48000.0, 512);
            return proc->getLatencySamples();
        };

        for (const auto voicing : { 0.0f, 1.0f })
        {
            check (latencyAt (0.0f, voicing) == 0,  "oversampling Off reports zero latency");
            check (latencyAt (1.0f, voicing) == 40, "oversampling 2x reports 40 samples");
            check (latencyAt (2.0f, voicing) == 60, "oversampling 4x reports 60 samples");
        }

        // And the default state is the zero-latency one, which is the suite's
        // rule for every module.
        auto fresh = createFetcomp();
        fresh->setPlayConfigDetails (2, 2, 48000.0, 512);
        fresh->prepareToPlay (48000.0, 512);
        check (fresh->getLatencySamples() == 0, "a fresh instance reports zero latency");
    }

    //== State round-trip ======================================================
    {
        juce::MemoryBlock state;

        const bmo::Setting settings[] {
            { P::kInput, 22.5f },
            { P::kOutput, -6.5f },
            { P::kAttack, 6.25f },
            { P::kRelease, 2.5f },
            { P::kRatio, 4.0f },
            { P::kMix, 40.0f },
            { P::kVoicing, 0.0f },
            { P::kOversampling, 1.0f },
        };

        {
            auto proc = createFetcomp();
            for (const auto& s : settings) setValue (*proc, s.id, s.value);
            proc->getStateInformation (state);
        }

        auto restored = createFetcomp();
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& s : settings)
            checkClose (getValue (*restored, s.id), s.value, 0.01,
                        juce::String ("state round-trip of '") + s.id + "'");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        check (xml != nullptr && xml->hasTagName ("PARAMS") && xml->hasAttribute ("stateVersion"),
               "saved state is <PARAMS stateVersion=..>");
    }

    //== The rack slot fits ====================================================
    // Eight parameters against a slot's 32 host lanes, so nothing here is held
    // off the grid. RackTests.cpp holds the bank table that pins which lane is
    // which; this is the count on its own.
    check (P::specs().size() <= 32, "every parameter gets a rack host lane");

    //== Presets ==============================================================
    {
        const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("bmo-fetcomp-preset-tests");
        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting (sandbox);

        auto proc = createFetcomp();
        auto& presets = proc->getPresets();

        check (presets.getFactory().size() == P::factory().size(), "every factory preset is listed");
        check (presets.getCurrentName() == "Init", "the plugin starts on Init");
        check (presets.extension() == ".bmofetcomp", "presets are .bmofetcomp files");

        // Init is index 0 and is all defaults -- modules/AGENTS.md step 6.
        check (P::factory()[0].settings.empty(), "Init sets nothing, so it is the defaults");

        for (int i = 0; i < (int) P::factory().size(); ++i)
        {
            const auto& preset = P::factory()[(size_t) i];
            presets.loadFactory (i);
            check (presets.getCurrentName() == preset.name, "loading names the preset");

            for (const auto& s : preset.settings)
                checkClose (getValue (*proc, s.id), s.value, 0.01,
                            juce::String ("preset \"") + preset.name + "\" sets " + s.id);
        }

        presets.loadFactory (0);
        setValue (*proc, P::kInput, 12.0f);
        check (presets.isEdited(), "moving a control marks the preset edited");
        check (presets.saveUser ("Round Trip"), "a user preset saves");
        presets.loadFactory (0);
        presets.loadUser ("Round Trip");
        checkClose (getValue (*proc, P::kInput), 12.0, 0.01, "user preset restores input");
        check (presets.deleteUser ("Round Trip"), "a user preset deletes");

        sandbox.deleteRecursively();
        bmo::PresetManager::setDirectoryForTesting ({});
    }

    //== Not asserted yet, and here is where it goes ==========================
    //
    // **Preset level matching.** Every other module's suite checks that a
    // preset comes out at the level it went in. It cannot mean anything while
    // the DSP is a pass-through: INPUT is a plain gain there, so the only
    // OUTPUT that would pass is its own negative, and pinning that now would
    // pin a number the real compressor will immediately contradict. The
    // presets carry OUTPUT 0 for the same reason -- see
    // modules/fetcomp/presets/FactoryPresets.h. Add the check with the makeup
    // pass, milestone M6.
    //
    // **Gain reduction.** currentGainReductionDb() is a flat zero in the
    // placeholder, so there is nothing to assert about it here; the real
    // figure, including past the meter's 24 dB pin, is asserted in
    // tests/dsp/FetcompDspTests.cpp once there is a cell to report one.

    return finish ("BMO FET");
}
